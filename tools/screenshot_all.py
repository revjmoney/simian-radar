"""Taps through every mode over USB serial ('n') and saves a contact sheet of the OLED screens.

Usage: python -I tools/screenshot_all.py sheet.png [COM11] [modes=16]
"""
import sys
import time

import serial
from PIL import Image, ImageDraw


def grab(s):
    s.reset_input_buffer()
    s.write(b"d")
    end = time.time() + 3
    while time.time() < end:
        line = s.readline().decode(errors="replace").strip()
        if line.startswith("FB:") and len(line) == 3 + 2048:
            fb = bytes.fromhex(line[3:])
            img = Image.new("1", (128, 64))
            for page in range(8):
                for x in range(128):
                    for bit in range(8):
                        if fb[page * 128 + x] >> bit & 1:
                            img.putpixel((x, page * 8 + bit), 1)
            return img.rotate(90, expand=True)
    raise SystemExit("no framebuffer received")


def main():
    out = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else "COM11"
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    s = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(0.5)
    shots = []
    for i in range(count):
        s.write(b"n")
        time.sleep(0.35)
        if i == 0:
            shots.append(grab(s))  # one example of the mode banner
        time.sleep(2.5)            # let the banner go and the list fill
        shots.append(grab(s))
    scale, pad, cols = 3, 6, 9
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("L", (cols * (64 * scale + pad) + pad, rows * (128 * scale + pad) + pad), 90)
    for i, img in enumerate(shots):
        big = img.convert("L").resize((64 * scale, 128 * scale), Image.NEAREST)
        sheet.paste(big, (pad + (i % cols) * (64 * scale + pad), pad + (i // cols) * (128 * scale + pad)))
    sheet.save(out)
    print(out, len(shots), "screens")


if __name__ == "__main__":
    main()
