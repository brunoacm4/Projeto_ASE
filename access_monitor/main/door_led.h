#pragma once

#include "esp_err.h"

esp_err_t door_led_init(void);
void door_led_open(void);
void door_led_denied(void);
void door_led_off(void);
