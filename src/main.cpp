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
#include <TAMC_GT911.h>
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
constexpr unsigned long kTouchDebounceMs = 220;
constexpr unsigned long kTouchPollMs = 30;
constexpr unsigned long kWifiRetryMs = 15000;
constexpr float kTargetHysteresisC = 2.0f;
constexpr uint8_t kGt911PrimaryAddress = 0x14;
constexpr uint8_t kGt911SecondaryAddress = 0x5D;
constexpr uint8_t kTouchCalibrationVersion = 1;

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

constexpr uint32_t kUiBgHex = 0xE7EEF1;
constexpr uint32_t kUiCardHex = 0xFCFEFD;
constexpr uint32_t kUiInkHex = 0x20343E;
constexpr uint32_t kUiMutedHex = 0x5F7884;
constexpr uint32_t kUiLineHex = 0xD4DFE4;
constexpr uint32_t kUiAccentHex = 0x2E8CA6;
constexpr uint32_t kUiTouchHex = 0xCFEAF1;
constexpr uint32_t kUiTouchBorderHex = 0x67A6B7;

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
  bool probeValid = false;
  bool airValid = false;
};

struct TouchCalibration {
  bool valid = false;
  bool swapXY = false;
  bool invertX = false;
  bool invertY = false;
  int rawMinX = 0;
  int rawMaxX = 479;
  int rawMinY = 0;
  int rawMaxY = 319;
};

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

enum class ScreenId { Dashboard, WifiMenu, LoggingMenu, TargetMenu, TextEditor, TouchCal };
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
TAMC_GT911 touchController(BoardConfig::kI2cSdaPin, BoardConfig::kI2cSclPin,
                           BoardConfig::kTouchIntPin, BoardConfig::kTouchResetPin,
                           480, 320);

AppSettings settings;
SensorState sensors;
TouchCalibration touchCal;

ScreenId currentScreen = ScreenId::Dashboard;
TargetField activeTarget = TargetField::Inlet;
TextField activeTextField = TextField::WifiSsid;
String editorValue;
bool editorShift = false;
bool uiDirty = true;
bool dashboardFrameDrawn = false;
bool showTouchDebug = false;
bool touchDebugLatched = false;
bool touchCalActive = true;
bool touchCalWaitForRelease = false;

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
unsigned long lastTouchMs = 0;
unsigned long lastTouchPollMs = 0;
unsigned long lastTouchReportMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastSerialCheckMs = 0;

StatusState wifiState = StatusState::Warning;
StatusState loggingState = StatusState::Warning;
StatusState lastDrawnWifiState = StatusState::Error;
StatusState lastDrawnLoggingState = StatusState::Error;
uint8_t activeTouchAddress = 0;
int16_t lastTouchDebugX = -1;
int16_t lastTouchDebugY = -1;

constexpr int kTouchCalPointCount = 4;
int touchCalStep = 0;
TP_Point touchCalSamples[kTouchCalPointCount];
const int16_t kTouchCalTargets[kTouchCalPointCount][2] = {
    {36, 36}, {444, 36}, {36, 284}, {444, 284}};

Rect wifiBadgeRect = {260, 18, 190, 44};
Rect loggingBadgeRect = {30, 18, 190, 44};
Rect inletTargetRect = {118, 134, 94, 24};
Rect outletTargetRect = {130, 244, 110, 24};

constexpr uint16_t kLvglBufferLines = 24;
lv_color_t lvglBuffer[480 * kLvglBufferLines];
lv_disp_draw_buf_t lvglDrawBuffer;
lv_disp_drv_t lvglDisplayDriver;
lv_indev_drv_t lvglInputDriver;
unsigned long lastLvglTickMs = 0;
bool lvglReady = false;
bool dashboardUiBuilt = false;
bool dashboardScreenLoaded = false;
bool lvglTouchPressed = false;
int16_t lvglTouchX = 0;
int16_t lvglTouchY = 0;
unsigned long lastLvglTouchMs = 0;

