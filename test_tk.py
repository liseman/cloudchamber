import tkinter as tk
from tkinter import ttk

root = tk.Tk()
root.title('Test')
root.geometry('300x200')

frame = ttk.Frame(root)
frame.pack()

label = ttk.Label(frame, text='Hello World', font=('Arial', 20))
label.pack(pady=20)

button = ttk.Button(frame, text='Click Me')
button.pack(pady=10)

root.mainloop()
