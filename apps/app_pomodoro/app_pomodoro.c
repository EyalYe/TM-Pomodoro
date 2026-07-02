/*
 * app_pomodoro — a graphical work/break timer for TaskMaster-C3.
 *
 * A full-screen app (no hint bar) that shows two circles — Work and Break. Scroll the
 * encoder to move between them, click to edit a circle's length, Select to start the
 * cycle (Work, then Break auto-follows), long-press Select to reset. While a phase
 * runs, its circle shrinks in proportion to the time left. Demonstrates: full-width
 * drawing, raw LVGL widgets, the 1.1 live tick, and the 1.2 long-press — all against a
 * sealed core the app never touches.
 */
#include "app.h"          /* device_app_t + TM_API_VERSION + TASKMASTER_REGISTER_APP */
#include "input.h"        /* EV_* incl. EV_SELECT_LONG */
#include "ui_frame.h"     /* ui_frame_content / ui_text / control hints */
#include "app_store.h"    /* persist the work/break lengths */
#include "app_config.h"   /* also expose them in Settings */

#include "esp_timer.h"
#include <stdio.h>

/* Needs the live tick (1.1) and long-press Select (1.2). */
TASKMASTER_REQUIRE_API(1, 2);

/* ── tunables ── */
#define WORK_DEF_MIN    25
#define BREAK_DEF_MIN    5
#define MIN_MIN          1
#define MAX_MIN         90
#define SECS_PER_MIN    60
#define US_PER_SEC      1000000LL
#define TICK_MS       1000

/* ── geometry (128×64, full width) ── */
#define DISC_D          44        /* full circle diameter */
#define DISC_MIN_D       2        /* below this, the shrinking disc is gone */
#define WORK_CX         34        /* left circle centre x */
#define BREAK_CX        94        /* right circle centre x */
#define DISC_CY         27        /* circle centre y */
#define FOCUS_D        (DISC_D - 10) /* focus ring — inside the main circle */
#define EDIT_D         (DISC_D - 16) /* edit ring — further inside */
#define DISC_BORDER      3        /* main outline-ring thickness */
#define THIN_BORDER      1        /* focus / edit ring thickness */
#define LABEL_Y         54        /* label / countdown baseline y */

typedef enum { PH_IDLE, PH_WORK, PH_BREAK } phase_t;

/* Knob-editable lengths — also appear in Settings → Pomodoro (shared app_store keys). */
static const app_cfg_field_t POMO_CFG[] = {
    { .key = "work",  .label = "Work min",  .type = ACFG_U8, .input = ACFG_KNOB, .min = MIN_MIN, .max = MAX_MIN },
    { .key = "break", .label = "Break min", .type = ACFG_U8, .input = ACFG_KNOB, .min = MIN_MIN, .max = MAX_MIN },
};
TASKMASTER_REGISTER_APP_CONFIG("pomo", "Pomodoro", POMO_CFG);

static app_store_t s_store;
static int      s_min[2];      /* [0]=work, [1]=break minutes */
static int      s_focus;       /* selected circle: 0=work, 1=break */
static bool     s_editing;     /* editing the focused circle's length */
static phase_t  s_phase;       /* running phase; PH_IDLE = not running */
static bool     s_running;     /* counting down vs. paused (within a phase) */
static int64_t  s_end_us;      /* absolute end time when running */
static int      s_remain_s;    /* frozen remaining when paused */

static const char *KEY[2]  = { "work", "break" };
static const char *NAME[2] = { "Work", "Break" };
static const int   CX[2]   = { WORK_CX, BREAK_CX };

static int remaining_s(void)
{
    if (!s_running) return s_remain_s;
    int64_t left = s_end_us - esp_timer_get_time();
    return left > 0 ? (int)(left / US_PER_SEC) : 0;
}

static void start_phase(phase_t ph)
{
    s_phase    = ph;
    s_remain_s = s_min[ph == PH_WORK ? 0 : 1] * SECS_PER_MIN;
    s_end_us   = esp_timer_get_time() + (int64_t)s_remain_s * US_PER_SEC;
    s_running  = true;
}

static void reset_all(void)
{
    s_phase = PH_IDLE;
    s_running = false;
    s_editing = false;
}

static void pomo_init(void)
{
    app_store_open(&s_store, "pomo");
    for (int i = 0; i < 2; i++) {
        uint32_t m = (i == 0) ? WORK_DEF_MIN : BREAK_DEF_MIN;
        app_store_get_u32(&s_store, KEY[i], &m, m);
        s_min[i] = (m < MIN_MIN) ? MIN_MIN : (m > MAX_MIN ? MAX_MIN : (int)m);
    }
    reset_all();
    s_focus = 0;
}