lv_obj_t* dashboardScreen = nullptr;
lv_obj_t* loggingButton = nullptr;
lv_obj_t* touchButton = nullptr;
lv_obj_t* wifiButton = nullptr;
lv_obj_t* loggingLabel = nullptr;
lv_obj_t* touchLabel = nullptr;
lv_obj_t* wifiLabel = nullptr;
lv_obj_t* airTempValueLabel = nullptr;
lv_obj_t* airHumidityValueLabel = nullptr;
lv_obj_t* inletTargetButton = nullptr;
lv_obj_t* inletTargetLabel = nullptr;
lv_obj_t* inletValueLabel = nullptr;
lv_obj_t* inletRelayLabel = nullptr;
lv_obj_t* middleValueLabel = nullptr;
lv_obj_t* lightValueLabel = nullptr;
lv_obj_t* outletTargetButton = nullptr;
lv_obj_t* outletTargetLabel = nullptr;
lv_obj_t* outletValueLabel = nullptr;
lv_obj_t* outletRelayLabel = nullptr;
lv_obj_t* dashboardCard = nullptr;

void mapTouchPoint(int16_t rawX, int16_t rawY, int16_t& x, int16_t& y);

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
      return lv_palette_main(LV_PALETTE_GREEN);
    case StatusState::Warning:
      return lv_palette_main(LV_PALETTE_AMBER);
    case StatusState::Error:
    default:
      return lv_palette_main(LV_PALETTE_RED);
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

