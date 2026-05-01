# Installation

## Prerequisites

- PlatformIO
- Python 3.7+
- USB cable for the CrowPanel
- Adafruit IO account

Install PlatformIO from the CLI if needed:

```bash
pip install platformio
```

## Clone

```bash
git clone https://github.com/liseman/cloudchamber.git
cd cloudchamber
git checkout v2
```

## Configure Credentials

Copy the template:

```bash
copy include\secrets.example.h include\secrets.h
```

Then edit:

- `WIFI_SSID`
- `WIFI_PASS`
- `AIO_USER`
- `AIO_KEY`

Current firmware reads credentials from `include/secrets.h`, not from `src/main.cpp`.

## Hardware Setup

Wire the current v2 hardware as follows:

- DS18B20 shared bus to `GPIO18`
- ALS-PT19 analog output to `GPIO17`
- SHT4x SDA to `GPIO15`
- SHT4x SCL to `GPIO16`
- heater relay control to `GPIO1`
- cooler relay control to `GPIO2`

Important notes:

- Use a `4.7k` pull-up from the shared DS18B20 data line to `3.3V`
- Share ground between all sensors and the CrowPanel
- `GPIO45` is driven low by firmware at boot to select wireless-module mode

## Build

```bash
platformio run
```

## Upload

```bash
platformio run --target upload
```

Or specify the port:

```bash
platformio run --target upload --upload-port COM31
```

## Monitor

```bash
platformio device monitor --baud 115200
```

## What to Expect

On the current v2 firmware:

- the CrowPanel boots directly into the LVGL dashboard
- touch is not part of the active branch
- WiFi and logging status are shown on-screen
- DS18B20 rows show temperatures, `missing`, or `read err`

## Adafruit IO

Create these feeds if needed:

- `hot`
- `mid`
- `cold`
- `air-temp`
- `air-humidity`
- `light`
