#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <lvgl.h>
#include <time.h>

#include "config.h"

extern const lv_img_dsc_t codex_logo;

static constexpr uint16_t SCREEN_WIDTH = 240;
static constexpr uint16_t SCREEN_HEIGHT = 320;
static constexpr uint8_t TFT_ROTATION = 3;
static constexpr uint16_t HTTP_TIMEOUT_MS = 15000;

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi(HSPI);

static lv_color_t drawBuffer[SCREEN_WIDTH * 20];
static lv_disp_draw_buf_t displayBuffer;

struct ProviderUi {
  lv_obj_t *card = nullptr;
  lv_obj_t *icon = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *tokens = nullptr;
  lv_obj_t *cost = nullptr;
  lv_obj_t *status = nullptr;
  lv_obj_t *percent = nullptr;
  lv_obj_t *bar = nullptr;
};

struct ProviderData {
  String id;
  String label;
  bool enabled = true;
  bool available = false;
  bool hasLimit = false;
  bool secondaryIsUsd = true;
  bool hasSecondary = true;
  String status = "--";
  String primaryText;
  String secondaryText;
  String secondaryUnit = "USD";
  uint64_t tokens = 0;
  uint64_t tokenLimit = 0;
  double secondaryValue = 0.0;
  double secondaryLimit = 0.0;
  int percentUsed = 0;
};

struct HttpResult {
  int statusCode = -1;
  String body;
  String error;
};

static ProviderUi openaiUi;
static ProviderUi anthropicUi;
static lv_obj_t *wifiLabel;
static lv_obj_t *footerLabel;
static lv_obj_t *refreshButton;

static bool refreshRequested = true;
static uint32_t lastRefreshAttempt = 0;
static uint32_t lastTick = 0;
static bool timeReady = false;

static String formatTokens(uint64_t value) {
  char buf[28];
  if (value >= 1000000000ULL) {
    snprintf(buf, sizeof(buf), "%.2fB", value / 1000000000.0);
  } else if (value >= 1000000ULL) {
    snprintf(buf, sizeof(buf), "%.2fM", value / 1000000.0);
  } else if (value >= 1000ULL) {
    snprintf(buf, sizeof(buf), "%.1fk", value / 1000.0);
  } else {
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
  }
  return String(buf);
}

static String formatUsd(double value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "US$ %.2f", value);
  return String(buf);
}

static String formatDouble(double value) {
  char buf[24];
  if (value >= 1000000.0) {
    snprintf(buf, sizeof(buf), "%.2fM", value / 1000000.0);
  } else if (value >= 1000.0) {
    snprintf(buf, sizeof(buf), "%.1fk", value / 1000.0);
  } else {
    snprintf(buf, sizeof(buf), "%.2f", value);
  }
  return String(buf);
}

static String formatSecondary(const ProviderData &data, double value) {
  if (data.secondaryIsUsd) {
    return formatUsd(value);
  }
  return formatDouble(value) + " " + data.secondaryUnit;
}

static void setFooter(const String &message) {
  lv_label_set_text(footerLabel, message.c_str());
}

static void clearTftMemory() {
  for (uint8_t rotation = 0; rotation < 4; rotation++) {
    tft.setRotation(rotation);
    tft.fillScreen(TFT_BLACK);
    delay(20);
  }
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(TFT_BLACK);
}

