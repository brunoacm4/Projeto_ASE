#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool access_control_is_authorized(const uint8_t *uid, size_t uid_len);
