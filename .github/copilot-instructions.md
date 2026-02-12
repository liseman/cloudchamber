# Copilot instructions

## Project overview
- PlatformIO-based ESP32-S3 firmware in [src/main.cpp](src/main.cpp) reads DS18B20 (hot/mid/cold), SHT4x, and a light sensor, then prints a semicolon-delimited `SENSORS;...` line over serial.
- Data is pushed to Adafruit IO every 60s via REST; WiFi + credentials are hardcoded in [src/main.cpp](src/main.cpp).
- Host-side GUIs (optional) parse the serial line and send relay commands back:
  - PySimpleGUI: [host_gui.py](host_gui.py)
  - Tkinter: [host_gui_tk.py](host_gui_tk.py)
  - Flask web UI: [host_gui_web.py](host_gui_web.py)

## Serial protocol + relay control
- Serial output format (firmware): `SENSORS;HOT:..;MID:..;COLD:..;AIR_T:..;AIR_H:..;LIGHT:..;RHOT:ON/OFF;RCOLD:ON/OFF`.
- Relay commands accepted by firmware: `RELAY HOT ON|OFF|TOGGLE` and `RELAY COLD ON|OFF|TOGGLE` (case-insensitive).
- Relay active state is HIGH = ON; flip in [src/main.cpp](src/main.cpp) if using active-low boards (see README note).

## Key firmware timing loops
- Sensor read cadence: every 2s; upload cadence: every 60s.
- LED status color pulses reflect sensor validity + upload success.

## Build and run workflows
- Build/upload firmware: `platformio run --target upload`.
- Serial monitor: `platformio device monitor --baud 115200`.
- PlatformIO env is `adafruit_feather_esp32s3` in [platformio.ini](platformio.ini).

## Patterns to follow when editing
- Keep serial output a single-line `SENSORS;` record so host GUIs can parse; see parsers in [host_gui.py](host_gui.py) and [host_gui_tk.py](host_gui_tk.py).
- When adding new sensor fields, update both the firmware format and GUI parsing maps.
- Adafruit IO feed mapping arrays live in [src/main.cpp](src/main.cpp) (`FEEDS`, `FEED_KEYS`, `NUM_FEEDS`).
