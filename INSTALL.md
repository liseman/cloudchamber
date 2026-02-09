# Installation & Configuration Guide

## Prerequisites

Before getting started, ensure you have:
- PlatformIO Core or PlatformIO IDE for VS Code
- Python 3.7 or higher
- Git
- USB cable for ESP32 programming
- Adafruit IO account (free at https://io.adafruit.com)

## Step 1: Install PlatformIO

### Option A: VS Code Extension (Recommended)
1. Install VS Code: https://code.visualstudio.com
2. Open VS Code and go to Extensions (Cmd+Shift+X on Mac)
3. Search for "PlatformIO IDE"
4. Install the official extension by PlatformIO

### Option B: Command Line
```bash
pip install platformio
```

## Step 2: Clone the Repository

```bash
git clone https://github.com/yourusername/cloudchamber.git
cd cloudchamber
```

## Step 3: Configure Credentials

### WiFi Configuration
1. Open `src/main.cpp`
2. Find the WiFi credential lines (around line 20):
```cpp
const char* WIFI_SSID = "cult";
const char* WIFI_PASS = "hereticality";
```
3. Replace with your actual WiFi SSID and password

### Adafruit IO Configuration
1. Get your credentials from https://io.adafruit.com
2. Click "My Key" in the top-right corner
3. Copy your Username and Key
4. In `src/main.cpp`, find lines around line 23-24:
```cpp
const char* AIO_KEY = "your-key-here";
const char* AIO_USER = "your-username-here";
```
5. Replace with your actual credentials

## Step 4: Hardware Setup

Connect your sensors to the ESP32 according to the pinout in README.md:

### OneWire Temperature Sensors
- Hot sensor: GPIO 5
- Cold sensor: GPIO 6
- Middle sensor: GPIO 9
- GND: Ground
- VCC: 3.3V

Use a 4.7kΩ pull-up resistor on the data line.

### SHT4x Air Sensor
- SDA: GPIO 21
- SCL: GPIO 20
- GND: Ground
- VCC: 3.3V

Use 4.7kΩ pull-up resistors on both SDA and SCL.

### Light Sensor
- Signal: A2 (GPIO 26)
- GND: Ground
- VCC: 3.3V

### Relays
- Hot Relay: GPIO 10
- Cold Relay: GPIO 11
- GND: Ground
- Control voltage: 3.3V

## Step 5: Build and Upload

### Using VS Code PlatformIO
1. Open the project folder in VS Code
2. Look for the PlatformIO icon in the left sidebar
3. Click "Upload" under the current environment

### Using Command Line
```bash
# Build only
platformio run

# Build and upload
platformio run --target upload

# Clean build
platformio run --target clean
```

## Step 6: Monitor Serial Output

### Using VS Code PlatformIO
1. Click the "Serial Monitor" button in the PlatformIO toolbar
2. Set baud rate to 115200 if not automatic

### Using Command Line
```bash
platformio device monitor --baud 115200
```

## Step 7: Set Up Adafruit IO

### Create Feeds
The firmware automatically tries to send to these feeds. Create them manually if needed:
1. Go to https://io.adafruit.com/feeds
2. Click "Create a New Feed" for each:
   - `hot`
   - `mid`
   - `cold`
   - `air-temp`
   - `air-humidity`
   - `light`

### Create Dashboard
1. Go to https://io.adafruit.com/dashboards
2. Click "Create a New Dashboard"
3. Add "Line Chart" blocks for each feed
4. Each chart will show temperature/humidity/light data over time

## Step 8: Verify Operation

1. **Check Serial Output**: You should see:
   ```
   Connecting to WiFi....
   WiFi connected!
   IP: 192.168.1.100
   READY
   SENSORS;HOT:20.87;MID:20.94;COLD:19.87;AIR_T:NaN;AIR_H:NaN;LIGHT:301;RHOT:OFF;RCOLD:OFF
   ```

2. **Check LED**: Should pulse green when WiFi is connected and data is being sent

3. **Check Adafruit IO**: Visit your dashboard to see data appearing

## Troubleshooting

### Upload Fails - "No device found"
- Check USB cable connection
- Try a different USB port
- Use `platformio device list` to find the port
- Specify the port manually: `platformio run --target upload --upload-port /dev/cu.usbmodem1101`

### WiFi Connection Fails
- Double-check SSID and password in main.cpp
- Verify WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check serial output for error messages
- Try manual WiFi reset by power cycling

### No Sensor Readings
- Verify pinout matches hardware connections
- Check I2C pull-up resistors on SDA/SCL (should be 4.7kΩ)
- Try power cycling the ESP32
- Monitor serial output for specific sensor errors

### Adafruit IO Not Updating
- Verify feed names match exactly
- Check API key and username are correct
- Monitor serial output for HTTP errors
- Try manual feed creation in Adafruit IO web interface

## Development Tips

### Enable Debug Serial Output
All debug messages are already sent to Serial. Connect via serial monitor to see:
- WiFi connection status
- Sensor readings
- Adafruit IO send confirmations/errors

### Modify Sensor Reading Interval
In `src/main.cpp`, change:
```cpp
const unsigned long READ_INTERVAL = 2000; // milliseconds
```

### Modify Adafruit IO Send Interval
In `src/main.cpp`, change:
```cpp
const unsigned long SEND_INTERVAL = 60000; // milliseconds
```

### Add New Sensors
1. Add pin definition at top of main.cpp
2. Initialize in setup()
3. Read in loop()
4. Add to Serial output
5. Add Adafruit IO feed and send code

## Next Steps

- Set up GitHub Actions for automated builds
- Add OTA (Over-the-Air) firmware updates
- Create mobile app for Adafruit IO integration
- Add data logging to microSD card
- Implement threshold-based alerts
