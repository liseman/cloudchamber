# Cloud Chamber IoT Monitor

An autonomous IoT cloud chamber monitoring system powered by ESP32, with real-time sensor data streaming to Adafruit IO.

## Overview

This project implements a complete cloud chamber environmental monitoring system that:
- **Reads 6 sensor values** in real-time (hot/mid/cold temperatures, air temperature, humidity, light)
- **Transmits data via WiFi** every 60 seconds to Adafruit IO
- **Works autonomously** without requiring a connected computer
- **Visual feedback** with pulsing green LED when connected and sending data
- **Serial output** for debugging and local monitoring
- **Web dashboard** integration with Adafruit IO for historical data visualization

## Hardware

- **Microcontroller**: Adafruit Feather ESP32-S3
- **Temperature Sensors**: 
  - 3x DS18B20 (OneWire) - Hot end, Middle, Cold end
- **Environmental Sensor**: SHT4x (I2C) - Air temperature & humidity
- **Light Sensor**: ALS-PT19 analog sensor on pin A2
- **Relays**: 2x control relays for hot/cold elements
- **LED**: Onboard GPIO 13 for status indication

## Pinout

| Component | Pin |
|-----------|-----|
| DS18B20 (Hot) | GPIO 5 |
| DS18B20 (Cold) | GPIO 6 |
| DS18B20 (Mid) | GPIO 9 |
| Relay Hot | GPIO 10 |
| Relay Cold | GPIO 11 |
| Light Sensor (ALS-PT19) | A2 (ADC) |
| Status LED | GPIO 13 |
| I2C SDA | GPIO 21 (SHT4x) |
| I2C SCL | GPIO 20 (SHT4x) |

## Features

### Sensor Readings
- **HOT**: Hot end temperature (°C)
- **MID**: Middle temperature (°C)
- **COLD**: Cold end temperature (°C)
- **AIR_T**: Ambient air temperature (°C)
- **AIR_H**: Ambient air humidity (%)
- **LIGHT**: Light intensity (0-4095 raw ADC)

### WiFi Connectivity
- Automatic WiFi connection at startup
- WiFi status monitoring
- Graceful fallback if WiFi unavailable

### Adafruit IO Integration
- REST API-based data transmission
- Automatic feed creation
- 60-second send interval
- Error handling with serial logging

### LED Status
- **Pulsing Green**: Connected to WiFi + successfully sending data
- **Off**: Disconnected or send failed

### Serial Output
Format: `SENSORS;HOT:XX.XX;MID:XX.XX;COLD:XX.XX;AIR_T:XX.XX;AIR_H:XX.XX;LIGHT:XXXX;RHOT:ON/OFF;RCOLD:ON/OFF`

Example:
```
SENSORS;HOT:20.87;MID:20.94;COLD:19.87;AIR_T:NaN;AIR_H:NaN;LIGHT:301;RHOT:OFF;RCOLD:OFF
```

## Quick Start

### Prerequisites
- PlatformIO (install via `pip install platformio`)
- Python 3.7+
- USB cable for ESP32 programming
- WiFi network credentials

### Installation

1. **Clone the repository**
```bash
git clone https://github.com/yourusername/cloudchamber.git
cd cloudchamber
```

2. **Configure WiFi Credentials**
Edit `src/main.cpp` and update:
```cpp
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
```

3. **Configure Adafruit IO**
Update your credentials in `src/main.cpp`:
```cpp
const char* AIO_KEY = "your-aio-key";
const char* AIO_USER = "your-aio-username";
```

4. **Build and Upload**
```bash
platformio run --target upload
```

Or use VS Code with PlatformIO extension and click "Upload".

5. **Monitor Serial Output**
```bash
platformio device monitor --baud 115200
```

## Adafruit IO Setup

### Create Feeds
The following feeds are automatically created/used:
- `hot` - Hot end temperature
- `mid` - Middle temperature
- `cold` - Cold end temperature
- `air-temp` - Ambient temperature
- `air-humidity` - Ambient humidity
- `light` - Light sensor reading

