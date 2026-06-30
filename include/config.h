#pragma once

// Most settings come from .env through scripts/load_env.py. You can still
// override any define from include/config.local.h if you prefer.
#if __has_include("config.local.h")
#include "config.local.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef CODEX_BRIDGE_URL
#define CODEX_BRIDGE_URL ""
#endif

#ifndef CLAUDE_BRIDGE_URL
#define CLAUDE_BRIDGE_URL ""
#endif

#ifndef USE_CODEX
#define USE_CODEX 1
#endif

#ifndef USE_CLAUDE
#define USE_CLAUDE 1
#endif

#ifndef CODEX_TOKEN_LIMIT
#define CODEX_TOKEN_LIMIT 0ULL
#endif

#ifndef CLAUDE_TOKEN_LIMIT
#define CLAUDE_TOKEN_LIMIT 0ULL
#endif

#ifndef CLAUDE_BUDGET_USD
#define CLAUDE_BUDGET_USD 0.0
#endif

#ifndef USAGE_WINDOW_DAYS
#define USAGE_WINDOW_DAYS 7
#endif

#ifndef DEVICE_NAME
#define DEVICE_NAME "Token Meter"
#endif

#ifndef REFRESH_INTERVAL_MS
#define REFRESH_INTERVAL_MS 300000UL
#endif

#ifndef USE_INSECURE_TLS
#define USE_INSECURE_TLS 1
#endif

#ifndef TIMEZONE_OFFSET_SECONDS
#define TIMEZONE_OFFSET_SECONDS 0
#endif

// ESP32-2432S028R / CYD touch pins.
#ifndef TOUCH_CS
#define TOUCH_CS 33
#endif
#ifndef TOUCH_IRQ
#define TOUCH_IRQ 36
#endif
#ifndef TOUCH_MOSI
#define TOUCH_MOSI 32
#endif
#ifndef TOUCH_MISO
#define TOUCH_MISO 39
#endif
#ifndef TOUCH_CLK
#define TOUCH_CLK 25
#endif

// Touch calibration after optional XY swap.
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 1
#endif
#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X 1
#endif
#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y 0
#endif
#ifndef TOUCH_MIN_X
#define TOUCH_MIN_X 250
#endif
#ifndef TOUCH_MAX_X
#define TOUCH_MAX_X 3800
#endif
#ifndef TOUCH_MIN_Y
#define TOUCH_MIN_Y 250
#endif
#ifndef TOUCH_MAX_Y
#define TOUCH_MAX_Y 3800
#endif
