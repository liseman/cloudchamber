#include <Arduino.h>

#include <algorithm>

#include "board_config.h"
#include "crowpanel_display.h"
#include "secrets.h"

#include <Adafruit_SHT4x.h>
#include <ArduinoHttpClient.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Preferences.h>
#include <lvgl.h>
#include <WiFi.h>
#include <Wire.h>

namespace {

constexpr const char* kFeedNames[] = {
    "hot", "mid", "cold", "air-temp", "air-humidity", "light"};
constexpr int kNumFeeds = sizeof(kFeedNames) / sizeof(kFeedNames[0]);

constexpr unsigned long kReadIntervalMs = 2000;
constexpr unsigned long kRelayIntervalMs = 1000;
constexpr unsigned long kSendIntervalMs = 60000;
constexpr unsigned long kWifiRetryMs = 15000;
constexpr float kTargetHysteresisC = 2.0f;

constexpr uint16_t kColorBackground = 0x18E3;
constexpr uint16_t kColorPanel = 0xFFFF;
constexpr uint16_t kColorInk = 0x2124;
constexpr uint16_t kColorMuted = 0x6B6D;
constexpr uint16_t kColorLine = 0xCE59;
constexpr uint16_t kColorAccent = 0x23AE;
constexpr uint16_t kColorGreen = 0x2DEB;
constexpr uint16_t kColorYellow = 0xF6E7;
constexpr uint16_t kColorRed = 0xD1A6;
constexpr uint16_t kColorButton = 0xE69A;

constexpr uint32_t kUiBgHex = 0xEEF3F4;
constexpr uint32_t kUiCardHex = 0xFFFFFF;
constexpr uint32_t kUiInkHex = 0x16303A;
constexpr uint32_t kUiMutedHex = 0x4D6772;
constexpr uint32_t kUiLineHex = 0xC8D6DC;
constexpr uint32_t kUiAccentHex = 0x1F718B;

struct AppSettings {
  String wifiSsid;
  String wifiPass;
  String aioUser;
  String aioKey;
  bool aioEnabled = true;
  float inletTargetC = 0.0f;
  float outletTargetC = -20.0f;
};

struct SensorState {
  float inletC = NAN;
  float middleC = NAN;
  float outletC = NAN;
  float airC = NAN;
  float humidity = NAN;
  int lightRaw = 0;
  int ds18Count = 0;
  bool probeValid = false;
  bool airValid = false;
};

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

enum class ScreenId { Dashboard, WifiMenu, LoggingMenu, TargetMenu, TextEditor };
enum class TargetField { Inlet, Outlet };
enum class TextField { WifiSsid, WifiPass, AioUser, AioKey };
enum class StatusState { Good, Warning, Error };
enum class TextAlign { Left, Center, Right };

CrowPanelDisplay display;
OneWire oneWireBus(BoardConfig::kOneWirePin);
DallasTemperature tempSensors(&oneWireBus);
Adafruit_SHT4x sht4;
Preferences preferences;
WiFiClient wifiClient;
HttpClient httpClient(wifiClient, "io.adafruit.com", 80);

AppSettings settings;
SensorState sensors;

ScreenId currentScreen = ScreenId::Dashboard;
TargetField activeTarget = TargetField::Inlet;
TextField activeTextField = TextField::WifiSsid;
String editorValue;
bool editorShift = false;
bool uiDirty = true;
bool dashboardFrameDrawn = false;

bool relayHotState = false;
bool relayColdState = false;
bool wifiConnectInFlight = false;
bool wifiAttemptFailed = false;
bool firstAioSendDone = false;
bool lastSendSuccess = false;
bool serialConnected = false;

unsigned long lastReadMs = 0;
unsigned long lastRelayMs = 0;
unsigned long lastSendMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastSerialCheckMs = 0;

StatusState wifiState = StatusState::Warning;
StatusState loggingState = StatusState::Warning;
StatusState lastDrawnWifiState = StatusState::Error;
StatusState lastDrawnLoggingState = StatusState::Error;

Rect wifiBadgeRect = {260, 18, 190, 44};
Rect loggingBadgeRect = {30, 18, 190, 44};
Rect inletTargetRect = {118, 134, 94, 24};
Rect outletTargetRect = {130, 244, 110, 24};

constexpr uint16_t kLvglBufferLines = 24;
lv_color_t lvglBuffer[480 * kLvglBufferLines];
lv_disp_draw_buf_t lvglDrawBuffer;
lv_disp_drv_t lvglDisplayDriver;
unsigned long lastLvglTickMs = 0;
bool lvglReady = false;
bool dashboardUiBuilt = false;
bool dashboardScreenLoaded = false;

lv_obj_t* dashboardScreen = nullptr;
lv_obj_t* loggingLabel = nullptr;
lv_obj_t* wifiLabel = nullptr;
lv_obj_t* airTempValueLabel = nullptr;
lv_obj_t* airHumidityValueLabel = nullptr;
lv_obj_t* inletTargetLabel = nullptr;
lv_obj_t* inletValueLabel = nullptr;
lv_obj_t* inletRelayLabel = nullptr;
lv_obj_t* middleValueLabel = nullptr;
lv_obj_t* lightValueLabel = nullptr;
lv_obj_t* outletTargetLabel = nullptr;
lv_obj_t* outletValueLabel = nullptr;
lv_obj_t* outletRelayLabel = nullptr;
lv_obj_t* dashboardCard = nullptr;

bool isValidProbeTemp(float value) {
  return value != DEVICE_DISCONNECTED_C && value > -80.0f && value < 125.0f;
}

void setRelayHot(bool on) {
  relayHotState = on;
  digitalWrite(BoardConfig::kRelayHotPin,
               on == BoardConfig::kRelayActiveHigh ? HIGH : LOW);
}

void setRelayCold(bool on) {
  relayColdState = on;
  digitalWrite(BoardConfig::kRelayColdPin,
               on == BoardConfig::kRelayActiveHigh ? HIGH : LOW);
}

String truncateMiddle(const String& value, size_t maxLen) {
  if (value.length() <= maxLen) {
    return value;
  }
  if (maxLen < 5) {
    return value.substring(0, maxLen);
  }
  const size_t left = (maxLen - 3) / 2;
  const size_t right = maxLen - 3 - left;
  return value.substring(0, left) + "..." +
         value.substring(value.length() - right);
}

String formatTemp(float value) {
  if (isnan(value)) {
    return "--.- C";
  }
  return String(value, 1) + " C";
}

String formatProbeValue(float value, int probeIndex) {
  if (sensors.ds18Count <= probeIndex) {
    return "missing";
  }
  if (!isValidProbeTemp(value)) {
    return "read err";
  }
  return formatTemp(value);
}

String formatHumidity(float value) {
  if (isnan(value)) {
    return "--.- %RH";
  }
  return String(value, 1) + " %RH";
}

int16_t averageInt(int a, int b) {
  return static_cast<int16_t>((a + b) / 2);
}

String fitTextToWidth(const String& text, int16_t maxWidth, int textSize) {
  display.setTextSize(textSize);
  if (display.textWidth(text) <= maxWidth) {
    return text;
  }

  String fitted = text;
  while (fitted.length() > 1 && display.textWidth(fitted + "...") > maxWidth) {
    fitted.remove(fitted.length() - 1);
  }
  return fitted + "...";
}

void drawFittedText(const Rect& rect, const String& text, uint16_t ink, uint16_t paper,
                    int preferredSize = 2, int minimumSize = 1,
                    TextAlign align = TextAlign::Left) {
  int chosenSize = preferredSize;
  String fitted = text;

  while (chosenSize >= minimumSize) {
    fitted = fitTextToWidth(text, rect.w, chosenSize);
    display.setTextSize(chosenSize);
    if (display.textWidth(fitted) <= rect.w) {
      break;
    }
    --chosenSize;
  }

  display.setTextSize(chosenSize);
  display.setTextFont(2);
  display.setTextColor(ink, paper);

  int16_t x = rect.x;
  const int16_t width = display.textWidth(fitted);
  if (align == TextAlign::Center) {
    x = rect.x + (rect.w - width) / 2;
  } else if (align == TextAlign::Right) {
    x = rect.x + rect.w - width;
  }

  int16_t y = rect.y;
  const int16_t lineHeight = display.fontHeight();
  if (rect.h > lineHeight) {
    y = rect.y + (rect.h - lineHeight) / 2;
  }

  display.setCursor(x, y);
  display.print(fitted);
}

bool pointInRect(int16_t x, int16_t y, const Rect& rect) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y &&
         y < rect.y + rect.h;
}

