# app_pomodoro — a self-registering app component.
# WHOLE_ARCHIVE keeps the registration constructor from being dropped by the linker.
# esp_timer is needed for esp_timer_get_time() (the countdown timestamp).
idf_component_register(
    SRCS "app_pomodoro.c"
    REQUIRES taskmaster_core esp_timer
    WHOLE_ARCHIVE
)