static void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  const uint32_t width = area->x2 - area->x1 + 1;
  const uint32_t height = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, width, height);
  tft.pushColors(reinterpret_cast<uint16_t *>(&colorP->full), width * height, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

static int32_t clampCoord(int32_t value, int32_t maxValue) {
  if (value < 0) {
    return 0;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

static uint16_t readTouchAxis(uint8_t command) {
  touchSpi.beginTransaction(SPISettings(SPI_TOUCH_FREQUENCY, MSBFIRST, SPI_MODE0));
  digitalWrite(TOUCH_CS, LOW);
  touchSpi.transfer(command);
  uint8_t high = touchSpi.transfer(0x00);
  uint8_t low = touchSpi.transfer(0x00);
  digitalWrite(TOUCH_CS, HIGH);
  touchSpi.endTransaction();

  return (static_cast<uint16_t>(high) << 8 | low) >> 3;
}

static uint16_t readTouchAxisAverage(uint8_t command) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < 4; i++) {
    total += readTouchAxis(command);
    delayMicroseconds(80);
  }
  return total / 4;
}

static void touchRead(lv_indev_drv_t *, lv_indev_data_t *data) {
  if (digitalRead(TOUCH_IRQ) != LOW) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  int32_t rawX = readTouchAxisAverage(0xD0);
  int32_t rawY = readTouchAxisAverage(0x90);

#if TOUCH_SWAP_XY
  int32_t tmp = rawX;
  rawX = rawY;
  rawY = tmp;
#endif

  int32_t x = map(rawX, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH - 1);
  int32_t y = map(rawY, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT - 1);

#if TOUCH_INVERT_X
  x = (SCREEN_WIDTH - 1) - x;
#endif
#if TOUCH_INVERT_Y
  y = (SCREEN_HEIGHT - 1) - y;
#endif

  if (TFT_ROTATION == 3) {
    x = (SCREEN_WIDTH - 1) - x;
    y = (SCREEN_HEIGHT - 1) - y;
  }

  data->point.x = clampCoord(x, SCREEN_WIDTH - 1);
  data->point.y = clampCoord(y, SCREEN_HEIGHT - 1);
  data->state = LV_INDEV_STATE_PR;
}

static void refreshClicked(lv_event_t *) {
  refreshRequested = true;
}

static void styleCard(lv_obj_t *card, uint32_t borderColor) {
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1D2225), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(borderColor), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 6, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
}

static String compactStatus(const ProviderData &data) {
  if (!data.enabled) {
    return "off";
  }
  if (data.available) {
    return "ok";
  }
  return "--";
}

