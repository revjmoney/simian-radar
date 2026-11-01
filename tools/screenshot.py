"""Grabs the Heltec OLED framebuffer over USB serial and saves it as a PNG (portrait, 4x scale).

Usage: python -I tools/screenshot.py out.png [COM11]
Needs Pillow and pyserial. The firmware answers 'd' with one "FB:<2048 hex>" line.
"""
import sys
import time

import serial
from PIL import Image


def grab(port):
    s = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write(b"d")
    end = time.time() + 3
    while time.time() < end:
        line = s.readline().decode(errors="replace").strip()
        if line.startswith("FB:") and len(line) == 3 + 2048:
            return bytes.fromhex(line[3:])
    raise SystemExit("no framebuffer received")


def main():
    out = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else "COM11"
    fb = grab(port)
    img = Image.new("1", (128, 64))
    for page in range(8):
        for x in range(128):
            byte = fb[page * 128 + x]
            for bit in range(8):
                img.putpixel((x, page * 8 + bit), 1 if byte >> bit & 1 else 0)
    # The firmware draws with rotation 1 (90 degrees clockwise), so turn it back to portrait.
    img = img.rotate(90, expand=True).resize((64 * 4, 128 * 4), Image.NEAREST)
    img.save(out)
    print(out)


if __name__ == "__main__":
    main()
