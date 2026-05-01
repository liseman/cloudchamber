#pragma once

#include <Arduino.h>

namespace BoardConfig {

// CrowPanel Advance 3.5" v1.3 wiring:
// - I2C-OUT: GPIO16/GPIO15 shared by SHT4x
// - UART1-OUT RX pin: shared 1-Wire bus on GPIO18 for all DS18B20 probes
// - UART1-OUT TX pin: analog light sensor input on GPIO17
// - Wireless-module header: GPIO1 heater relay, GPIO2 cooler relay
constexpr uint8_t kOneWirePin = 18;
constexpr uint8_t kLightPin = 17;
constexpr uint8_t kRelayHotPin = 1;
constexpr uint8_t kRelayColdPin = 2;
constexpr uint8_t kI2cSdaPin = 15;
constexpr uint8_t kI2cSclPin = 16;
constexpr uint8_t kFunctionSelectPin = 45;

// Relay modules vary by board and driver board. Keep the current HIGH = ON
// behavior as the default until we confirm the new hardware.
constexpr bool kRelayActiveHigh = true;

constexpr uint8_t kBacklightPin = 38;
constexpr uint8_t kDisplayCsPin = 40;
constexpr uint8_t kDisplayDcPin = 41;
constexpr uint8_t kDisplaySclkPin = 42;
constexpr uint8_t kDisplayMosiPin = 39;
constexpr uint8_t kTouchIntPin = 47;
constexpr uint8_t kTouchResetPin = 48;

}  // namespace BoardConfig
