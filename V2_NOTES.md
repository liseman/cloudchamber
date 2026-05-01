# Cloudchamber V2 Notes

This clone is the local starting point for the next hardware revision.

## What changed

- Board-specific pin assignments now live in `include/board_config.h`.
- WiFi and Adafruit IO credentials now live in `include/secrets.h`.
- `include/secrets.h` is ignored by git, and `include/secrets.example.h` is the checked-in template.
- The local baseline now targets the Elecrow CrowPanel Advance 3.5 hardware.
- Current wiring assumptions:
  - heater relay on GPIO1
  - cooler relay on GPIO2
  - shared DS18B20 1-Wire bus on GPIO18
  - ALS-PT19 analog light sensor on GPIO17
  - SHT4x on I2C GPIO15/16
- The CrowPanel function-select line on GPIO45 is now driven low at boot to enable wireless-module mode on the module header.
- ADS1115 support was removed from the current hardware plan.
- The firmware now includes a CrowPanel-specific LovyanGFX display driver and an LVGL dashboard/menu flow tuned for the 480x320 display.
- GT911 touch support and calibration code were removed from the current firmware branch so the display stack is dashboard-only for now.
- The dashboard layout has been iterated on-device to improve contrast, row alignment, larger text, consistent spacing, and explicit red/yellow/green WiFi and logging status text.
- LVGL font config now enables Montserrat 22 and 24 so the dashboard can use larger text without custom font assets.
- DS18B20 rows now show explicit state on-screen:
  - live temperature when valid
  - `missing` when a probe index is not found on the 1-Wire bus
  - `read err` when a probe is present but the reading is invalid

## Next steps

1. Confirm the latest on-device layout is final after one more physical-screen review pass.
2. Confirm DS18B20 probe order on the shared bus, or switch to address-based labeling if the physical order matters.
3. Verify the wireless-module header is now powered and in the expected mode with GPIO45 held low.
4. Verify the installed relay modules match the current `HIGH = ON` assumption.
