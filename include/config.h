#pragma once

// Set this to the API host and port reachable from the Yun Shield.
#define API_BASE_URL "http://192.168.1.100:3000"
#define PROFILES_PATH "/profiles"
#define API_TOKEN ""
#define API_TIMEOUT_SECONDS 8

// The API returns profiles with id, name, min_cm, and max_cm fields.
// min_cm is the near-wall threshold; max_cm is the standing threshold.
#define PROFILE_LIMIT 12
#define PROFILE_THRESHOLD_MARGIN_CM 3.0f
#define OLED_I2C_ADDRESS 0x3C