static void pomo_on_event(uint8_t ev)
{
    if (ev == EV_SELECT_LONG) { reset_all(); return; }   /* reset from any state */

    if (s_phase != PH_IDLE) {                            /* running / paused */
        if (ev == EV_ENCODER_CLICK) {                    /* pause / resume */
            if (s_running) { s_remain_s = remaining_s(); s_running = false; }
            else { s_end_us = esp_timer_get_time() + (int64_t)s_remain_s * US_PER_SEC; s_running = true; }
        }
        return;
    }

    if (s_editing) {                                     /* editing the focused length */
        int *m = &s_min[s_focus];
        switch (ev) {
        case EV_ENCODER_CW:  if (*m < MAX_MIN) (*m)++; break;
        case EV_ENCODER_CCW: if (*m > MIN_MIN) (*m)--; break;
        case EV_ENCODER_CLICK:
            app_store_set_u32(&s_store, KEY[s_focus], (uint32_t)*m);   /* save on exit */
            s_editing = false;
            break;
        default: break;
        }
        return;
    }

    switch (ev) {                                        /* idle: pick / start */
    case EV_ENCODER_CW:
    case EV_ENCODER_CCW:   s_focus ^= 1;            break;   /* scroll between circles */
    case EV_ENCODER_CLICK: s_editing = true;        break;   /* edit the focused one */
    case EV_SELECT:        start_phase(PH_WORK);     break;   /* start the cycle */
    default: break;
    }
}

/* Draw a filled disc (border ignored) or an outline ring of `border` px, centred at
 * (cx, cy). */
static void disc(int cx, int cy, int d, bool filled, int border)
{
    if (d < DISC_MIN_D) return;
    lv_obj_t *o = lv_obj_create(ui_frame_content());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    if (filled) {
        lv_obj_set_style_bg_color(o, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_border_width(o, border, 0);
        lv_obj_set_style_border_color(o, lv_color_white(), 0);
    }
}

/* Text centred at (cx, cy). */
static void text_center(int cx, int cy, const char *s)
{
    lv_obj_t *l = ui_text(0, 0, s);
    lv_obj_update_layout(l);
    lv_obj_set_pos(l, cx - lv_obj_get_width(l) / 2, cy - lv_obj_get_height(l) / 2);
}

static void pomo_render(void)
{
    lv_obj_clean(ui_frame_content());
    ui_frame_set_hints(NULL);                            /* full screen — no hint bar */

    /* Auto-advance a finished phase. */
    if (s_phase != PH_IDLE && s_running && remaining_s() <= 0) {
        if (s_phase == PH_WORK) start_phase(PH_BREAK);   /* Work done → Break */
        else reset_all();                                /* Break done → back to idle */
    }

    for (int i = 0; i < 2; i++) {
        bool active = (s_phase == (i == 0 ? PH_WORK : PH_BREAK));
        if (active) {
            int total = s_min[i] * SECS_PER_MIN;
            int rem   = remaining_s();
            int d     = total > 0 ? DISC_D * rem / total : 0;   /* shrink with time left */
            disc(CX[i], DISC_CY, d, true, 0);
            char t[16];
            snprintf(t, sizeof(t), "%02d:%02d", rem / SECS_PER_MIN, rem % SECS_PER_MIN);
            text_center(CX[i], LABEL_Y, s_running ? t : "paused");
        } else {
            disc(CX[i], DISC_CY, DISC_D, false, DISC_BORDER);   /* thick main ring */
            char m[8];
            snprintf(m, sizeof(m), "%d", s_min[i]);
            text_center(CX[i], DISC_CY, m);                  /* minutes inside */
            text_center(CX[i], LABEL_Y, NAME[i]);            /* Work / Break */
            if (s_phase == PH_IDLE && s_focus == i) {        /* focus / edit rings (inside, thin) */
                disc(CX[i], DISC_CY, FOCUS_D, false, THIN_BORDER);
                if (s_editing) disc(CX[i], DISC_CY, EDIT_D, false, THIN_BORDER);
            }
        }
    }
}

static void pomo_exit(void)
{
    app_store_close(&s_store);
}

static const device_app_t pomodoro_app = {
    .name     = "Pomodoro",
    .init     = pomo_init,
    .on_event = pomo_on_event,
    .render   = pomo_render,
    .exit     = pomo_exit,
    .tick_ms  = TICK_MS,          /* live countdown (app-API 1.1) */
};
TASKMASTER_REGISTER_APP(pomodoro_app);