static lv_obj_t *createClaudeIcon(lv_obj_t *parent, uint32_t color) {
  lv_obj_t *icon = lv_obj_create(parent);
  lv_obj_set_size(icon, 28, 28);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(icon, 0, 0);
  lv_obj_set_style_pad_all(icon, 0, 0);

  const lv_color_t orange = lv_color_hex(color);
  const lv_color_t eye = lv_color_hex(0x121619);

  lv_obj_t *body = lv_obj_create(icon);
  lv_obj_set_size(body, 22, 16);
  lv_obj_set_pos(body, 3, 6);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(body, orange, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_radius(body, 2, 0);
  lv_obj_set_style_pad_all(body, 0, 0);

  lv_obj_t *leftArm = lv_obj_create(icon);
  lv_obj_set_size(leftArm, 5, 9);
  lv_obj_set_pos(leftArm, 0, 11);
  lv_obj_clear_flag(leftArm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(leftArm, orange, 0);
  lv_obj_set_style_bg_opa(leftArm, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(leftArm, 0, 0);
  lv_obj_set_style_radius(leftArm, 1, 0);

  lv_obj_t *rightArm = lv_obj_create(icon);
  lv_obj_set_size(rightArm, 5, 9);
  lv_obj_set_pos(rightArm, 23, 11);
  lv_obj_clear_flag(rightArm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(rightArm, orange, 0);
  lv_obj_set_style_bg_opa(rightArm, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rightArm, 0, 0);
  lv_obj_set_style_radius(rightArm, 1, 0);

  for (uint8_t i = 0; i < 4; i++) {
    lv_obj_t *leg = lv_obj_create(icon);
    lv_obj_set_size(leg, 4, 5);
    lv_obj_set_pos(leg, 5 + i * 5, 21);
    lv_obj_clear_flag(leg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(leg, orange, 0);
    lv_obj_set_style_bg_opa(leg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(leg, 0, 0);
    lv_obj_set_style_radius(leg, 1, 0);
  }

  lv_obj_t *leftEye = lv_obj_create(icon);
  lv_obj_set_size(leftEye, 3, 6);
  lv_obj_set_pos(leftEye, 9, 10);
  lv_obj_clear_flag(leftEye, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(leftEye, eye, 0);
  lv_obj_set_style_bg_opa(leftEye, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(leftEye, 0, 0);
  lv_obj_set_style_radius(leftEye, 1, 0);

  lv_obj_t *rightEye = lv_obj_create(icon);
  lv_obj_set_size(rightEye, 3, 6);
  lv_obj_set_pos(rightEye, 18, 10);
  lv_obj_clear_flag(rightEye, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(rightEye, eye, 0);
  lv_obj_set_style_bg_opa(rightEye, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rightEye, 0, 0);
  lv_obj_set_style_radius(rightEye, 1, 0);

  return icon;
}

static void createProviderCard(
    lv_obj_t *parent,
    ProviderUi &ui,
    int16_t y,
    const char *title,
    const lv_img_dsc_t *logo,
    uint32_t accent) {
  ui.card = lv_obj_create(parent);
  lv_obj_set_size(ui.card, 224, 112);
  lv_obj_set_pos(ui.card, 8, y);
  styleCard(ui.card, accent);

  if (logo) {
    ui.icon = lv_img_create(ui.card);
    lv_img_set_src(ui.icon, logo);
    lv_obj_set_size(ui.icon, 28, 28);
  } else {
    ui.icon = createClaudeIcon(ui.card, accent);
  }
  lv_obj_align(ui.icon, LV_ALIGN_TOP_LEFT, 0, 0);

  ui.title = lv_label_create(ui.card);
  lv_label_set_text(ui.title, title);
  lv_obj_set_style_text_color(ui.title, lv_color_hex(0xEAF0EE), 0);
  lv_obj_set_style_text_font(ui.title, &lv_font_montserrat_14, 0);
  lv_label_set_long_mode(ui.title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(ui.title, 108);
  lv_obj_align(ui.title, LV_ALIGN_TOP_LEFT, 36, 0);

  ui.status = lv_label_create(ui.card);
  lv_label_set_text(ui.status, "--");
  lv_obj_set_style_text_color(ui.status, lv_color_hex(0x9CA8A4), 0);
  lv_obj_set_style_text_font(ui.status, &lv_font_montserrat_12, 0);
  lv_label_set_long_mode(ui.status, LV_LABEL_LONG_DOT);
  lv_obj_set_width(ui.status, 44);
  lv_obj_set_style_text_align(ui.status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(ui.status, LV_ALIGN_TOP_RIGHT, 0, 1);

  ui.tokens = lv_label_create(ui.card);
  lv_label_set_text(ui.tokens, "--");
  lv_obj_set_style_text_color(ui.tokens, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(ui.tokens, &lv_font_montserrat_24, 0);
  lv_obj_align(ui.tokens, LV_ALIGN_TOP_LEFT, 0, 36);

  ui.cost = lv_label_create(ui.card);
  lv_label_set_text(ui.cost, "sem dados");
  lv_obj_set_style_text_color(ui.cost, lv_color_hex(0xBBC6C2), 0);
  lv_obj_set_style_text_font(ui.cost, &lv_font_montserrat_12, 0);
  lv_obj_set_width(ui.cost, 150);
  lv_label_set_long_mode(ui.cost, LV_LABEL_LONG_DOT);
  lv_obj_align(ui.cost, LV_ALIGN_BOTTOM_LEFT, 0, -18);

  ui.percent = lv_label_create(ui.card);
  lv_label_set_text(ui.percent, "--");
  lv_obj_set_style_text_color(ui.percent, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(ui.percent, &lv_font_montserrat_16, 0);
  lv_obj_align(ui.percent, LV_ALIGN_BOTTOM_RIGHT, 0, -14);

  ui.bar = lv_bar_create(ui.card);
  lv_obj_set_size(ui.bar, 204, 9);
  lv_obj_align(ui.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_bar_set_range(ui.bar, 0, 100);
  lv_bar_set_value(ui.bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ui.bar, lv_color_hex(0x30383C), 0);
  lv_obj_set_style_bg_color(ui.bar, lv_color_hex(accent), LV_PART_INDICATOR);
}

static void updateProviderUi(ProviderUi &ui, const ProviderData &data) {
  lv_label_set_text(ui.title, data.label.c_str());
  const String statusText = compactStatus(data);
  lv_label_set_text(ui.status, statusText.c_str());

  if (!data.enabled) {
    lv_label_set_text(ui.tokens, "off");
    lv_label_set_text(ui.cost, "desativado");
    lv_label_set_text(ui.percent, "--");
    lv_bar_set_value(ui.bar, 0, LV_ANIM_OFF);
    return;
  }

  if (!data.available) {
    lv_label_set_text(ui.tokens, "--");
    lv_label_set_text(ui.cost, "sem dados");
    lv_label_set_text(ui.percent, "--");
    lv_bar_set_value(ui.bar, 0, LV_ANIM_OFF);
    return;
  }

  if (data.primaryText.length() > 0) {
    lv_label_set_text(ui.tokens, data.primaryText.c_str());
  } else {
    lv_label_set_text(ui.tokens, formatTokens(data.tokens).c_str());
  }

  String rightText;
  if (data.secondaryText.length() > 0) {
    rightText = data.secondaryText;
  } else if (data.hasSecondary) {
    rightText = formatSecondary(data, data.secondaryValue);
  } else {
    rightText = "sem limite";
  }

  if (data.secondaryLimit > 0.0) {
    rightText += " / ";
    rightText += formatSecondary(data, data.secondaryLimit);
  } else if (data.tokenLimit > 0) {
    rightText = formatTokens(data.tokens) + " / " + formatTokens(data.tokenLimit);
  }
  lv_label_set_text(ui.cost, rightText.c_str());
  lv_bar_set_value(ui.bar, constrain(data.percentUsed, 0, 100), LV_ANIM_OFF);
  String percentText = String(constrain(data.percentUsed, 0, 100)) + "%";
  lv_label_set_text(ui.percent, percentText.c_str());
}

static void createUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x121619), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, DEVICE_NAME);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF5F8F7), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 7);

  wifiLabel = lv_label_create(screen);
  lv_label_set_text(wifiLabel, "WiFi --");
  lv_obj_set_style_text_color(wifiLabel, lv_color_hex(0xAEB8B4), 0);
  lv_obj_set_style_text_font(wifiLabel, &lv_font_montserrat_12, 0);
  lv_obj_align(wifiLabel, LV_ALIGN_TOP_LEFT, 10, 27);

  refreshButton = lv_btn_create(screen);
  lv_obj_set_size(refreshButton, 32, 28);
  lv_obj_align(refreshButton, LV_ALIGN_TOP_RIGHT, -8, 7);
  lv_obj_set_style_radius(refreshButton, 6, 0);
  lv_obj_set_style_bg_color(refreshButton, lv_color_hex(0x27C3A3), 0);
  lv_obj_add_event_cb(refreshButton, refreshClicked, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *refreshText = lv_label_create(refreshButton);
  lv_label_set_text(refreshText, LV_SYMBOL_REFRESH);
  lv_obj_center(refreshText);

  createProviderCard(screen, openaiUi, 50, "Codex", &codex_logo, 0x27C3A3);
  createProviderCard(screen, anthropicUi, 176, "Claude Code", nullptr, 0xF6B73C);

  footerLabel = lv_label_create(screen);
  lv_label_set_long_mode(footerLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(footerLabel, 220);
  lv_label_set_text(footerLabel, "iniciando");
  lv_obj_set_style_text_color(footerLabel, lv_color_hex(0x8E9995), 0);
  lv_obj_set_style_text_font(footerLabel, &lv_font_montserrat_12, 0);
  lv_obj_align(footerLabel, LV_ALIGN_BOTTOM_LEFT, 10, -6);
}

static bool connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lv_label_set_text(wifiLabel, "WiFi conectando");
  setFooter("conectando WiFi");

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 25000UL) {
    lv_timer_handler();
    delay(25);
  }

  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(wifiLabel, "WiFi offline");
    setFooter("falha no WiFi");
    return false;
  }

  const String ipText = "WiFi " + WiFi.localIP().toString();
  lv_label_set_text(wifiLabel, ipText.c_str());
  return true;
}

static bool ensureTime() {
  if (timeReady) {
    return true;
  }

  setFooter("sincronizando horario");
  configTime(TIMEZONE_OFFSET_SECONDS, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  const uint32_t start = millis();
  while (millis() - start < 20000UL) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      timeReady = true;
      return true;
    }
    lv_timer_handler();
    delay(100);
  }

  setFooter("NTP falhou");
  return false;
}

static HttpResult httpGet(const String &url) {
  HttpResult result;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    result.error = "begin falhou";
    return result;
  }

  http.addHeader("User-Agent", "ESP32TokenMeter/0.1");
  http.addHeader("Accept", "application/json");
  result.statusCode = http.GET();
  if (result.statusCode > 0) {
    result.body = http.getString();
  } else {
    result.error = http.errorToString(result.statusCode);
  }
  http.end();

  return result;
}

static uint64_t jsonNumber(JsonVariantConst value) {
  if (value.isNull()) {
    return 0;
  }
  if (value.is<const char *>()) {
    return static_cast<uint64_t>(strtoull(value.as<const char *>(), nullptr, 10));
  }
  return value.as<uint64_t>();
}

static double jsonDouble(JsonVariantConst value) {
  if (value.isNull()) {
    return 0.0;
  }
  if (value.is<const char *>()) {
    return atof(value.as<const char *>());
  }
  return value.as<double>();
}

static JsonArrayConst responseData(JsonDocument &doc) {
  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (!data.isNull()) {
    return data;
  }

  JsonArrayConst results = doc["results"].as<JsonArrayConst>();
  if (!results.isNull()) {
    return results;
  }

  return JsonArrayConst();
}

static int percentFor(uint64_t tokens, uint64_t tokenLimit, double secondaryValue, double secondaryLimit) {
  if (secondaryLimit > 0.0) {
    return static_cast<int>(round((secondaryValue / secondaryLimit) * 100.0));
  }
  if (tokenLimit > 0) {
    return static_cast<int>(round((static_cast<double>(tokens) / static_cast<double>(tokenLimit)) * 100.0));
  }
  return 0;
}

static bool parseJsonPayload(const HttpResult &http, JsonDocument &doc, String &statusText) {
  if (http.statusCode != HTTP_CODE_OK) {
    statusText = "HTTP " + String(http.statusCode);
    if (http.error.length() > 0) {
      statusText = http.error;
    }
    return false;
  }
  DeserializationError error = deserializeJson(doc, http.body);
  if (error) {
    statusText = "JSON erro";
    return false;
  }
  return true;
}

static JsonObjectConst findProviderObject(JsonDocument &doc, const char *providerId) {
  if (String(doc["id"] | "") == providerId) {
    return doc.as<JsonObjectConst>();
  }

  JsonArrayConst providers = doc["providers"].as<JsonArrayConst>();
  for (JsonObjectConst provider : providers) {
    if (String(provider["id"] | "") == providerId) {
      return provider;
    }
  }

  return JsonObjectConst();
}

static ProviderData fetchBridgeProvider(
    const char *providerId,
    const char *label,
    const char *url,
    bool enabled,
    uint64_t tokenLimit,
    const String &fallbackSecondaryText) {
  ProviderData data;
  data.id = providerId;
  data.label = label;
  data.enabled = enabled;
  data.secondaryIsUsd = false;
  data.hasSecondary = false;
  data.secondaryText = fallbackSecondaryText;
  data.tokenLimit = tokenLimit;

  if (!data.enabled) {
    data.status = "off";
    return data;
  }

  if (String(url).length() == 0) {
    data.status = "sem URL";
    return data;
  }

  JsonDocument doc;
  String statusText;
  HttpResult http = httpGet(String(url));
  if (!parseJsonPayload(http, doc, statusText)) {
    data.status = statusText;
    return data;
  }

  JsonObjectConst provider = findProviderObject(doc, providerId);
  if (provider.isNull()) {
    data.status = "JSON sem dados";
    return data;
  }

  data.available = provider["available"] | false;
  data.status = provider["status"] | "local";
  data.tokens = jsonNumber(provider["tokens_used"]);
  if (data.tokens == 0) {
    data.tokens = jsonNumber(provider["total_tokens_all"]);
  }
  data.primaryText = provider["display_value"] | "";
  const uint64_t totalTokensAll = jsonNumber(provider["total_tokens_all"]);

  uint64_t bridgeLimit = jsonNumber(provider["limit_tokens"]);
  if (data.tokenLimit == 0 && bridgeLimit > 0) {
    data.tokenLimit = bridgeLimit;
  }

  const String manualLabel = provider["limit_label"] | "";
  if (manualLabel.length() > 0) {
    data.secondaryText = manualLabel;
  }

  const String windowLabel = provider["window_label"] | "local";
  if (manualLabel.length() == 0) {
    data.secondaryText = windowLabel;
  }

  const int bridgePercent = provider["percent_used"] | 0;
  if (bridgePercent > 0) {
    data.percentUsed = bridgePercent;
  }

  data.hasLimit = data.tokenLimit > 0;
  if (data.percentUsed == 0) {
    data.percentUsed = percentFor(data.tokens, data.tokenLimit, data.secondaryValue, data.secondaryLimit);
  }
  if (data.percentUsed == 0 && data.tokenLimit == 0 && totalTokensAll > 0) {
    data.percentUsed = static_cast<int>(round((static_cast<double>(data.tokens) / totalTokensAll) * 100.0));
  }
  return data;
}

static void fetchAndRender() {
  if (!connectWifi()) {
    return;
  }

  setFooter("Codex");
  ProviderData codex = fetchBridgeProvider(
      "codex",
      "Codex",
      CODEX_BRIDGE_URL,
      USE_CODEX,
      CODEX_TOKEN_LIMIT,
      "bridge local");
  updateProviderUi(openaiUi, codex);
  lv_timer_handler();

  setFooter("Claude");
  ProviderData claude = fetchBridgeProvider(
      "claude",
      "Claude Code",
      CLAUDE_BRIDGE_URL,
      USE_CLAUDE,
      CLAUDE_TOKEN_LIMIT,
      "bridge local");
  updateProviderUi(anthropicUi, claude);

  String footer = "Codex ";
  footer += compactStatus(codex);
  footer += " | Claude ";
  footer += compactStatus(claude);
  setFooter(footer);
}

void setup() {
  Serial.begin(115200);
  delay(200);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif

  tft.begin();
  clearTftMemory();

  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_IRQ, INPUT);
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);

  lv_init();
  lv_disp_draw_buf_init(&displayBuffer, drawBuffer, nullptr, SCREEN_WIDTH * 20);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_WIDTH;
  dispDrv.ver_res = SCREEN_HEIGHT;
  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &displayBuffer;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t touchDrv;
  lv_indev_drv_init(&touchDrv);
  touchDrv.type = LV_INDEV_TYPE_POINTER;
  touchDrv.read_cb = touchRead;
  lv_indev_drv_register(&touchDrv);

  createUi();
  lv_obj_invalidate(lv_scr_act());
  lv_timer_handler();
  lastTick = millis();
}

void loop() {
  const uint32_t now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();

  if (refreshRequested || now - lastRefreshAttempt >= REFRESH_INTERVAL_MS) {
    refreshRequested = false;
    lastRefreshAttempt = now;
    fetchAndRender();
  }

  delay(5);
}
