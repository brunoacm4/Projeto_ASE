#pragma once

#include "esp_err.h"

esp_err_t mqtt_app_start_once(void);
esp_err_t mqtt_app_publish(const char *topic, const char *payload, int qos, int retain);

