# Cloud Chamber V2

`cloudchamber-v2` is the current hardware port of the cloud chamber monitor for the Elecrow CrowPanel Advance 3.5 (ESP32-S3, 480x320 display).

The firmware reads:
- 3x DS18B20 probe temperatures
- 1x SHT4x air temperature / humidity sensor
- 1x ALS-PT19 light sensor

It also:
- drives 2 relay outputs
- publishes data to Adafruit IO over WiFi
- renders a local LVGL dashboard on the CrowPanel screen

## Current Hardware Target

- Board: Elecrow CrowPanel Advance 3.5
- Display: 480x320 integrated TFT
- Framework: Arduino via PlatformIO
- UI: LVGL

Touch support was removed from the active firmware branch for now. The current UI is display-only.

## Wiring

Current v2 wiring assumptions:

| Function | Pin |
|---|---|
| DS18B20 shared 1-Wire bus | `GPIO18` |
| ALS-PT19 analog light sensor | `GPIO17` |
| SHT4x SDA | `GPIO15` |
| SHT4x SCL | `GPIO16` |
| Heater relay | `GPIO1` |
| Cooler relay | `GPIO2` |
| Wireless-module function select | `GPIO45` |

Notes:
- `GPIO45` is driven low at boot to put the board in wireless-module mode.
- The wireless module header itself already exposes power rails; the firmware selects the module path rather than creating a new rail.
- DS18B20 sensors should share one bus on `GPIO18` with a `4.7k` pull-up from data to `3.3V`.

## Firmware Behavior

### Dashboard

The on-device dashboard shows:
- logging status
- WiFi status
- air temperature
- relative humidity
- inlet probe temperature
- middle probe temperature
- outlet probe temperature
- light sensor value
- heater / cooler relay state

The dashboard currently uses:
- larger LVGL text with Montserrat 22/24 enabled
- explicit red / yellow / green status colors for WiFi and logging
- a single aligned sensor-value column

### DS18B20 Status

Probe rows display:
- a live temperature when valid
- `missing` when that probe index is not found on the 1-Wire bus
- `read err` when a probe is present but returns an invalid reading

## Credentials

Local credentials live in:
- `include/secrets.h`

Template credentials live in:
- `include/secrets.example.h`

`include/secrets.h` is gitignored.

## Build

The active PlatformIO environment is:

```ini
[env:crowpanel_advance_3_5]
```

Build:

```bash
platformio run
```

Upload:

```bash
platformio run --target upload
```

Upload to a specific port:

```bash
platformio run --target upload --upload-port COM31
```

Serial monitor:

```bash
platformio device monitor --baud 115200
```

## Adafruit IO

The firmware publishes to these feeds:
- `hot`
- `mid`
- `cold`
- `air-temp`
- `air-humidity`
- `light`

## Repository Notes

- `src/main.cpp`: main CrowPanel firmware
- `include/board_config.h`: board-specific pin mapping
- `lv_conf.h` and `include/lv_conf.h`: LVGL font/config toggles
- `V2_NOTES.md`: short running notes for this hardware revision

## Status

This branch is the current v2 bring-up branch and reflects the CrowPanel-specific UI and wiring changes rather than the original Feather-based build.
