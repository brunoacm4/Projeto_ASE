#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access_control.h"

typedef struct {
    uint8_t bytes[10];
    size_t len;
} authorized_uid_t;

static const authorized_uid_t authorized_uids[] = {
    {{0xDE, 0xAF, 0x32, 0xF6}, 4},
    {{0x04, 0xA1, 0xB2, 0xC3}, 4},
    {{0x55, 0xEE, 0x12, 0xAA}, 4},
};

bool access_control_is_authorized(const uint8_t *uid, size_t uid_len)
{
    if (uid == NULL || uid_len == 0) {
        return false;
    }

    for (size_t i = 0; i < sizeof(authorized_uids) / sizeof(authorized_uids[0]); i++) {
        if (authorized_uids[i].len == uid_len &&
            memcmp(authorized_uids[i].bytes, uid, uid_len) == 0) {
            return true;
        }
    }

    return false;
}