uint16_t statusColor(StatusState state) {
  switch (state) {
    case StatusState::Good:
      return kColorGreen;
    case StatusState::Warning:
      return kColorYellow;
    case StatusState::Error:
    default:
      return kColorRed;
  }
}

lv_color_t statusColorLv(StatusState state) {
  switch (state) {
    case StatusState::Good:
      return lv_color_hex(0x2FA84F);
    case StatusState::Warning:
      return lv_color_hex(0xD7A300);
    case StatusState::Error:
    default:
      return lv_color_hex(0xC53A32);
  }
}

void styleFlatButton(lv_obj_t* button, lv_color_t bg, lv_color_t border) {
  lv_obj_remove_style_all(button);
  lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, border, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 18, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_mix(bg, lv_color_white(), LV_OPA_20),
                            LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(button, border, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(button, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
}

void lvglFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  const int32_t width = area->x2 - area->x1 + 1;
  const int32_t height = area->y2 - area->y1 + 1;
  display.startWrite();
  display.setSwapBytes(true);
  display.pushImage(area->x1, area->y1, width, height,
                    reinterpret_cast<uint16_t*>(&color_p->full));
  display.setSwapBytes(false);
  display.endWrite();
  lv_disp_flush_ready(disp);
}

lv_obj_t* createTextLabel(lv_obj_t* parent, int x, int y, const char* text,
                          const lv_font_t* font, uint32_t colorHex) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(colorHex), LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  return label;
}

lv_obj_t* createValueLabel(lv_obj_t* parent, int x, int y, int w, lv_text_align_t align) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_size(label, w, LV_SIZE_CONTENT);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  return label;
}

void setStatusLabelColor(lv_obj_t* label, StatusState state) {
  if (label != nullptr) {
    lv_obj_set_style_text_color(label, statusColorLv(state), LV_PART_MAIN);
  }
}

void syncDashboardLvgl() {
  if (!dashboardUiBuilt) {
    return;
  }

  setStatusLabelColor(loggingLabel, loggingState);
  setStatusLabelColor(wifiLabel, wifiState);

  lv_label_set_text(airTempValueLabel, formatTemp(sensors.airC).c_str());
  lv_label_set_text(airHumidityValueLabel, formatHumidity(sensors.humidity).c_str());
  lv_label_set_text(inletTargetLabel, ("inlet (" + String(settings.inletTargetC, 1) + "C):")
                                        .c_str());
  lv_label_set_text(inletValueLabel, formatProbeValue(sensors.inletC, 0).c_str());
  lv_label_set_text(inletRelayLabel, relayHotState ? "heater ON" : "heater OFF");
  lv_label_set_text(middleValueLabel, formatProbeValue(sensors.middleC, 1).c_str());
  lv_label_set_text(lightValueLabel, String(sensors.lightRaw).c_str());
  lv_label_set_text(outletTargetLabel, ("outlet (" + String(settings.outletTargetC, 1) + "C):")
                                         .c_str());
  lv_label_set_text(outletValueLabel, formatProbeValue(sensors.outletC, 2).c_str());
  lv_label_set_text(outletRelayLabel, relayColdState ? "cooler ON" : "cooler OFF");
}

