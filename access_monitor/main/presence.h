#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "project_config.h"

#define PRESENCE_CARD_ID_MAX_LEN 21

typedef struct {
    char card_id[PRESENCE_CARD_ID_MAX_LEN];
    int64_t entry_millis;
} presence_entry_t;

typedef enum {
    PRESENCE_ACTION_ENTERED,
    PRESENCE_ACTION_EXITED,
    PRESENCE_ACTION_FULL,
} presence_action_t;

size_t presence_count(void);
bool presence_contains(const char *card_id);
presence_action_t presence_toggle(const char *card_id, int64_t millis);
size_t presence_snapshot(presence_entry_t *entries, size_t max_entries);
void presence_clear(void);
