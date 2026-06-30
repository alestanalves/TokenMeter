#pragma once

// Prefer .env for credentials. This file remains useful for touch calibration
// overrides or when you do not want to use scripts/load_env.py.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define CODEX_BRIDGE_URL ""
#define CLAUDE_BRIDGE_URL ""
#define USE_CODEX 1
#define USE_CLAUDE 1
#define USAGE_WINDOW_DAYS 7
#define CODEX_TOKEN_LIMIT 0ULL
#define CLAUDE_TOKEN_LIMIT 0ULL
#define CLAUDE_BUDGET_USD 0.0

#define DEVICE_NAME "Token Meter"
#define REFRESH_INTERVAL_MS 300000UL
#define USE_INSECURE_TLS 1

// ESP32-2432S028R / CYD touch pins.
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25

// Adjust these if touch is mirrored or offset.
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 1
#define TOUCH_INVERT_Y 0
#define TOUCH_MIN_X 250
#define TOUCH_MAX_X 3800
#define TOUCH_MIN_Y 250
#define TOUCH_MAX_Y 3800