### Create Dashboard
1. Go to [Adafruit IO Dashboard](https://io.adafruit.com/dashboards)
2. Create a new dashboard
3. Add Line Chart blocks for each feed to visualize data over time

### Get API Key
1. Log in to [Adafruit IO](https://io.adafruit.com)
2. Click "My Key" in the top-right
3. Copy your API key and username

## Data Format

### Serial Protocol
All readings are output as semicolon-delimited strings every 2 seconds:
```
SENSORS;KEY1:VALUE1;KEY2:VALUE2;...;KEYN:VALUEN
```

### Adafruit IO REST API
Data is sent as JSON POST requests to:
```
https://io.adafruit.com/api/v2/{USERNAME}/feeds/{FEED_NAME}/data
```

With payload:
```json
{"value": 20.87}
```

## Serial Commands

Send these via serial monitor to control relays:
```
RELAY HOT ON
RELAY HOT OFF
RELAY HOT TOGGLE
RELAY COLD ON
RELAY COLD OFF
RELAY COLD TOGGLE
```

## Troubleshooting

### WiFi Not Connecting
- Check SSID and password in `main.cpp`
- Verify ESP32 is in range of WiFi network
- Check serial output for connection errors

### Adafruit IO Not Receiving Data
- Verify API key and username are correct
- Check WiFi connection (LED should be pulsing)
- Monitor serial output for HTTP errors
- Ensure feeds exist in Adafruit IO

### Temperature Sensors Not Reading
- Verify OneWire sensors are wired correctly
- Check pin configuration matches pinout table
- Try running sensor test via serial command

### SHT4x Shows NaN
- Verify I2C wiring (SDA/SCL)
- Check pull-up resistors (typically 4.7k ohms)
- Confirm sensor is powered correctly
- Try power cycle

## Software Architecture

### Main Loop Cycle
1. **Every 2 seconds**: Read all sensors and output to serial
2. **Every 60 seconds**: Send data to Adafruit IO (if WiFi connected)
3. **Continuous**: Monitor LED status, handle serial commands

### Libraries Used
- **OneWire** - DS18B20 temperature sensors
- **DallasTemperature** - Temperature reading
- **Adafruit_SHT4x** - Air temperature/humidity
- **WiFi** - ESP32 WiFi connectivity
- **ArduinoHttpClient** - HTTP/REST requests
- **Wire** - I2C communication

## Files

```
cloudchamber/
├── src/
│   └── main.cpp              # ESP32 firmware
├── platformio.ini            # PlatformIO configuration
├── adafruit_io_sender.py     # Alternative Python data sender
├── serial_echo.py            # Serial monitoring utility
├── host_gui_web.py           # Legacy web interface
└── README.md                 # This file
```

## Power Consumption

- **WiFi Idle**: ~80mA
- **WiFi Transmit**: ~150mA
- **Sensor Reading**: ~10mA
- **LED Pulsing**: ~3mA

Typical average: ~100mA @ 3.3V

## Future Enhancements

- [ ] MQTT support for Adafruit IO
- [ ] Data logging to SD card
- [ ] Web-based configuration interface
- [ ] OTA (Over-the-Air) firmware updates
- [ ] Threshold-based alerts

## License

MIT License

## Author

Luke Iseman

## Support

For issues and questions:
1. Check troubleshooting section above
2. Monitor serial output for error messages
3. Verify hardware connections
4. Review Adafruit IO API documentation

## References

- [ESP32 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Adafruit IO API](https://io.adafruit.com/api/docs/)
- [PlatformIO Documentation](https://docs.platformio.org/)
- [DS18B20 Datasheet](https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf)
- [SHT4x Datasheet](https://sensirion.com/products/catalog/SHT40/)

**Firmware (PlatformIO):**
- OneWire
- DallasTemperature (milesburton)
- Adafruit SHT4x Library
- Adafruit BusIO

**Host GUI:**
- Flask
- pyserial

Notes

- Relay active state is HIGH = ON; modify `src/main.cpp` if your relay module is active-low.
- The web server runs on port 8888 by default.
- Both sensor monitor (Serial) and web GUI can run simultaneously.
