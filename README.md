# TM-Pomodoro — a Pomodoro timer for TaskMaster-C3

A complete, graphical example **app** for a [TaskMaster-C3](https://github.com/EyalYe/TaskMaster)
device: a **full-screen** work/break timer drawn as two circles that **shrink** as the
time runs down. It's a real, self-contained app that pins the OS (`taskmaster_core`) as
a **sealed dependency** and never touches it — the same pattern any app follows.

- **Rotate encoder** — move focus between the **Work** and **Break** circles
- **Encoder click** — edit the focused circle's length (rotate to change, click to save);
  while running, click **pauses / resumes**
- **Select** — start the cycle (Work, then Break auto-follows)
- **Long-press Select** — reset
- **Work / Break lengths** — also knob-editable in **Settings → Pomodoro**

The app lives in [`apps/app_pomodoro/`](apps/app_pomodoro/app_pomodoro.c). It draws with
raw LVGL (no hint bar — it owns the whole screen) and uses two app-API additions it
drove: **`tick_ms`** (1.1) for the live shrink and **`EV_SELECT_LONG`** (1.2) for the
reset. So it pins core to `v1.2.0` in [`apps.yaml`](apps.yaml) and declares
`TASKMASTER_REQUIRE_API(1, 2)`.

## Build, flash, update

Same as any TaskMaster-C3 app — no core to compile, nothing hosted:

- **CI / local build** produces two files: **`firmware-merged.bin`** (first USB flash) and
  **`taskmaster_c3_app.bin`** (OTA). Use the right one:
- **First flash (USB):** `python tools/flash.py --bin firmware-merged.bin` (or bare
  `python tools/flash.py` for a local build).
- **Update over your LAN (OTA):** `python tools/ota_serve.py --bin taskmaster_c3_app.bin` (or
  bare for a local build), then point the device's `fw_url` at the URL it prints
  (Settings → Web config) → Settings → Check update.

## Make your own app

Copy `apps/app_pomodoro/` to a new folder, rename it, add an entry in `apps.yaml`, and
write your `device_app_t`. The full contract is in [`docs/APP_API.md`](docs/APP_API.md);
agent/contributor context is in [`docs/agents/`](docs/agents/). You edit only `apps.yaml`
and your app folder — never the core.
