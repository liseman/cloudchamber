# Cloud Chamber - GitHub Push Instructions

Your local git repository is ready! Here's how to push it to GitHub:

## Step 1: Create Repository on GitHub

1. Go to https://github.com/new
2. Name it: `cloudchamber`
3. Add description: "Autonomous IoT cloud chamber monitoring with ESP32 and Adafruit IO"
4. Choose: Public (for easy sharing)
5. **Do NOT** initialize with README (we already have one)
6. Click "Create repository"

## Step 2: Push to GitHub

Copy and paste these commands in the cloudchamber folder:

```bash
cd /Users/lukeiseman/Documents/PlatformIO/Projects/cloudchamber

git branch -M main

git remote add origin https://github.com/YOUR_USERNAME/cloudchamber.git

git push -u origin main
```

Replace `YOUR_USERNAME` with your actual GitHub username.

## Step 3: Add OAuth Token (if needed)

If you get an authentication error:

1. Go to https://github.com/settings/tokens
2. Click "Generate new token (classic)"
3. Select scopes: `repo`
4. Generate and copy the token
5. When prompted for password during git push, paste the token instead

## Step 4: Verify Upload

Go to https://github.com/YOUR_USERNAME/cloudchamber to verify all files are there.

## Project Structure on GitHub

```
cloudchamber/
├── src/
│   └── main.cpp              # ESP32 firmware with WiFi and Adafruit IO
├── include/                  # Arduino include files
├── lib/                      # Arduino libraries
├── platformio.ini            # PlatformIO configuration
├── adafruit_io_sender.py     # Python data sender (alternative)
├── serial_echo.py            # Serial monitor utility
├── host_gui_web.py           # Legacy Flask web interface
├── README.md                 # Main documentation
├── INSTALL.md                # Installation guide
├── LICENSE                   # MIT License
└── .gitignore               # Git ignore patterns
```

## Repository Contents

This project includes:

- ✅ **ESP32 Firmware**: Full WiFi and Adafruit IO integration
- ✅ **Sensor Support**: Temperature (DS18B20), Humidity/Temp (SHT4x), Light (ALS-PT19)
- ✅ **Relay Control**: 2x controllable relays via serial commands
- ✅ **Documentation**: Installation, pinout, troubleshooting
- ✅ **Python Tools**: Serial monitoring and data sending utilities
- ✅ **MIT License**: Free for personal and commercial use

## Future GitHub Features

Consider adding:

1. **GitHub Pages**: Host documentation at `username.github.io/cloudchamber`
2. **GitHub Actions**: Auto-build and test firmware on every push
3. **Releases**: Create tagged releases for stable versions
4. **Wiki**: Add assembly guide, calibration instructions, etc.
5. **Issues**: Track feature requests and bugs
6. **Discussions**: Community support and ideas

## Share Your Project

Once on GitHub, you can share:
- Direct link: `https://github.com/YOUR_USERNAME/cloudchamber`
- Clone command: `git clone https://github.com/YOUR_USERNAME/cloudchamber.git`
- Add to README badges for build status, license, etc.

## Example GitHub README Badge

Add this to your GitHub README.md for a nice link to Adafruit IO:

```markdown
[![Adafruit IO](https://img.shields.io/badge/Adafruit%20IO-Cloud%20Dashboard-blueviolet)](https://io.adafruit.com/liseman/dashboards/cloud1)
```

Enjoy your cloud chamber project! 🎉
