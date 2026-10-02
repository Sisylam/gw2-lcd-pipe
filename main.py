import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

import render
import state


SCALE = 3
REFRESH_SEC = 0.25


def main():
    root = tk.Tk()
    root.title("GW2 LCD mock - 320x240")
    root.resizable(False, False)

    label = ttk.Label(root, borderwidth=0)
    label.pack(padx=4, pady=4)

    root.update_idletasks()
    photo = tk.PhotoImage(data=render.render_char_panel(state.get_state()).to_ppm(SCALE), format="PPM")
    label.configure(image=photo)
    label.image = photo
    root.geometry(f"{photo.width() + 18}x{photo.height() + 18}")

    stop_flag = threading.Event()

    def loop():
        while not stop_flag.is_set():
            state.refresh()
            fb = render.render_char_panel(state.get_state())
            try:
                img = tk.PhotoImage(data=fb.to_ppm(SCALE), format="PPM")
                label.configure(image=img)
                label.image = img
                root.update()
            except tk.TclError:
                break
            time.sleep(REFRESH_SEC)

    threading.Thread(target=loop, daemon=True).start()

    def on_close():
        stop_flag.set()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)
    root.mainloop()


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)
