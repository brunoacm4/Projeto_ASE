#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t storage_log_mount(void);
void storage_log_unmount(void);
esp_err_t storage_log_append(const char *wakeup,
                             const char *event,
                             float temperature,
                             float humidity,
                             const char *card_uid,
                             const char *result,
                             bool access_blocked);
esp_err_t storage_log_append_access(const char *event,
                                    const char *card_id,
                                    const char *result,
                                    float temperature,
                                    float humidity,
                                    bool access_blocked);
esp_err_t storage_log_write_presence_snapshot(void);
