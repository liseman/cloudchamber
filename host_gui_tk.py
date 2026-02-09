import threading
import queue
import time
import serial
import serial.tools.list_ports
import tkinter as tk
from tkinter import ttk, messagebox


def list_ports():
    return [p.device for p in serial.tools.list_ports.comports()]


class App:
    def __init__(self, root):
        self.root = root
        root.title('Cloud Chamber Monitor')
        root.geometry('600x400')

        # Main container
        container = ttk.Frame(root)
        container.pack(side="top", fill="both", expand=True, padx=10, pady=10)

        # Connection controls
        conn_frame = ttk.LabelFrame(container, text="Connection", padding=5)
        conn_frame.pack(fill="x", padx=5, pady=5)

        ttk.Label(conn_frame, text="Serial Port:").pack(side="left", padx=5)
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(conn_frame, values=list_ports(), textvariable=self.port_var, width=25)
        self.port_cb.pack(side="left", padx=5)
        ttk.Button(conn_frame, text="Refresh", command=self.refresh_ports).pack(side="left", padx=5)
        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self.toggle_connect)
        self.connect_btn.pack(side="left", padx=5)

        # Sensor readings
        sensor_frame = ttk.LabelFrame(container, text="Sensor Readings", padding=10)
        sensor_frame.pack(fill="both", expand=True, padx=5, pady=5)

        self.vars = {}
        labels = ['Hot End:', 'Middle:', 'Cold End:', 'Air Temp:', 'Air Humidity:', 'Light (raw):']
        for i, label_text in enumerate(labels):
            lbl = ttk.Label(sensor_frame, text=label_text, width=15)
            lbl.grid(row=i, column=0, sticky='w', padx=5, pady=3)
            v = tk.StringVar(value='--')
            val_lbl = ttk.Label(sensor_frame, textvariable=v, font=('Courier', 11))
            val_lbl.grid(row=i, column=1, sticky='w', padx=5, pady=3)
            self.vars[label_text] = v

        # Relay controls
        relay_frame = ttk.LabelFrame(container, text="Relays", padding=10)
        relay_frame.pack(fill="x", padx=5, pady=5)

        self.hot_btn = ttk.Button(relay_frame, text="Toggle Hot Relay", command=self.toggle_hot, state='disabled')
        self.hot_btn.pack(side="left", padx=10, pady=5)
        self.cold_btn = ttk.Button(relay_frame, text="Toggle Cold Relay", command=self.toggle_cold, state='disabled')
        self.cold_btn.pack(side="left", padx=10, pady=5)

        self.ser = None
        self.connected = False
        self.q = queue.Queue()
        self.root.after(100, self.process_queue)

    def refresh_ports(self):
        self.port_cb['values'] = list_ports()

    def toggle_connect(self):
        if not self.connected:
            port = self.port_var.get()
            if not port:
                messagebox.showwarning('Select port', 'Please select a serial port')
                return
            try:
                self.ser = serial.Serial(port, 115200, timeout=0.1)
                time.sleep(0.2)
            except Exception as e:
                messagebox.showerror('Serial error', str(e))
                return
            self.connected = True
            self.connect_btn.config(text='Disconnect')
            self.hot_btn.config(state='normal')
            self.cold_btn.config(state='normal')
            self.read_thread = threading.Thread(target=self.serial_reader, daemon=True)
            self.read_thread.start()
        else:
            self.connected = False
            try:
                if self.ser:
                    self.ser.close()
            except Exception:
                pass
            self.ser = None
            self.connect_btn.config(text='Connect')
            self.hot_btn.config(state='disabled')
            self.cold_btn.config(state='disabled')

    def toggle_hot(self):
        if self.ser:
            try:
                self.ser.write(b'RELAY HOT TOGGLE\n')
            except Exception as e:
                messagebox.showerror('Write error', str(e))

    def toggle_cold(self):
        if self.ser:
            try:
                self.ser.write(b'RELAY COLD TOGGLE\n')
            except Exception as e:
                messagebox.showerror('Write error', str(e))

    def serial_reader(self):
        while self.connected and self.ser:
            try:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    self.q.put(line)
            except Exception:
                pass
            time.sleep(0.02)

    def process_queue(self):
        try:
            while True:
                line = self.q.get_nowait()
                self.parse_line(line)
        except queue.Empty:
            pass
        self.root.after(100, self.process_queue)

    def parse_line(self, line):
        if not line.startswith('SENSORS;'):
            return
        parts = line.strip().split(';')
        values = {}
        for p in parts[1:]:
            if ':' in p:
                k, v = p.split(':', 1)
                values[k] = v
        
        mapping = {
            'HOT': 'Hot End:',
            'MID': 'Middle:',
            'COLD': 'Cold End:',
            'AIR_T': 'Air Temp:',
            'AIR_H': 'Air Humidity:',
            'LIGHT': 'Light (raw):'
        }
        for key, label in mapping.items():
            if key in values:
                self.vars[label].set(values[key])


if __name__ == '__main__':
    root = tk.Tk()
    root.attributes('-topmost', True)  # Force window to front
    app = App(root)
    root.update()  # Force immediate redraw
    root.mainloop()
