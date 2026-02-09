#!/usr/bin/env python3
import serial
import time
import sys
import os
import requests

# Configuration
PORT = os.environ.get('SENSOR_PORT', '/dev/cu.usbmodem1301')
BAUD = int(os.environ.get('SENSOR_BAUD', '115200'))
ADAFRUIT_IO_USERNAME = 'liseman'
ADAFRUIT_IO_KEY = '4c06ce1666504628a241f07107012585'
SEND_INTERVAL = 60  # seconds
BASE_URL = f'https://io.adafruit.com/api/v2/{ADAFRUIT_IO_USERNAME}/feeds'

# Feed names
FEEDS = {
    'HOT': 'hot',
    'MID': 'mid',
    'COLD': 'cold',
    'AIR_T': 'air-temp',
    'AIR_H': 'air-humidity',
    'LIGHT': 'light'
}

def send_to_adafruit(feed_name, value):
    """Send a value to an Adafruit IO feed"""
    try:
        url = f'{BASE_URL}/{feed_name}/data'
        headers = {'X-AIO-Key': ADAFRUIT_IO_KEY}
        data = {'value': float(value)}
        response = requests.post(url, json=data, headers=headers, timeout=5)
        if response.status_code in [200, 201]:
            return True
        else:
            print(f'[ERROR] HTTP {response.status_code}: {response.text}')
            return False
    except Exception as e:
        print(f'[ERROR] Failed to send {feed_name}={value}: {e}')
        return False

try:
    s = serial.Serial(PORT, BAUD, timeout=1)
    print(f'[INFO] Connected to {PORT} at {BAUD} baud')
    sys.stdout.flush()
    
    last_send = 0
    last_readings = {}
    
    while True:
        try:
            line = s.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f'[SERIAL] {line}')
                sys.stdout.flush()
                
                # Parse sensor data
                if line.startswith('SENSORS;'):
                    parts = line.split(';')[1:]
                    readings = {}
                    for p in parts:
                        if ':' in p:
                            k, v = p.split(':', 1)
                            readings[k] = v
                    
                    # Store latest readings
                    last_readings = readings
                    
                    # Send to Adafruit IO once per minute
                    current_time = time.time()
                    if current_time - last_send >= SEND_INTERVAL:
                        print(f'[INFO] Sending to Adafruit IO...')
                        sys.stdout.flush()
                        
                        try:
                            for sensor_key, feed_name in FEEDS.items():
                                if sensor_key in last_readings:
                                    value = last_readings[sensor_key]
                                    # Skip NaN values
                                    if value.lower() != 'nan':
                                        if send_to_adafruit(feed_name, value):
                                            print(f'[SUCCESS] Sent {feed_name}={value}')
                                            sys.stdout.flush()
                            
                            last_send = current_time
                        except Exception as e:
                            print(f'[ERROR] Failed to send to Adafruit IO: {e}')
                            sys.stdout.flush()
        
        except Exception as e:
            print(f'[SERIAL ERROR] {e}')
            sys.stdout.flush()
            time.sleep(0.5)

except Exception as e:
    print(f'[OPEN ERROR] {e}')
    sys.exit(1)
