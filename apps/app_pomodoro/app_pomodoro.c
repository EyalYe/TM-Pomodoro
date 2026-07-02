/*
 * app_pomodoro — a work/break timer for TaskMaster-C3.
 *
 * A complete, real example app (not a task source): it self-registers, persists its
 * config, and uses the app-API 1.1 `tick_ms` to update a live countdown once a second.
 * Controls: encoder-push = start/pause, Select = next phase. It never touches core.
 */
#include "app.h"          /* device_app_t + TASKMASTER_REGISTER_APP + TM_API_VERSION */
#include "input.h"        /* EV_* */
#include "ui_frame.h"     /* ui_text_row / ui_frame_content / control_hints_t */
#include "app_store.h"    /* our private, persistent config */
#include "app_config.h"   /* declare the knob-editable work/break lengths */

#include "esp_timer.h"    /* esp_timer_get_time() — a monotonic timestamp */
#include <stdio.h>

/* We use the 1.1 `tick_ms` field, so require at least app-API 1.1. */
TASKMASTER_REQUIRE_API(1, 1);

#define POMO_WORK_DEF   25    /* default work minutes */
#define POMO_BREAK_DEF   5    /* default break minutes */
#define POMO_MIN_MIN     1    /* knob range: minutes */
#define POMO_MIN_MAX    90
#define SECS_PER_MIN    60
#define US_PER_SEC      1000000LL
#define POMO_TICK_MS    1000  /* re-render once a second for the countdown */

#define TITLE_ROW  0
#define TIME_ROW   1
#define STATE_ROW  2

/* Knob-editable lengths (appear in Settings under "Pomodoro"). */
static const app_cfg_field_t POMO_CFG[] = {
    { .key = "work",  .label = "Work min",  .type = ACFG_U8, .input = ACFG_KNOB,
      .min = POMO_MIN_MIN, .max = POMO_MIN_MAX },
    { .key = "break", .label = "Break min", .type = ACFG_U8, .input = ACFG_KNOB,
      .min = POMO_MIN_MIN, .max = POMO_MIN_MAX },
};
TASKMASTER_REGISTER_APP_CONFIG("pomo", "Pomodoro", POMO_CFG);

static app_store_t s_store;
static bool        s_work;       /* true = work phase, false = break */
static bool        s_running;
static int64_t     s_end_us;     /* when running: absolute end timestamp (µs) */
static int         s_remain_s;   /* when paused: frozen remaining seconds */

/* Length of a phase in seconds, from the (knob-set) config. */
static int phase_len_s(bool work)
{
    uint32_t m = work ? POMO_WORK_DEF : POMO_BREAK_DEF;
    app_store_get_u32(&s_store, work ? "work" : "break", &m, m);
    if (m < POMO_MIN_MIN) m = POMO_MIN_MIN;
    return (int)m * SECS_PER_MIN;
}

/* Reset to the start of `work`/break phase, paused. */
static void load_phase(bool work)
{
    s_work     = work;
    s_running  = false;
    s_remain_s = phase_len_s(work);
}

/* Seconds left right now (computed from the timestamp while running — so it stays
 * correct even across a screen blank, since we don't count ticks). */
static int remaining_s(void)
{
    if (!s_running) {
        return s_remain_s;
    }
    int64_t left = s_end_us - esp_timer_get_time();
    return left > 0 ? (int)(left / US_PER_SEC) : 0;
}

static void toggle_run(void)
{
    if (s_running) {
        s_remain_s = remaining_s();          /* freeze */
        s_running  = false;
    } else {
        s_end_us  = esp_timer_get_time() + (int64_t)s_remain_s * US_PER_SEC;
        s_running = true;
    }
}

static void pomo_init(void)
{
    app_store_open(&s_store, "pomo");
    load_phase(true);                          /* start at Work, paused */
}

static void pomo_on_event(uint8_t ev)
{
    switch (ev) {
    case EV_ENCODER_CLICK: toggle_run();          break;   /* start / pause */
    case EV_SELECT:        load_phase(!s_work);   break;   /* skip to the next phase */
    default: break;                                        /* rotation unused */
    }
}

static void pomo_render(void)
{
    lv_obj_clean(ui_frame_content());

    int rem = remaining_s();
    if (s_running && rem <= 0) {               /* phase finished → switch, paused */
        load_phase(!s_work);
        rem = remaining_s();
    }

    ui_text_row(TITLE_ROW, s_work ? "Work" : "Break");
    char t[16];
    snprintf(t, sizeof(t), "%02d:%02d", rem / SECS_PER_MIN, rem % SECS_PER_MIN);
    ui_text_row(TIME_ROW, t);
    ui_text_row(STATE_ROW, s_running ? "running" : "paused");

    /* Hint bar: click starts/pauses (label reflects the state), Select skips ahead. */
    control_hints_t h = { .click = s_running ? "PAU" : "GO", .select = "NXT" };
    ui_frame_set_hints(&h);
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
    .tick_ms  = POMO_TICK_MS,      /* live 1 Hz countdown (app-API 1.1) */
};
TASKMASTER_REGISTER_APP(pomodoro_app);
