#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "presence.h"

RTC_DATA_ATTR static presence_entry_t s_present[PROJECT_MAX_PRESENT_CARDS];
RTC_DATA_ATTR static size_t s_present_count;

static int find_card(const char *card_id)
{
    if (card_id == NULL || card_id[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < s_present_count; i++) {
        if (strncmp(s_present[i].card_id, card_id, PRESENCE_CARD_ID_MAX_LEN) == 0) {
            return (int)i;
        }
    }
    return -1;
}

size_t presence_count(void)
{
    if (s_present_count > PROJECT_MAX_PRESENT_CARDS) {
        s_present_count = 0;
    }
    return s_present_count;
}

bool presence_contains(const char *card_id)
{
    return find_card(card_id) >= 0;
}

presence_action_t presence_toggle(const char *card_id, int64_t millis)
{
    int index = find_card(card_id);
    if (index >= 0) {
        size_t pos = (size_t)index;
        for (size_t i = pos; i + 1 < s_present_count; i++) {
            s_present[i] = s_present[i + 1];
        }
        if (s_present_count > 0) {
            s_present_count--;
        }
        return PRESENCE_ACTION_EXITED;
    }

    if (s_present_count >= PROJECT_MAX_PRESENT_CARDS) {
        return PRESENCE_ACTION_FULL;
    }

    strlcpy(s_present[s_present_count].card_id, card_id, sizeof(s_present[s_present_count].card_id));
    s_present[s_present_count].entry_millis = millis;
    s_present_count++;
    return PRESENCE_ACTION_ENTERED;
}

size_t presence_snapshot(presence_entry_t *entries, size_t max_entries)
{
    size_t count = presence_count();
    if (entries == NULL || max_entries == 0) {
        return count;
    }

    size_t copy_count = count < max_entries ? count : max_entries;
    for (size_t i = 0; i < copy_count; i++) {
        entries[i] = s_present[i];
    }
    return count;
}

void presence_clear(void)
{
    memset(s_present, 0, sizeof(s_present));
    s_present_count = 0;
}
