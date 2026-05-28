#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t display_ui_init(void);
void display_ui_show_boot(const char *reason);
void display_ui_show_env(float temperature, float humidity, bool blocked);
void display_ui_show_wait_rfid_pending(void);
void display_ui_show_wait_rfid(float temperature, float humidity);
void display_ui_show_access_granted(const char *uid);
void display_ui_show_access_denied(const char *uid, const char *reason);
void display_ui_show_alert(float temperature, float humidity);
void display_ui_show_sensor_error(void);
void display_ui_power_down(void);
