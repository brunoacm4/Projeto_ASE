#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define RFID_UID_MAX_LEN 10
#define RFID_UID_STR_LEN (RFID_UID_MAX_LEN * 2 + 1)

typedef struct {
    uint8_t bytes[RFID_UID_MAX_LEN];
    size_t len;
} rfid_uid_t;

esp_err_t rfid_init(void);
esp_err_t rfid_read_uid(rfid_uid_t *uid);
void rfid_uid_to_string(const rfid_uid_t *uid, char *out, size_t out_size);

