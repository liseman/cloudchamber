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
- ADS1115 support was removed from the current hardware plan.
- The firmware now includes a CrowPanel-specific LovyanGFX display driver, GT911 touch support, and an LVGL dashboard/menu flow.

## Next steps

1. Finish display color and styling cleanup so the LVGL dashboard matches the shipped Elecrow demo more closely.
2. Keep improving GT911 touch behavior and calibration persistence on the CrowPanel hardware.
3. Confirm DS18B20 probe order on the shared bus, or switch to address-based labeling if the physical order matters.
4. Verify the installed relay modules match the current `HIGH = ON` assumption.