void startTouchCalibration() {
  touchCalActive = true;
  touchCalStep = 0;
  lvglTouchPressed = false;
  lvglTouchX = 0;
  lvglTouchY = 0;
  touchCalWaitForRelease = true;
  dashboardScreenLoaded = false;
  lastTouchMs = 0;
  lastLvglTouchMs = 0;
  currentScreen = ScreenId::TouchCal;
  uiDirty = true;
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

void lvglTouchRead(lv_indev_drv_t* indev_drv, lv_indev_data_t* data) {
  (void)indev_drv;
  data->state = lvglTouchPressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  data->point.x = lvglTouchX;
  data->point.y = lvglTouchY;
  if (currentScreen != ScreenId::Dashboard || touchCalActive || activeTouchAddress == 0) {
    lvglTouchPressed = false;
    return;
  }

  touchController.read();
  if (!touchController.isTouched) {
    lvglTouchPressed = false;
    return;
  }

  const int16_t rawX = touchController.points[0].x;
  const int16_t rawY = touchController.points[0].y;
  if (rawX < 0 || rawY < 0 || rawX > 480 || rawY > 320) {
    lvglTouchPressed = false;
    return;
  }

  int16_t x = 0;
  int16_t y = 0;
  mapTouchPoint(rawX, rawY, x, y);
  if (millis() - lastLvglTouchMs < kTouchDebounceMs &&
      abs(x - lvglTouchX) < 3 && abs(y - lvglTouchY) < 3) {
    data->state = lvglTouchPressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = lvglTouchX;
    data->point.y = lvglTouchY;
    return;
  }

  lvglTouchX = x;
  lvglTouchY = y;
  lvglTouchPressed = true;
  lastLvglTouchMs = millis();
  data->state = LV_INDEV_STATE_PR;
  data->point.x = x;
  data->point.y = y;
}

void openWifiMenuEvent(lv_event_t* event) {
  (void)event;
  currentScreen = ScreenId::WifiMenu;
  uiDirty = true;
}

void openLoggingMenuEvent(lv_event_t* event) {
  (void)event;
  currentScreen = ScreenId::LoggingMenu;
  uiDirty = true;
}

void openTouchCalibrationEvent(lv_event_t* event) {
  (void)event;
  startTouchCalibration();
}

void openInletTargetEvent(lv_event_t* event) {
  (void)event;
  activeTarget = TargetField::Inlet;
  currentScreen = ScreenId::TargetMenu;
  uiDirty = true;
}

void openOutletTargetEvent(lv_event_t* event) {
  (void)event;
  activeTarget = TargetField::Outlet;
  currentScreen = ScreenId::TargetMenu;
  uiDirty = true;
}

void styleHeaderButton(lv_obj_t* button, lv_obj_t** label, const char* text) {
  lv_obj_set_size(button, 96, 32);
  styleFlatButton(button, lv_color_hex(kUiCardHex), lv_color_hex(0x9bb7c8));
  *label = lv_label_create(button);
  lv_label_set_text(*label, text);
  lv_obj_set_style_text_color(*label, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(*label, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_center(*label);
}

lv_obj_t* createValueLabel(lv_obj_t* parent, int x, int y, int w, lv_text_align_t align) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_size(label, w, LV_SIZE_CONTENT);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  return label;
}

void setStatusButtonColor(lv_obj_t* button, StatusState state) {
  styleFlatButton(button, lv_color_mix(statusColorLv(state), lv_color_white(), LV_OPA_20),
                  statusColorLv(state));
}

void syncDashboardLvgl() {
  if (!dashboardUiBuilt) {
    return;
  }

  setStatusButtonColor(loggingButton, loggingState);
  setStatusButtonColor(wifiButton, wifiState);
  styleFlatButton(touchButton, lv_color_hex(kUiTouchHex), lv_color_hex(kUiTouchBorderHex));

  lv_label_set_text(airTempValueLabel, formatTemp(sensors.airC).c_str());
  lv_label_set_text(airHumidityValueLabel, formatHumidity(sensors.humidity).c_str());
  lv_label_set_text(inletTargetLabel, ("inlet (" + String(settings.inletTargetC, 1) + "C)")
                                        .c_str());
  lv_label_set_text(inletValueLabel, formatTemp(sensors.inletC).c_str());
  lv_label_set_text(inletRelayLabel, relayHotState ? "heater ON" : "heater OFF");
  lv_label_set_text(middleValueLabel, formatTemp(sensors.middleC).c_str());
  lv_label_set_text(lightValueLabel, String(sensors.lightRaw).c_str());
  lv_label_set_text(outletTargetLabel, ("outlet (" + String(settings.outletTargetC, 1) + "C)")
                                         .c_str());
  lv_label_set_text(outletValueLabel, formatTemp(sensors.outletC).c_str());
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
  lv_obj_set_size(dashboardCard, 456, 300);
  lv_obj_set_pos(dashboardCard, 12, 10);
  lv_obj_set_style_radius(dashboardCard, 24, LV_PART_MAIN);
  lv_obj_set_style_border_width(dashboardCard, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(dashboardCard, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  lv_obj_set_style_bg_color(dashboardCard, lv_color_hex(kUiCardHex), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(dashboardCard, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dashboardCard, 0, LV_PART_MAIN);

  lv_obj_t* headerRow = lv_obj_create(dashboardCard);
  lv_obj_remove_style_all(headerRow);
  lv_obj_set_size(headerRow, 420, 36);
  lv_obj_set_pos(headerRow, 18, 14);
  lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(headerRow, LV_OBJ_FLAG_SCROLLABLE);

  loggingButton = lv_btn_create(headerRow);
  styleHeaderButton(loggingButton, &loggingLabel, "logging");
  lv_obj_add_event_cb(loggingButton, openLoggingMenuEvent, LV_EVENT_CLICKED, nullptr);

  touchButton = lv_btn_create(headerRow);
  styleHeaderButton(touchButton, &touchLabel, "touch");
  lv_obj_add_event_cb(touchButton, openTouchCalibrationEvent, LV_EVENT_CLICKED, nullptr);

  wifiButton = lv_btn_create(headerRow);
  styleHeaderButton(wifiButton, &wifiLabel, "wifi");
  lv_obj_add_event_cb(wifiButton, openWifiMenuEvent, LV_EVENT_CLICKED, nullptr);

  auto addRow = [&](int y, const char* labelText) {
    lv_obj_t* label = lv_label_create(dashboardCard);
    lv_obj_set_pos(label, 26, y);
    lv_label_set_text(label, labelText);
    lv_obj_set_style_text_color(label, lv_color_hex(kUiInkHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_t* line = lv_obj_create(dashboardCard);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 424, 1);
    lv_obj_set_pos(line, 16, y + 24);
    lv_obj_set_style_bg_color(line, lv_color_hex(kUiLineHex), LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(line, 0, LV_PART_MAIN);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
  };

  addRow(72, "air in:");
  addRow(120, "");
  addRow(168, "middle:");
  addRow(214, "light sensor:");

  airTempValueLabel = createValueLabel(dashboardCard, 236, 72, 94, LV_TEXT_ALIGN_RIGHT);
  airHumidityValueLabel = createValueLabel(dashboardCard, 338, 72, 102, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_style_text_color(airTempValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  lv_obj_set_style_text_color(airHumidityValueLabel, lv_color_hex(kUiMutedHex), LV_PART_MAIN);

  inletTargetButton = lv_btn_create(dashboardCard);
  lv_obj_set_size(inletTargetButton, 186, 28);
  lv_obj_set_pos(inletTargetButton, 20, 116);
  lv_obj_set_style_bg_opa(inletTargetButton, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(inletTargetButton, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(inletTargetButton, 0, LV_PART_MAIN);
  inletTargetLabel = lv_label_create(inletTargetButton);
  lv_obj_set_style_text_color(inletTargetLabel, lv_color_hex(0x2d86a6), LV_PART_MAIN);
  lv_obj_set_style_text_font(inletTargetLabel, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_align(inletTargetLabel, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(inletTargetButton, openInletTargetEvent, LV_EVENT_CLICKED, nullptr);

  inletValueLabel = createValueLabel(dashboardCard, 230, 120, 84, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_style_text_color(inletValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  inletRelayLabel = lv_label_create(dashboardCard);
  lv_obj_set_pos(inletRelayLabel, 322, 124);
  lv_obj_set_style_text_color(inletRelayLabel, lv_color_hex(kUiMutedHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(inletRelayLabel, &lv_font_montserrat_12, LV_PART_MAIN);

  middleValueLabel = createValueLabel(dashboardCard, 230, 168, 84, LV_TEXT_ALIGN_RIGHT);
  lightValueLabel = createValueLabel(dashboardCard, 286, 214, 100, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_style_text_color(middleValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  lv_obj_set_style_text_color(lightValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);

  outletTargetButton = lv_btn_create(dashboardCard);
  lv_obj_set_size(outletTargetButton, 186, 28);
  lv_obj_set_pos(outletTargetButton, 20, 250);
  lv_obj_set_style_bg_opa(outletTargetButton, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(outletTargetButton, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(outletTargetButton, 0, LV_PART_MAIN);
  outletTargetLabel = lv_label_create(outletTargetButton);
  lv_obj_set_style_text_color(outletTargetLabel, lv_color_hex(0x2d86a6), LV_PART_MAIN);
  lv_obj_set_style_text_font(outletTargetLabel, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_align(outletTargetLabel, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_add_event_cb(outletTargetButton, openOutletTargetEvent, LV_EVENT_CLICKED, nullptr);

  outletValueLabel = createValueLabel(dashboardCard, 230, 252, 84, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_style_text_color(outletValueLabel, lv_color_hex(kUiInkHex), LV_PART_MAIN);
  outletRelayLabel = lv_label_create(dashboardCard);
  lv_obj_set_pos(outletRelayLabel, 322, 256);
  lv_obj_set_style_text_color(outletRelayLabel, lv_color_hex(kUiMutedHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(outletRelayLabel, &lv_font_montserrat_12, LV_PART_MAIN);

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

  lv_indev_drv_init(&lvglInputDriver);
  lvglInputDriver.type = LV_INDEV_TYPE_POINTER;
  lvglInputDriver.read_cb = lvglTouchRead;
  lv_indev_drv_register(&lvglInputDriver);

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

void saveTouchCalibration() {
  preferences.putUChar("touch_ver", kTouchCalibrationVersion);
  preferences.putBool("touch_valid", touchCal.valid);
  preferences.putBool("touch_swap", touchCal.swapXY);
  preferences.putBool("touch_inv_x", touchCal.invertX);
  preferences.putBool("touch_inv_y", touchCal.invertY);
  preferences.putInt("touch_min_x", touchCal.rawMinX);
  preferences.putInt("touch_max_x", touchCal.rawMaxX);
  preferences.putInt("touch_min_y", touchCal.rawMinY);
  preferences.putInt("touch_max_y", touchCal.rawMaxY);
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
  const uint8_t savedTouchVersion = preferences.getUChar("touch_ver", 0);
  touchCal.valid = preferences.getBool("touch_valid", false);
  touchCal.swapXY = preferences.getBool("touch_swap", false);
  touchCal.invertX = preferences.getBool("touch_inv_x", false);
  touchCal.invertY = preferences.getBool("touch_inv_y", false);
  touchCal.rawMinX = preferences.getInt("touch_min_x", 0);
  touchCal.rawMaxX = preferences.getInt("touch_max_x", 479);
  touchCal.rawMinY = preferences.getInt("touch_min_y", 0);
  touchCal.rawMaxY = preferences.getInt("touch_max_y", 319);
  touchCalActive = !touchCal.valid && savedTouchVersion != kTouchCalibrationVersion;
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
  display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 18, kColorPanel);
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 18, statusColor(state));
  display.drawRoundRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 18,
                        statusColor(state));
  display.fillCircle(rect.x + 24, rect.y + rect.h / 2, 8, statusColor(state));
  drawFittedText({static_cast<int16_t>(rect.x + 42), static_cast<int16_t>(rect.y + 7),
                  static_cast<int16_t>(rect.w - 56), static_cast<int16_t>(rect.h - 10)},
                 label, kColorInk, kColorPanel, 2, 1, TextAlign::Left);
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
  drawFittedText({30, 130, 54, 16}, "inlet", kColorInk, kColorPanel, 1, 1);
  drawFittedText({86, 130, 124, 16}, "(" + String(settings.inletTargetC, 1) + "C):",
                 kColorAccent, kColorPanel, 1, 1);
  clearValueArea(230, 128, 84, 18);
  drawFittedText({230, 130, 84, 16}, formatTemp(sensors.inletC), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);
  clearValueArea(320, 126, 100, 14);
  drawFittedText({320, 128, 100, 12}, relayHotState ? "heater ON" : "heater OFF",
                 kColorMuted, kColorPanel, 1, 1);

  clearValueArea(230, 184, 84, 18);
  drawFittedText({230, 186, 84, 16}, formatTemp(sensors.middleC), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);

  clearValueArea(286, 230, 100, 18);
  drawFittedText({286, 232, 100, 16}, String(sensors.lightRaw), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);

  clearValueArea(30, 260, 186, 18);
  drawFittedText({30, 262, 60, 16}, "outlet", kColorInk, kColorPanel, 1, 1);
  drawFittedText({96, 262, 118, 16}, "(" + String(settings.outletTargetC, 1) + "C):",
                 kColorAccent, kColorPanel, 1, 1);
  clearValueArea(230, 260, 84, 18);
  drawFittedText({230, 262, 84, 16}, formatTemp(sensors.outletC), kColorInk, kColorPanel,
                 1, 1, TextAlign::Right);
  clearValueArea(320, 260, 100, 14);
  drawFittedText({320, 262, 100, 12}, relayColdState ? "cooler ON" : "cooler OFF",
                 kColorMuted, kColorPanel, 1, 1);

  if (showTouchDebug) {
    clearValueArea(24, 286, 220, 16);
    display.setTextColor(kColorRed, kColorPanel);
    display.setTextSize(1);
    display.setCursor(28, 288);
    if (touchDebugLatched) {
      display.print("touch ");
      display.print(lastTouchDebugX);
      display.print(",");
      display.print(lastTouchDebugY);
    } else {
      display.print("touch none");
    }

    if (touchDebugLatched) {
      display.drawRect(lastTouchDebugX - 6, lastTouchDebugY - 6, 12, 12, kColorRed);
      display.drawLine(lastTouchDebugX - 8, lastTouchDebugY, lastTouchDebugX + 8,
                       lastTouchDebugY, kColorRed);
      display.drawLine(lastTouchDebugX, lastTouchDebugY - 8, lastTouchDebugX,
                       lastTouchDebugY + 8, kColorRed);
    }
  }
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

void drawCalibrationTarget(int16_t x, int16_t y, uint16_t color) {
  display.fillCircle(x, y, 10, color);
  display.fillCircle(x, y, 4, kColorPanel);
  display.drawLine(x - 18, y, x + 18, y, color);
  display.drawLine(x, y - 18, x, y + 18, color);
}

void drawTouchCalibrationScreen() {
  dashboardFrameDrawn = false;
  display.fillScreen(kColorBackground);
  display.fillRoundRect(12, 10, 456, 300, 24, kColorPanel);
  display.drawRoundRect(12, 10, 456, 300, 24, kColorInk);
  drawFittedText({28, 22, 260, 24}, "touch calibration", kColorInk, kColorPanel, 2, 1);
  drawFittedText({28, 52, 340, 14}, "tap the red target in each corner",
                 kColorMuted, kColorPanel, 1, 1);
  drawFittedText({28, 68, 320, 14}, "lift your finger after each corner tap",
                 kColorMuted, kColorPanel, 1, 1);

  for (int i = 0; i < kTouchCalPointCount; ++i) {
    const uint16_t color = (i == touchCalStep) ? kColorRed : kColorLine;
    drawCalibrationTarget(kTouchCalTargets[i][0], kTouchCalTargets[i][1], color);
  }
  drawFittedText({28, 262, 120, 18},
                 "step " + String(touchCalStep + 1) + " / " + String(kTouchCalPointCount),
                 kColorAccent, kColorPanel, 2, 1);
}

int16_t mapCalibratedAxis(int16_t rawValue, int rawMin, int rawMax, bool invert,
                          int16_t screenMax) {
  if (rawMax == rawMin) {
    return screenMax / 2;
  }

  long mapped = map(rawValue, rawMin, rawMax, 0, screenMax);
  mapped = constrain(mapped, 0, screenMax);
  if (invert) {
    mapped = screenMax - mapped;
  }
  return static_cast<int16_t>(mapped);
}

void mapTouchPoint(int16_t rawX, int16_t rawY, int16_t& x, int16_t& y) {
  if (!touchCal.valid) {
    x = constrain(map(rawX, 0, 480, 0, display.width() - 1), 0, display.width() - 1);
    y = constrain(map(rawY, 0, 320, 0, display.height() - 1), 0, display.height() - 1);
    return;
  }

  const int16_t axisX = touchCal.swapXY ? rawY : rawX;
  const int16_t axisY = touchCal.swapXY ? rawX : rawY;
  x = mapCalibratedAxis(axisX, touchCal.rawMinX, touchCal.rawMaxX, touchCal.invertX,
                        display.width() - 1);
  y = mapCalibratedAxis(axisY, touchCal.rawMinY, touchCal.rawMaxY, touchCal.invertY,
                        display.height() - 1);
}

void finishTouchCalibration() {
  const TP_Point& topLeft = touchCalSamples[0];
  const TP_Point& topRight = touchCalSamples[1];
  const TP_Point& bottomLeft = touchCalSamples[2];
  const TP_Point& bottomRight = touchCalSamples[3];

  const long horizontalByX = labs(topRight.x - topLeft.x) + labs(bottomRight.x - bottomLeft.x);
  const long horizontalByY = labs(topRight.y - topLeft.y) + labs(bottomRight.y - bottomLeft.y);
  touchCal.swapXY = horizontalByY > horizontalByX;

  int leftA;
  int leftB;
  int rightA;
  int rightB;
  int topA;
  int topB;
  int bottomA;
  int bottomB;

  if (touchCal.swapXY) {
    leftA = topLeft.y;
    leftB = bottomLeft.y;
    rightA = topRight.y;
    rightB = bottomRight.y;
    topA = topLeft.x;
    topB = topRight.x;
    bottomA = bottomLeft.x;
    bottomB = bottomRight.x;
  } else {
    leftA = topLeft.x;
    leftB = bottomLeft.x;
    rightA = topRight.x;
    rightB = bottomRight.x;
    topA = topLeft.y;
    topB = topRight.y;
    bottomA = bottomLeft.y;
    bottomB = bottomRight.y;
  }

  const int16_t leftAvg = averageInt(leftA, leftB);
  const int16_t rightAvg = averageInt(rightA, rightB);
  const int16_t topAvg = averageInt(topA, topB);
  const int16_t bottomAvg = averageInt(bottomA, bottomB);

  touchCal.invertX = rightAvg < leftAvg;
  touchCal.invertY = bottomAvg < topAvg;
  touchCal.rawMinX = std::min(leftAvg, rightAvg);
  touchCal.rawMaxX = std::max(leftAvg, rightAvg);
  touchCal.rawMinY = std::min(topAvg, bottomAvg);
  touchCal.rawMaxY = std::max(topAvg, bottomAvg);
  touchCal.valid = true;
  touchCalActive = false;
  currentScreen = ScreenId::Dashboard;
  saveTouchCalibration();

  Serial.print("TOUCH;CAL;swap=");
  Serial.print(touchCal.swapXY ? "1" : "0");
  Serial.print(";invertX=");
  Serial.print(touchCal.invertX ? "1" : "0");
  Serial.print(";invertY=");
  Serial.print(touchCal.invertY ? "1" : "0");
  Serial.print(";minX=");
  Serial.print(touchCal.rawMinX);
  Serial.print(";maxX=");
  Serial.print(touchCal.rawMaxX);
  Serial.print(";minY=");
  Serial.print(touchCal.rawMinY);
  Serial.print(";maxY=");
  Serial.println(touchCal.rawMaxY);

  uiDirty = true;
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
    case ScreenId::TouchCal:
      dashboardScreenLoaded = false;
      drawTouchCalibrationScreen();
      break;
  }
  uiDirty = false;
}

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

uint8_t detectTouchAddress() {
  if (i2cDevicePresent(kGt911PrimaryAddress)) {
    return kGt911PrimaryAddress;
  }
  if (i2cDevicePresent(kGt911SecondaryAddress)) {
    return kGt911SecondaryAddress;
  }
  return 0;
}

void initTouchController() {
  activeTouchAddress = detectTouchAddress();
  Serial.print("TOUCH;SCAN;");
  Serial.print(i2cDevicePresent(kGt911PrimaryAddress) ? "14" : "--");
  Serial.print(";");
  Serial.println(i2cDevicePresent(kGt911SecondaryAddress) ? "5D" : "--");
  if (activeTouchAddress == 0) {
    Serial.println("TOUCH;ADDR;NONE");
    return;
  }
  touchController.begin(activeTouchAddress);
  touchController.setRotation(ROTATION_NORMAL);
  Serial.print("TOUCH;ADDR;");
  Serial.println(activeTouchAddress, HEX);
}

void reportTouchStatus() {
  if (millis() - lastTouchReportMs < 3000) {
    return;
  }
  lastTouchReportMs = millis();
  Serial.print("TOUCH;STATUS;");
  Serial.print("scan14=");
  Serial.print(i2cDevicePresent(kGt911PrimaryAddress) ? "Y" : "N");
  Serial.print(";scan5D=");
  Serial.print(i2cDevicePresent(kGt911SecondaryAddress) ? "Y" : "N");
  Serial.print(";active=");
  if (activeTouchAddress == 0) {
    Serial.println("NONE");
  } else {
    Serial.println(activeTouchAddress, HEX);
  }
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
  const float inlet = tempSensors.getTempCByIndex(0);
  const float middle = tempSensors.getTempCByIndex(1);
  const float outlet = tempSensors.getTempCByIndex(2);

  sensors.probeValid =
      isValidProbeTemp(inlet) && isValidProbeTemp(middle) && isValidProbeTemp(outlet);
  if (isValidProbeTemp(inlet)) sensors.inletC = inlet;
  if (isValidProbeTemp(middle)) sensors.middleC = middle;
  if (isValidProbeTemp(outlet)) sensors.outletC = outlet;

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

void handleDashboardTap(int16_t x, int16_t y) {
  if (pointInRect(x, y, loggingBadgeRect)) {
    currentScreen = ScreenId::LoggingMenu;
  } else if (pointInRect(x, y, wifiBadgeRect)) {
    currentScreen = ScreenId::WifiMenu;
  } else if (pointInRect(x, y, inletTargetRect)) {
    activeTarget = TargetField::Inlet;
    currentScreen = ScreenId::TargetMenu;
  } else if (pointInRect(x, y, outletTargetRect)) {
    activeTarget = TargetField::Outlet;
    currentScreen = ScreenId::TargetMenu;
  } else {
    return;
  }
  uiDirty = true;
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

void handleTouch() {
  if (currentScreen == ScreenId::Dashboard && lvglReady && !touchCalActive) {
    return;
  }
  if (activeTouchAddress == 0) {
    return;
  }
  if (millis() - lastTouchPollMs < kTouchPollMs) {
    return;
  }
  lastTouchPollMs = millis();
  touchController.read();
  if (!touchController.isTouched) {
    if (touchCalActive && touchCalWaitForRelease) {
      touchCalWaitForRelease = false;
    }
    return;
  }
  const int16_t rawX = touchController.points[0].x;
  const int16_t rawY = touchController.points[0].y;
  if (rawX < 0 || rawY < 0) {
    return;
  }

  if (touchCalActive) {
    if (touchCalWaitForRelease) {
      return;
    }
    if (millis() - lastTouchMs < 120) {
      return;
    }
    lastTouchMs = millis();
    currentScreen = ScreenId::TouchCal;
    touchCalSamples[touchCalStep] = touchController.points[0];
    touchCalWaitForRelease = true;
    ++touchCalStep;
    if (touchCalStep >= kTouchCalPointCount) {
      touchCalStep = 0;
      finishTouchCalibration();
    } else {
      uiDirty = true;
    }
    return;
  }

  if (rawX > 480 || rawY > 320) {
    return;
  }
  if (millis() - lastTouchMs < kTouchDebounceMs) {
    return;
  }
  lastTouchMs = millis();
  int16_t x = 0;
  int16_t y = 0;
  mapTouchPoint(rawX, rawY, x, y);

  if (showTouchDebug) {
    lastTouchDebugX = x;
    lastTouchDebugY = y;
    touchDebugLatched = true;
    uiDirty = true;
  }

  if (serialConnected) {
    Serial.print("TOUCH;");
    Serial.print("raw=");
    Serial.print(rawX);
    Serial.print(",");
    Serial.print(rawY);
    Serial.print(";mapped=");
    Serial.print(x);
    Serial.print(";");
    Serial.println(y);
  }

  switch (currentScreen) {
    case ScreenId::Dashboard:
      break;
    case ScreenId::WifiMenu:
      handleWifiMenuTap(x, y);
      break;
    case ScreenId::LoggingMenu:
      handleLoggingMenuTap(x, y);
      break;
    case ScreenId::TargetMenu:
      handleTargetMenuTap(x, y);
      break;
    case ScreenId::TextEditor:
      handleTextEditorTap(x, y);
      break;
    case ScreenId::TouchCal:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(BoardConfig::kRelayHotPin, OUTPUT);
  pinMode(BoardConfig::kRelayColdPin, OUTPUT);
  setRelayHot(false);
  setRelayCold(false);

  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  loadSettings();

  tempSensors.begin();
  Wire.begin(BoardConfig::kI2cSdaPin, BoardConfig::kI2cSclPin);
  sht4.begin();

  display.init();
  display.setRotation(0);
  display.setBrightness(220);
  display.setTextDatum(top_left);
  display.setTextFont(2);
  initTouchController();
  initLvgl();
  lastLvglTickMs = millis();

  if (touchCalActive) {
    currentScreen = ScreenId::TouchCal;
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
  reportTouchStatus();
  ensureWifiConnected();
  updateSensors();
  controlRelays();
  publishFeeds();
  handleTouch();

  if (uiDirty) {
    redrawUi();
  } else if (lvglReady && currentScreen == ScreenId::Dashboard) {
    lv_timer_handler();
  }
}
