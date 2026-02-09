import threading
import time
import PySimpleGUI as sg
import serial
import serial.tools.list_ports

# Simple GUI to show sensor readings and control relays over serial

def list_ports():
    return [p.device for p in serial.tools.list_ports.comports()]

layout = [
    [sg.Text('Serial Port:'), sg.Combo(values=list_ports(), key='-PORT-', size=(20,1)), sg.Button('Refresh'), sg.Button('Connect')],
    [sg.HorizontalSeparator()],
    [sg.Text('Hot End:'), sg.Text('', key='-HOT-')],
    [sg.Text('Middle:'), sg.Text('', key='-MID-')],
    [sg.Text('Cold End:'), sg.Text('', key='-COLD-')],
    [sg.Text('Air Temp:'), sg.Text('', key='-AT-')],
    [sg.Text('Air Humidity:'), sg.Text('', key='-AH-')],
    [sg.Text('Light (raw):'), sg.Text('', key='-LIGHT-')],
    [sg.HorizontalSeparator()],
    [sg.Button('Toggle Hot Relay', key='-BTN_HOT-', disabled=True), sg.Button('Toggle Cold Relay', key='-BTN_COLD-', disabled=True)],
    [sg.Button('Exit')]
]

window = sg.Window('Cloud Chamber Monitor', layout)

ser = None
connected = False
relay_hot = False
relay_cold = False


def open_serial(port):
    try:
        s = serial.Serial(port, 115200, timeout=0.1)
        time.sleep(0.2)
        return s
    except Exception as e:
        sg.popup('Failed to open serial:\n' + str(e))
        return None


def parse_line(line):
    global relay_hot, relay_cold
    # expected form: SENSORS;HOT:xx.xx;MID:yy.yy;COLD:zz.zz;AIR_T:...;AIR_H:...;LIGHT:nnn;RHOT:ON;RCOLD:OFF
    if not line.startswith('SENSORS;'):
        return
    parts = line.strip().split(';')
    values = {}
    for p in parts[1:]:
        if ':' in p:
            k,v = p.split(':',1)
            values[k] = v
    if 'HOT' in values:
        window['-HOT-'].update(values['HOT'])
    if 'MID' in values:
        window['-MID-'].update(values['MID'])
    if 'COLD' in values:
        window['-COLD-'].update(values['COLD'])
    if 'AIR_T' in values:
        window['-AT-'].update(values['AIR_T'])
    if 'AIR_H' in values:
        window['-AH-'].update(values['AIR_H'])
    if 'LIGHT' in values:
        window['-LIGHT-'].update(values['LIGHT'])
    if 'RHOT' in values:
        relay_hot = (values['RHOT'].upper() == 'ON')
        window['-BTN_HOT-'].update('Toggle Hot Relay ({})'.format('ON' if relay_hot else 'OFF'))
    if 'RCOLD' in values:
        relay_cold = (values['RCOLD'].upper() == 'ON')
        window['-BTN_COLD-'].update('Toggle Cold Relay ({})'.format('ON' if relay_cold else 'OFF'))


def serial_reader():
    global ser, connected
    while connected and ser:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                parse_line(line)
        except Exception:
            pass
        time.sleep(0.05)

while True:
    event, values = window.read(timeout=100)
    if event == sg.WIN_CLOSED or event == 'Exit':
        break
    if event == 'Refresh':
        window['-PORT-'].update(values=list_ports())
    if event == 'Connect':
        if not connected:
            port = values['-PORT-']
            if not port:
                sg.popup('Select a serial port first')
                continue
            ser = open_serial(port)
            if ser:
                connected = True
                window['-BTN_HOT-'].update(disabled=False)
                window['-BTN_COLD-'].update(disabled=False)
                threading.Thread(target=serial_reader, daemon=True).start()
                window['Connect'].update('Disconnect')
        else:
            connected = False
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            window['-BTN_HOT-'].update(disabled=True)
            window['-BTN_COLD-'].update(disabled=True)
            window['Connect'].update('Connect')
    if event == '-BTN_HOT-' and connected and ser:
        try:
            ser.write(b"RELAY HOT TOGGLE\n")
        except Exception as e:
            sg.popup('Serial write failed:\n' + str(e))
    if event == '-BTN_COLD-' and connected and ser:
        try:
            ser.write(b"RELAY COLD TOGGLE\n")
        except Exception as e:
            sg.popup('Serial write failed:\n' + str(e))

window.close()
