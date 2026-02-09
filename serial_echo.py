#!/usr/bin/env python3
import serial, time, sys, os

port = os.environ.get('SENSOR_PORT', '/dev/cu.usbmodem1301')
baud = int(os.environ.get('SENSOR_BAUD', '115200'))

try:
    s = serial.Serial(port, baud, timeout=1)
    print('SERIAL ECHO STARTED', port)
    sys.stdout.flush()
    while True:
        try:
            line = s.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)
                sys.stdout.flush()
        except Exception as e:
            print('SERIAL ERR', e)
            sys.stdout.flush()
            time.sleep(0.5)
except Exception as e:
    print('OPEN ERR', e)
    sys.exit(1)
