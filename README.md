# TM-Pomodoro — a Pomodoro timer for TaskMaster-C3

A complete example **app** for a [TaskMaster-C3](https://github.com/EyalYe/TaskMaster)
device (1.3" OLED + rotary encoder + two buttons): a work/break timer with a live
countdown. It's a real, self-contained app that pins the OS (`taskmaster_core`) as a
**sealed dependency** and never touches it — the same pattern any app follows.

- **Encoder push** — start / pause
- **Select** — skip to the next phase (Work ↔ Break)
- **Work / Break lengths** — knob-editable in **Settings → Pomodoro**

The app lives in [`apps/app_pomodoro/`](apps/app_pomodoro/app_pomodoro.c). It uses the
**app-API 1.1** `tick_ms` field for its 1 Hz countdown, so it pins core to `v1.1.0`
in [`apps.yaml`](apps.yaml) and declares `TASKMASTER_REQUIRE_API(1, 1)`.

## Build, flash, update

Same as any TaskMaster-C3 app — no core to compile, nothing hosted:

- **CI:** push → GitHub Actions builds a `.bin` (`.github/workflows/build.yml`).
- **Local build:** `idf.py set-target esp32c3 && idf.py build` (ESP-IDF v6.0.1).
- **Flash (USB, esptool only):** `python tools/flash.py`
- **Update over your LAN:** `python tools/ota_serve.py`, then point the device's `fw_url`
  at it (Settings → Web config) → Settings → Check update.

## Make your own app

Copy `apps/app_pomodoro/` to a new folder, rename it, add an entry in `apps.yaml`, and
write your `device_app_t`. The full contract is in [`docs/APP_API.md`](docs/APP_API.md);
agent/contributor context is in [`docs/agents/`](docs/agents/). You edit only `apps.yaml`
and your app folder — never the core.
