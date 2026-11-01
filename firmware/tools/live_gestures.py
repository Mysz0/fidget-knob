#!/usr/bin/env python3
"""Live view of what the knob recognises, for checking gestures by hand.

Prints taps, shakes and mode changes as they happen (twist lines are summed
into one running counter so they do not scroll everything else away). Works
with the normal firmware - it reads the same serial log the app already
writes. Stop with Ctrl-C.

    ~/.local/pipx/venvs/platformio/bin/python tools/live_gestures.py
"""

import glob
import sys
import time

import serial

GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
BOLD = "\033[1m"
RESET = "\033[0m"


def connect():
    while True:
        ports = glob.glob("/dev/cu.usbmodem*")
        if ports:
            try:
                return serial.Serial(ports[0], 115200, timeout=0.2)
            except serial.SerialException:
                pass
        print("\rCzekam na krążek (podłącz kablem / porusz nim)...", end="", flush=True)
        time.sleep(0.5)


def main():
    ser = connect()
    print(f"\r{BOLD}Podgląd gestów na żywo{RESET} - Ctrl-C kończy.\n")
    pending = ""
    twists = 0
    while True:
        try:
            pending += ser.read(4096).decode(errors="replace")
        except serial.SerialException:
            ser = connect()
            continue
        *lines, pending = pending.split("\n")
        for line in lines:
            line = line.strip()
            stamp = time.strftime("%H:%M:%S")
            if line.startswith("twist"):
                twists += 1
                print(f"\r  obrót... ({twists} zdarzeń)", end="", flush=True)
            elif line.startswith("tap"):
                state = "WŁĄCZONA" if line.endswith("on") else "WYŁĄCZONA"
                print(f"\r{stamp}  {GREEN}{BOLD}✓ STUKNIĘCIE{RESET} -> lampa {state}" + " " * 10)
            elif line.startswith("shake"):
                mode = "KOLOR" if "COLOUR" in line else "JASNOŚĆ"
                print(f"\r{stamp}  {YELLOW}{BOLD}✓ MACHNIĘCIE{RESET} -> tryb {mode}" + " " * 10)
            elif "write failed" in line:
                print(f"\r{stamp}  {RED}hub nie odpowiedział{RESET} ({line.split('(')[-1]}" + " " * 10)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