void createDashboardLvgl() {
  if (dashboardUiBuilt) {
    return;
  }

  dashboardScreen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(dashboardScreen);
  lv_obj_clear_flag(dashboardScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(dashboardScreen, lv_color_hex(kUiBgHex), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dashboardScreen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(dashboardScreen, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dashboardScreen, 0, LV_PART_MAIN);

  dashboardCard = lv_obj_create(dashboardScreen);
  lv_obj_remove_style_all(dashboardCard);
  lv_obj_clear_flag(dashboardCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dashboardCard, 458, 302);
  lv_obj_set_pos(dashboardCard, 11, 9);
  lv_obj_set_style_radius(dashboardCard, 24, LV_PART_MAIN);
  lv_obj_set_style_border_width(dashboardCard, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(dashboardCard, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  lv_obj_set_style_bg_color(dashboardCard, lv_color_hex(kUiCardHex), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(dashboardCard, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dashboardCard, 0, LV_PART_MAIN);

  loggingLabel = createTextLabel(dashboardCard, 18, 18, "logging",
                                 &lv_font_montserrat_18, kUiInkHex);
  lv_obj_set_width(loggingLabel, 120);
  lv_obj_set_style_text_align(loggingLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  wifiLabel = createTextLabel(dashboardCard, 388, 18, "wifi",
                              &lv_font_montserrat_18, kUiInkHex);
  lv_obj_set_width(wifiLabel, 52);
  lv_obj_set_style_text_align(wifiLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

  auto addRow = [&](int y, const char* labelText) {
    lv_obj_t* label = lv_label_create(dashboardCard);
    lv_obj_set_pos(label, 26, y);
    lv_label_set_text(label, labelText);
    lv_obj_set_style_text_color(label, lv_color_hex(kUiInkHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_t* line = lv_obj_create(dashboardCard);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 424, 1);
    lv_obj_set_pos(line, 16, y + 34);
    lv_obj_set_style_bg_color(line, lv_color_hex(kUiLineHex), LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(line, 0, LV_PART_MAIN);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
  };

  addRow(60, "air in:");
  addRow(108, "");
  addRow(156, "middle:");
  addRow(204, "light sensor:");

  airTempValueLabel = createValueLabel(dashboardCard, 198, 58, 110, LV_TEXT_ALIGN_RIGHT);
  airHumidityValueLabel = createValueLabel(dashboardCard, 314, 58, 116, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_style_text_color(airTempValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  lv_obj_set_style_text_color(airHumidityValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);

  inletTargetLabel = createTextLabel(dashboardCard, 26, 108, "",
                                     &lv_font_montserrat_22, kUiInkHex);
  lv_obj_set_width(inletTargetLabel, 200);

  inletValueLabel = createValueLabel(dashboardCard, 198, 106, 110, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_style_text_color(inletValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  inletRelayLabel = lv_label_create(dashboardCard);
  lv_obj_set_pos(inletRelayLabel, 314, 116);
  lv_obj_set_style_text_color(inletRelayLabel, lv_color_hex(kUiMutedHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(inletRelayLabel, &lv_font_montserrat_16, LV_PART_MAIN);

  middleValueLabel = createValueLabel(dashboardCard, 198, 154, 110, LV_TEXT_ALIGN_RIGHT);
  lightValueLabel = createValueLabel(dashboardCard, 190, 202, 110, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_style_text_color(middleValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  lv_obj_set_style_text_color(lightValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);

  outletTargetLabel = createTextLabel(dashboardCard, 26, 252, "",
                                      &lv_font_montserrat_22, kUiInkHex);
  lv_obj_set_width(outletTargetLabel, 200);

  outletValueLabel = createValueLabel(dashboardCard, 198, 250, 110, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_style_text_color(outletValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  outletRelayLabel = lv_label_create(dashboardCard);
  lv_obj_set_pos(outletRelayLabel, 314, 260);
  lv_obj_set_style_text_color(outletRelayLabel, lv_color_hex(kUiMutedHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(outletRelayLabel, &lv_font_montserrat_16, LV_PART_MAIN);

  syncDashboardLvgl();
  dashboardUiBuilt = true;
}

void initLvgl() {
  lv_init();
  lv_disp_draw_buf_init(&lvglDrawBuffer, lvglBuffer, nullptr, 480 * kLvglBufferLines);
  lv_disp_drv_init(&lvglDisplayDriver);
  lvglDisplayDriver.hor_res = 480;
  lvglDisplayDriver.ver_res = 320;
  lvglDisplayDriver.flush_cb = lvglFlush;
  lvglDisplayDriver.draw_buf = &lvglDrawBuffer;
  lv_disp_drv_register(&lvglDisplayDriver);

  createDashboardLvgl();
  lv_scr_load(dashboardScreen);
  dashboardScreenLoaded = true;
  lvglReady = true;
}

void saveSettings() {
  preferences.putString("wifi_ssid", settings.wifiSsid);
  preferences.putString("wifi_pass", settings.wifiPass);
  preferences.putString("aio_user", settings.aioUser);
  preferences.putString("aio_key", settings.aioKey);
  preferences.putBool("aio_enabled", settings.aioEnabled);
  preferences.putFloat("inlet_target", settings.inletTargetC);
  preferences.putFloat("outlet_target", settings.outletTargetC);
}

void loadSettings() {
  preferences.begin("cloudchamber", false);
  settings.wifiSsid =
      preferences.getString("wifi_ssid", String(WIFI_SSID));
  settings.wifiPass =
      preferences.getString("wifi_pass", String(WIFI_PASS));
  settings.aioUser =
      preferences.getString("aio_user", String(AIO_USER));
  settings.aioKey =
      preferences.getString("aio_key", String(AIO_KEY));
  settings.aioEnabled = preferences.getBool("aio_enabled", true);
  settings.inletTargetC = preferences.getFloat("inlet_target", 0.0f);
  settings.outletTargetC = preferences.getFloat("outlet_target", -20.0f);
}

void startWifiConnect() {
  if (settings.wifiSsid.isEmpty()) {
    wifiConnectInFlight = false;
    wifiAttemptFailed = true;
    return;
  }
  WiFi.disconnect(true, true);
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
  wifiConnectInFlight = true;
  wifiAttemptFailed = false;
  lastWifiAttemptMs = millis();
  uiDirty = true;
}

void updateWifiState() {
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    wifiConnectInFlight = false;
    wifiAttemptFailed = false;
    wifiState = StatusState::Good;
  } else if (wifiConnectInFlight &&
             (status == WL_IDLE_STATUS || status == WL_DISCONNECTED ||
              status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED)) {
    if (millis() - lastWifiAttemptMs > 10000) {
      wifiConnectInFlight = false;
      wifiAttemptFailed = true;
    }
  }

  if (status == WL_CONNECTED) {
    wifiState = StatusState::Good;
  } else if (wifiConnectInFlight) {
    wifiState = StatusState::Warning;
  } else if (wifiAttemptFailed || settings.wifiSsid.isEmpty()) {
    wifiState = StatusState::Error;
  } else {
    wifiState = StatusState::Warning;
  }
}

void ensureWifiConnected() {
  updateWifiState();
  if (WiFi.status() == WL_CONNECTED || wifiConnectInFlight) {
    return;
  }
  if (millis() - lastWifiAttemptMs < kWifiRetryMs) {
    return;
  }
  startWifiConnect();
}

void updateLoggingState() {
  if (!settings.aioEnabled) {
    loggingState = StatusState::Error;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    loggingState = wifiConnectInFlight ? StatusState::Warning : StatusState::Error;
    return;
  }
  if (!firstAioSendDone) {
    loggingState = StatusState::Warning;
    return;
  }
  loggingState = lastSendSuccess ? StatusState::Good : StatusState::Error;
}

void drawBadge(const Rect& rect, const String& label, StatusState state) {
  drawFittedText({rect.x, static_cast<int16_t>(rect.y + 5), rect.w, rect.h}, label,
                 statusColor(state), kColorPanel, 2, 1,
                 rect.x < 100 ? TextAlign::Left : TextAlign::Right);
}

void drawTopBar() {
  drawBadge(loggingBadgeRect, "logging", loggingState);
  drawBadge(wifiBadgeRect, "wifi", wifiState);
  lastDrawnLoggingState = loggingState;
  lastDrawnWifiState = wifiState;
}

void drawRowLabelValue(int16_t y, const String& label, const String& value,
                       bool drawDivider = true) {
  display.setTextColor(kColorInk, kColorPanel);
  display.setTextSize(2);
  display.setCursor(30, y);
  display.print(label);
  display.setCursor(240, y);
  display.print(value);
  if (drawDivider) {
    display.drawLine(28, y + 26, 452, y + 26, kColorLine);
  }
}

void clearValueArea(int16_t x, int16_t y, int16_t w, int16_t h) {
  display.fillRect(x, y, w, h, kColorPanel);
}

void drawDashboardFrame() {
  display.fillScreen(kColorBackground);
  display.fillRoundRect(12, 10, 456, 300, 24, kColorPanel);
  display.drawRoundRect(12, 10, 456, 300, 24, kColorInk);

  drawTopBar();
  drawFittedText({30, 76, 150, 16}, "air in:", kColorInk, kColorPanel, 1, 1);
  display.drawLine(28, 102, 452, 102, kColorLine);

  drawFittedText({30, 132, 150, 16}, "inlet", kColorInk, kColorPanel, 1, 1);
  display.drawLine(28, 158, 452, 158, kColorLine);

  drawFittedText({30, 188, 150, 16}, "middle:", kColorInk, kColorPanel, 1, 1);
  display.drawLine(28, 214, 452, 214, kColorLine);

  drawFittedText({30, 234, 170, 16}, "light sensor:", kColorInk, kColorPanel, 1, 1);
  display.drawLine(28, 260, 452, 260, kColorLine);

  drawFittedText({30, 266, 150, 16}, "outlet", kColorInk, kColorPanel, 1, 1);
  dashboardFrameDrawn = true;
}

void drawDashboardValues() {
  if (loggingState != lastDrawnLoggingState || wifiState != lastDrawnWifiState) {
    drawTopBar();
  }

  clearValueArea(228, 72, 102, 18);
  drawFittedText({228, 74, 102, 16}, formatTemp(sensors.airC), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);
  clearValueArea(344, 72, 102, 18);
  drawFittedText({344, 74, 102, 16}, formatHumidity(sensors.humidity), kColorInk,
                 kColorPanel, 1, 1, TextAlign::Right);

  clearValueArea(30, 128, 186, 18);
  drawFittedText({30, 130, 180, 16}, "inlet (" + String(settings.inletTargetC, 1) + "C):",
                 kColorInk, kColorPanel, 1, 1);
  clearValueArea(230, 128, 84, 18);
  drawFittedText({230, 130, 84, 16}, formatProbeValue(sensors.inletC, 0), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);
  clearValueArea(320, 126, 100, 14);
  drawFittedText({320, 128, 100, 12}, relayHotState ? "heater ON" : "heater OFF",
                 kColorMuted, kColorPanel, 1, 1);

  clearValueArea(230, 184, 84, 18);
  drawFittedText({230, 186, 84, 16}, formatProbeValue(sensors.middleC, 1), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);

  clearValueArea(286, 230, 100, 18);
  drawFittedText({286, 232, 100, 16}, String(sensors.lightRaw), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);

  clearValueArea(30, 260, 186, 18);
  drawFittedText({30, 262, 180, 16}, "outlet (" + String(settings.outletTargetC, 1) + "C):",
                 kColorInk, kColorPanel, 1, 1);
  clearValueArea(230, 260, 84, 18);
  drawFittedText({230, 262, 84, 16}, formatProbeValue(sensors.outletC, 2), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);
  clearValueArea(320, 260, 100, 14);
  drawFittedText({320, 262, 100, 12}, relayColdState ? "cooler ON" : "cooler OFF",
                 kColorMuted, kColorPanel, 1, 1);
}

void drawDashboard() {
  if (!dashboardFrameDrawn) {
    drawDashboardFrame();
  }
  drawDashboardValues();
}

void drawMenuHeader(const String& title) {
  dashboardFrameDrawn = false;
  display.fillScreen(kColorBackground);
  display.fillRoundRect(12, 10, 456, 300, 24, kColorPanel);
  display.drawRoundRect(12, 10, 456, 300, 24, kColorInk);
  drawFittedText({30, 22, 300, 28}, title, kColorInk, kColorPanel, 2, 1);

  display.fillRoundRect(350, 20, 90, 36, 14, kColorButton);
  display.drawRoundRect(350, 20, 90, 36, 14, kColorInk);
  drawFittedText({356, 24, 78, 24}, "back", kColorInk, kColorButton, 2, 1,
                 TextAlign::Center);
}

void drawField(int16_t y, const String& label, const String& value,
               bool highlight = false) {
  const uint16_t fill = highlight ? 0xC6DB : 0xF79E;
  drawFittedText({30, y, 160, 20}, label, kColorInk, kColorPanel, 2, 1);
  display.fillRoundRect(30, y + 26, 420, 36, 12, fill);
  display.drawRoundRect(30, y + 26, 420, 36, 12, kColorInk);
  drawFittedText({42, static_cast<int16_t>(y + 32), 396, 24}, value, kColorInk, fill, 2, 1);
}

void drawWifiMenu() {
  drawMenuHeader("wifi settings");
  drawField(72, "network", truncateMiddle(settings.wifiSsid, 28));
  drawField(148, "password", settings.wifiPass.isEmpty() ? "" : "************");
  drawField(224, "status",
            WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                          : (wifiConnectInFlight ? "connecting"
                                                                 : "disconnected"),
            true);
  display.fillRoundRect(300, 254, 150, 36, 12, kColorAccent);
  display.drawRoundRect(300, 254, 150, 36, 12, kColorInk);
  drawFittedText({306, 258, 138, 24}, "reconnect", 0xFFFF, kColorAccent, 2, 1,
                 TextAlign::Center);
}

void drawLoggingMenu() {
  drawMenuHeader("adafruit io");
  drawField(72, "username", truncateMiddle(settings.aioUser, 28));
  drawField(148, "api key", settings.aioKey.isEmpty() ? "" : truncateMiddle(settings.aioKey, 22));
  drawField(224, "logging", settings.aioEnabled ? "enabled" : "disabled", true);
  display.fillRoundRect(260, 254, 190, 36, 12,
                        settings.aioEnabled ? kColorAccent : kColorRed);
  display.drawRoundRect(260, 254, 190, 36, 12, kColorInk);
  drawFittedText({266, 258, 178, 24},
                 settings.aioEnabled ? "tap to disable" : "tap to enable", 0xFFFF,
                 settings.aioEnabled ? kColorAccent : kColorRed, 2, 1,
                 TextAlign::Center);
}

void drawTargetMenu() {
  drawMenuHeader(activeTarget == TargetField::Inlet ? "inlet target" : "outlet target");
  const float liveValue =
      activeTarget == TargetField::Inlet ? sensors.inletC : sensors.outletC;
  const float targetValue =
      activeTarget == TargetField::Inlet ? settings.inletTargetC : settings.outletTargetC;

  drawFittedText({30, 80, 140, 20}, "live value", kColorMuted, kColorPanel, 2, 1);
  drawFittedText({230, 80, 140, 20}, formatTemp(liveValue), kColorInk, kColorPanel, 2, 1);

  drawFittedText({30, 122, 140, 20}, "target", kColorMuted, kColorPanel, 2, 1);
  drawFittedText({230, 122, 140, 20}, String(targetValue, 1) + " C", kColorAccent,
                 kColorPanel, 2, 1);

  const Rect buttons[] = {
      {34, 184, 92, 52}, {142, 184, 92, 52}, {250, 184, 92, 52},
      {358, 184, 92, 52}, {88, 248, 126, 40}, {266, 248, 126, 40}};
  const char* labels[] = {"-5", "-1", "+1", "+5", "save", "cancel"};
  const uint16_t fills[] = {kColorButton, kColorButton, kColorButton, kColorButton,
                            kColorAccent, kColorRed};

  for (int i = 0; i < 6; ++i) {
    display.fillRoundRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h,
                          16, fills[i]);
    display.drawRoundRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h,
                          16, kColorInk);
    drawFittedText({static_cast<int16_t>(buttons[i].x + 8),
                    static_cast<int16_t>(buttons[i].y + 12),
                    static_cast<int16_t>(buttons[i].w - 16),
                    static_cast<int16_t>(buttons[i].h - 20)},
                   labels[i], i >= 4 ? 0xFFFF : kColorInk, fills[i], 2, 1,
                   TextAlign::Center);
  }
}

Rect keyboardCell(int row, int col, int cols) {
  const int16_t marginX = 18;
  const int16_t top = 104;
  const int16_t gap = 6;
  const int16_t width = (444 - (cols - 1) * gap) / cols;
  const int16_t height = 40;
  return {static_cast<int16_t>(marginX + col * (width + gap)),
          static_cast<int16_t>(top + row * (height + gap)), width, height};
}

void drawKey(const Rect& rect, const String& label, uint16_t fill,
             uint16_t ink = kColorInk) {
  display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 10, fill);
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 10, kColorInk);
  drawFittedText({static_cast<int16_t>(rect.x + 4), static_cast<int16_t>(rect.y + 8),
                  static_cast<int16_t>(rect.w - 8), static_cast<int16_t>(rect.h - 12)},
                 label, ink, fill, 2, 1,
                 label.length() <= 4 ? TextAlign::Center : TextAlign::Left);
}

String keyLabel(char ch) {
  if (editorShift && ch >= 'a' && ch <= 'z') {
    return String(static_cast<char>(ch - 32));
  }
  return String(ch);
}

void drawTextEditor() {
  drawMenuHeader("edit text");
  switch (activeTextField) {
    case TextField::WifiSsid:
      drawFittedText({30, 66, 180, 20}, "wifi network", kColorMuted, kColorPanel, 2, 1);
      break;
    case TextField::WifiPass:
      drawFittedText({30, 66, 180, 20}, "wifi password", kColorMuted, kColorPanel, 2, 1);
      break;
    case TextField::AioUser:
      drawFittedText({30, 66, 180, 20}, "aio username", kColorMuted, kColorPanel, 2, 1);
      break;
    case TextField::AioKey:
      drawFittedText({30, 66, 180, 20}, "aio key", kColorMuted, kColorPanel, 2, 1);
      break;
  }

  display.fillRoundRect(30, 88, 420, 44, 12, 0xF79E);
  display.drawRoundRect(30, 88, 420, 44, 12, kColorInk);
  drawFittedText({40, 96, 400, 28}, editorValue, kColorInk, 0xF79E, 2, 1);

  const char* rows[] = {"1234567890", "qwertyuiop", "asdfghjkl.", "zxcvbnm_@!"};
  for (int row = 0; row < 4; ++row) {
    const String chars = rows[row];
    for (int col = 0; col < chars.length(); ++col) {
      drawKey(keyboardCell(row, col, chars.length()), keyLabel(chars[col]), kColorButton);
    }
  }

  drawKey({18, 288, 90, 28}, editorShift ? "SHIFT" : "shift", kColorAccent, 0xFFFF);
  drawKey({120, 288, 90, 28}, "space", kColorButton);
  drawKey({222, 288, 90, 28}, "back", kColorButton);
  drawKey({324, 288, 60, 28}, "save", kColorGreen, 0xFFFF);
  drawKey({390, 288, 60, 28}, "x", kColorRed, 0xFFFF);
}

void redrawUi() {
  updateWifiState();
  updateLoggingState();
  switch (currentScreen) {
    case ScreenId::Dashboard:
      if (lvglReady) {
        if (!dashboardScreenLoaded) {
          lv_scr_load(dashboardScreen);
          dashboardScreenLoaded = true;
        }
        syncDashboardLvgl();
        lv_obj_invalidate(dashboardScreen);
        lv_timer_handler();
      } else {
        drawDashboard();
      }
      break;
    case ScreenId::WifiMenu:
      dashboardScreenLoaded = false;
      drawWifiMenu();
      break;
    case ScreenId::LoggingMenu:
      dashboardScreenLoaded = false;
      drawLoggingMenu();
      break;
    case ScreenId::TargetMenu:
      dashboardScreenLoaded = false;
      drawTargetMenu();
      break;
    case ScreenId::TextEditor:
      dashboardScreenLoaded = false;
      drawTextEditor();
      break;
  }
  uiDirty = false;
}

void openTextEditor(TextField field) {
  activeTextField = field;
  switch (field) {
    case TextField::WifiSsid:
      editorValue = settings.wifiSsid;
      break;
    case TextField::WifiPass:
      editorValue = settings.wifiPass;
      break;
    case TextField::AioUser:
      editorValue = settings.aioUser;
      break;
    case TextField::AioKey:
      editorValue = settings.aioKey;
      break;
  }
  editorShift = false;
  currentScreen = ScreenId::TextEditor;
  uiDirty = true;
}

void commitEditor() {
  switch (activeTextField) {
    case TextField::WifiSsid:
      settings.wifiSsid = editorValue;
      startWifiConnect();
      break;
    case TextField::WifiPass:
      settings.wifiPass = editorValue;
      startWifiConnect();
      break;
    case TextField::AioUser:
      settings.aioUser = editorValue;
      firstAioSendDone = false;
      break;
    case TextField::AioKey:
      settings.aioKey = editorValue;
      firstAioSendDone = false;
      break;
  }
  saveSettings();
}

void adjustActiveTarget(float delta) {
  if (activeTarget == TargetField::Inlet) {
    settings.inletTargetC += delta;
    settings.inletTargetC = constrain(settings.inletTargetC, -40.0f, 60.0f);
  } else {
    settings.outletTargetC += delta;
    settings.outletTargetC = constrain(settings.outletTargetC, -80.0f, 40.0f);
  }
  uiDirty = true;
}

void publishFeeds() {
  if (!settings.aioEnabled || WiFi.status() != WL_CONNECTED ||
      settings.aioUser.isEmpty() || settings.aioKey.isEmpty()) {
    return;
  }
  if (millis() - lastSendMs < kSendIntervalMs) {
    return;
  }
  lastSendMs = millis();
  lastSendSuccess = true;
  firstAioSendDone = true;

  const float values[kNumFeeds] = {sensors.inletC, sensors.middleC, sensors.outletC,
                                   sensors.airC, sensors.humidity,
                                   static_cast<float>(sensors.lightRaw)};

  for (int i = 0; i < kNumFeeds; ++i) {
    if (i < 5 && isnan(values[i])) {
      continue;
    }
    String url = "/api/v2/" + settings.aioUser + "/feeds/" + kFeedNames[i] + "/data";
    String body = String("{\"value\":") + String(values[i], 2) + "}";
    httpClient.beginRequest();
    httpClient.post(url.c_str());
    httpClient.sendHeader("Content-Type", "application/json");
    httpClient.sendHeader("X-AIO-Key", settings.aioKey);
    httpClient.sendHeader("Content-Length", body.length());
    httpClient.beginBody();
    httpClient.print(body);
    httpClient.endRequest();
    const int status = httpClient.responseStatusCode();
    if (status != 200 && status != 201) {
      lastSendSuccess = false;
    }
    while (httpClient.available()) {
      httpClient.read();
    }
  }
  uiDirty = true;
}

void updateSensors() {
  if (millis() - lastReadMs < kReadIntervalMs) {
    return;
  }
  lastReadMs = millis();

  tempSensors.requestTemperatures();
  const int deviceCount = tempSensors.getDS18Count();
  const float inlet = tempSensors.getTempCByIndex(0);
  const float middle = tempSensors.getTempCByIndex(1);
  const float outlet = tempSensors.getTempCByIndex(2);

  sensors.ds18Count = deviceCount;

  sensors.probeValid =
      isValidProbeTemp(inlet) && isValidProbeTemp(middle) && isValidProbeTemp(outlet);
  sensors.inletC = isValidProbeTemp(inlet) ? inlet : NAN;
  sensors.middleC = isValidProbeTemp(middle) ? middle : NAN;
  sensors.outletC = isValidProbeTemp(outlet) ? outlet : NAN;

  sensors_event_t humidityEvent;
  sensors_event_t tempEvent;
  if (sht4.getEvent(&humidityEvent, &tempEvent)) {
    sensors.airC = tempEvent.temperature;
    sensors.humidity = humidityEvent.relative_humidity;
    sensors.airValid = true;
  } else {
    sensors.airValid = false;
  }

  sensors.lightRaw = analogRead(BoardConfig::kLightPin);
  uiDirty = true;

  if (serialConnected) {
    Serial.print("SENSORS;");
    Serial.print("DS18COUNT:");
    Serial.print(deviceCount);
    Serial.print(";");
    Serial.print("HOT:");
    Serial.print(sensors.inletC, 2);
    Serial.print(";MID:");
    Serial.print(sensors.middleC, 2);
    Serial.print(";COLD:");
    Serial.print(sensors.outletC, 2);
    Serial.print(";AIR_T:");
    if (!isnan(sensors.airC)) {
      Serial.print(sensors.airC, 2);
    } else {
      Serial.print("NaN");
    }
    Serial.print(";AIR_H:");
    if (!isnan(sensors.humidity)) {
      Serial.print(sensors.humidity, 2);
    } else {
      Serial.print("NaN");
    }
    Serial.print(";LIGHT:");
    Serial.print(sensors.lightRaw);
    Serial.print(";RHOT:");
    Serial.print(relayHotState ? "ON" : "OFF");
    Serial.print(";RCOLD:");
    Serial.println(relayColdState ? "ON" : "OFF");
  }
}

void controlRelays() {
  if (!sensors.probeValid || millis() - lastRelayMs < kRelayIntervalMs) {
    return;
  }
  lastRelayMs = millis();

  if (sensors.inletC > settings.inletTargetC + kTargetHysteresisC) {
    setRelayHot(false);
  } else if (sensors.inletC < settings.inletTargetC - kTargetHysteresisC) {
    setRelayHot(true);
  }

  if (sensors.outletC < settings.outletTargetC - kTargetHysteresisC) {
    setRelayCold(false);
  } else if (sensors.outletC > settings.outletTargetC + kTargetHysteresisC) {
    setRelayCold(true);
  }
}

void updateSerialConnection() {
  if (millis() - lastSerialCheckMs < 1000) {
    return;
  }
  lastSerialCheckMs = millis();
  serialConnected = static_cast<bool>(Serial);
}

void handleSerialCommand(const String& raw) {
  String cmd = raw;
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "RELAY HOT ON") {
    setRelayHot(true);
  } else if (cmd == "RELAY HOT OFF") {
    setRelayHot(false);
  } else if (cmd == "RELAY COLD ON") {
    setRelayCold(true);
  } else if (cmd == "RELAY COLD OFF") {
    setRelayCold(false);
  } else if (cmd.startsWith("TARGET INLET ")) {
    settings.inletTargetC = cmd.substring(13).toFloat();
    saveSettings();
  } else if (cmd.startsWith("TARGET OUTLET ")) {
    settings.outletTargetC = cmd.substring(14).toFloat();
    saveSettings();
  }
  uiDirty = true;
}

void readSerial() {
  if (!serialConnected) {
    return;
  }
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    if (line.length() > 0) {
      handleSerialCommand(line);
    }
  }
}

bool tappedBack(int16_t x, int16_t y) {
  return pointInRect(x, y, {350, 20, 90, 36});
}

void handleWifiMenuTap(int16_t x, int16_t y) {
  if (tappedBack(x, y)) {
    currentScreen = ScreenId::Dashboard;
  } else if (pointInRect(x, y, {30, 98, 420, 36})) {
    openTextEditor(TextField::WifiSsid);
    return;
  } else if (pointInRect(x, y, {30, 174, 420, 36})) {
    openTextEditor(TextField::WifiPass);
    return;
  } else if (pointInRect(x, y, {300, 254, 150, 36})) {
    startWifiConnect();
  } else {
    return;
  }
  uiDirty = true;
}

void handleLoggingMenuTap(int16_t x, int16_t y) {
  if (tappedBack(x, y)) {
    currentScreen = ScreenId::Dashboard;
  } else if (pointInRect(x, y, {30, 98, 420, 36})) {
    openTextEditor(TextField::AioUser);
    return;
  } else if (pointInRect(x, y, {30, 174, 420, 36})) {
    openTextEditor(TextField::AioKey);
    return;
  } else if (pointInRect(x, y, {260, 254, 190, 36}) ||
             pointInRect(x, y, {30, 250, 420, 40})) {
    settings.aioEnabled = !settings.aioEnabled;
    saveSettings();
    uiDirty = true;
  } else {
    return;
  }
}

void handleTargetMenuTap(int16_t x, int16_t y) {
  if (tappedBack(x, y)) {
    currentScreen = ScreenId::Dashboard;
  } else if (pointInRect(x, y, {34, 184, 92, 52})) {
    adjustActiveTarget(-5.0f);
    return;
  } else if (pointInRect(x, y, {142, 184, 92, 52})) {
    adjustActiveTarget(-1.0f);
    return;
  } else if (pointInRect(x, y, {250, 184, 92, 52})) {
    adjustActiveTarget(1.0f);
    return;
  } else if (pointInRect(x, y, {358, 184, 92, 52})) {
    adjustActiveTarget(5.0f);
    return;
  } else if (pointInRect(x, y, {88, 248, 126, 40})) {
    saveSettings();
    currentScreen = ScreenId::Dashboard;
  } else if (pointInRect(x, y, {266, 248, 126, 40})) {
    loadSettings();
    currentScreen = ScreenId::Dashboard;
  } else {
    return;
  }
  uiDirty = true;
}

void appendEditorChar(char ch) {
  if (editorValue.length() >= 63) {
    return;
  }
  if (editorShift && ch >= 'a' && ch <= 'z') {
    editorValue += static_cast<char>(ch - 32);
  } else {
    editorValue += ch;
  }
}

void handleTextEditorTap(int16_t x, int16_t y) {
  if (tappedBack(x, y) || pointInRect(x, y, {390, 288, 60, 28})) {
    currentScreen = (activeTextField == TextField::WifiSsid ||
                     activeTextField == TextField::WifiPass)
                        ? ScreenId::WifiMenu
                        : ScreenId::LoggingMenu;
    uiDirty = true;
    return;
  }

  const char* rows[] = {"1234567890", "qwertyuiop", "asdfghjkl.", "zxcvbnm_@!"};
  for (int row = 0; row < 4; ++row) {
    const String chars = rows[row];
    for (int col = 0; col < chars.length(); ++col) {
      if (pointInRect(x, y, keyboardCell(row, col, chars.length()))) {
        appendEditorChar(chars[col]);
        uiDirty = true;
        return;
      }
    }
  }

  if (pointInRect(x, y, {18, 288, 90, 28})) {
    editorShift = !editorShift;
  } else if (pointInRect(x, y, {120, 288, 90, 28})) {
    appendEditorChar(' ');
  } else if (pointInRect(x, y, {222, 288, 90, 28})) {
    if (!editorValue.isEmpty()) {
      editorValue.remove(editorValue.length() - 1);
    }
  } else if (pointInRect(x, y, {324, 288, 60, 28})) {
    commitEditor();
    currentScreen = (activeTextField == TextField::WifiSsid ||
                     activeTextField == TextField::WifiPass)
                        ? ScreenId::WifiMenu
                        : ScreenId::LoggingMenu;
  } else {
    return;
  }
  uiDirty = true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(BoardConfig::kRelayHotPin, OUTPUT);
  pinMode(BoardConfig::kRelayColdPin, OUTPUT);
  pinMode(BoardConfig::kFunctionSelectPin, OUTPUT);
  setRelayHot(false);
  setRelayCold(false);
  digitalWrite(BoardConfig::kFunctionSelectPin, LOW);
  pinMode(BoardConfig::kOneWirePin, INPUT_PULLUP);

  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  loadSettings();
  settings.wifiSsid = WIFI_SSID;
  settings.wifiPass = WIFI_PASS;
  saveSettings();

  tempSensors.begin();
  tempSensors.setWaitForConversion(true);
  tempSensors.setCheckForConversion(true);
  Wire.begin(BoardConfig::kI2cSdaPin, BoardConfig::kI2cSclPin);
  sht4.begin();

  display.init();
  display.setRotation(0);
  display.setBrightness(220);
  display.setTextDatum(top_left);
  display.setTextFont(2);
  initLvgl();
  lastLvglTickMs = millis();
  if (serialConnected) {
    Serial.print("DS18;COUNT;");
    Serial.println(tempSensors.getDS18Count());
  }

  startWifiConnect();
  uiDirty = true;
}

void loop() {
  const unsigned long now = millis();
  lv_tick_inc(now - lastLvglTickMs);
  lastLvglTickMs = now;
  updateSerialConnection();
  readSerial();
  ensureWifiConnected();
  updateSensors();
  controlRelays();
  publishFeeds();

  if (uiDirty) {
    redrawUi();
  } else if (lvglReady && currentScreen == ScreenId::Dashboard) {
    lv_timer_handler();
  }
}
