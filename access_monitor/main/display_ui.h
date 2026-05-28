#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t display_ui_init(void);
void display_ui_show_boot(const char *reason);
void display_ui_show_env(float temperature, float humidity, bool blocked, size_t occupancy);
void display_ui_show_presence(size_t occupancy, const char *const *card_ids, size_t card_count);
void display_ui_show_wait_rfid_pending(void);
void display_ui_show_wait_rfid(float temperature, float humidity);
void display_ui_show_access_granted(const char *uid);
void display_ui_show_access_denied(const char *uid, const char *reason);
void display_ui_show_timeout(void);
void display_ui_show_alert(float temperature, float humidity);
void display_ui_show_sensor_error(void);
void display_ui_power_down(void);
