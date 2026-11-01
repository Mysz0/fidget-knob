#!/usr/bin/env python3
"""Guided gesture recording for tuning the tap and shake thresholds.

Needs the trace firmware on the knob:

    pio run -e trace -t upload

Then, with the knob still plugged in:

    ~/.local/pipx/venvs/platformio/bin/python tools/record_gestures.py

It walks through each gesture with a countdown, labels every IMU sample with
the step it was recorded in, and writes tools/recordings/<timestamp>.csv. Taps
and shakes the current firmware recognises are echoed live, so a missed one is
visible as it happens.
"""

import glob
import os
import queue
import sys
import threading
import time

import serial  # pyserial ships inside PlatformIO's venv.

WALK_STEPS = [
    ("rest", 4, "Put the knob on the table and do NOT touch it."),
    ("walk", 20, "WALK around the room holding the knob normally, like carrying it. Do not twist."),
    ("walk_swing", 12, "Walk again, arm swinging naturally with the knob in your hand."),
    ("twist_slow", 10, "Stand still and TWIST it slowly back and forth, like turning volume."),
    ("twist_fast", 8, "TWIST it quickly back and forth."),
    ("walk_twist", 12, "Walk AND twist it now and then on purpose while walking."),
]

STEPS = [
    ("rest", 4, "Połóż krążek na stole i NIE RUSZAJ go."),
    ("hold", 5, "Weź go do ręki i trzymaj normalnie, bez gestów."),
    ("tap_light", 12, "Stuknij LEKKO palcem w obudowę 5 razy, co ~2 sekundy."),
    ("tap_hard", 10, "Stuknij MOCNIEJ 4 razy, co ~2 sekundy."),
    ("shake_h", 12, "Machnij POZIOMO (w bok i z powrotem) 3 razy, co ~3 sekundy."),
    ("shake_v", 12, "Machnij PIONOWO (w górę i w dół) 3 razy, co ~3 sekundy."),
    ("twist", 8, "OBRACAJ powoli w lewo i w prawo."),
    ("handle", 10, "Poobracaj go w dłoni, podnieś, odłóż - zwykłe trzymanie, bez stukania."),
]

BOLD = "\033[1m"
DIM = "\033[2m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
RESET = "\033[0m"


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("Cannot see the knob. Plug it in over USB-C and close the Arduino IDE.")
    return ports[0]


def start_reader(ser):
    """Reads the port continuously on a background thread.

    The knob's USB serial stops transmitting for good if the host stops reading
    for a few seconds - its transmit buffer fills and it decides nobody is
    listening. So reading never pauses, not even while waiting for Enter; lines
    queue up here and the steps take them as they go.
    """
    lines = queue.Queue()

    def run():
        pending = b""
        while True:
            pending += ser.read(4096)
            *complete, pending = pending.split(b"\n")
            for raw in complete:
                lines.put(raw.decode(errors="replace").strip())

    threading.Thread(target=run, daemon=True).start()
    return lines


def alive(lines, seconds=1.5):
    """True if the knob sends anything within `seconds`."""
    try:
        lines.get(timeout=seconds)
        return True
    except queue.Empty:
        return False


def discard(lines):
    while not lines.empty():
        lines.get_nowait()


def drain(lines, out, label):
    """Writes queued sample lines labelled with the step; returns any events."""
    events = []
    while not lines.empty():
        line = lines.get_nowait()
        parts = line.split(",")
        # Only sample lines: t,ax,ay,az,gz,event. Boot chatter is skipped.
        if len(parts) not in (6, 8):
            continue
        try:
            float(parts[1])
        except ValueError:
            continue
        out.write(f"{label},{line}\n")
        if parts[5]:
            events.append(parts[5])
    return events


def main():
    global STEPS
    if "--walk" in sys.argv:
        STEPS = WALK_STEPS
    port = find_port()
    ser = serial.Serial(port, 115200, timeout=0.05)
    lines = start_reader(ser)

    os.makedirs(os.path.join(os.path.dirname(__file__), "recordings"), exist_ok=True)
    path = os.path.join(
        os.path.dirname(__file__), "recordings", time.strftime("%Y%m%d-%H%M%S") + ".csv"
    )

    names = {"tap": "tap", "shake": "shake"}
    with open(path, "w") as out:
        out.write("step,t_ms,ax,ay,az,gz,event,gx,gy\n")
        print(f"\n{BOLD}Gesture recording{RESET}  {DIM}({len(STEPS)} steps, ~1.5 min){RESET}")
        print("Keep the knob plugged in the whole time (a long cable helps for walking).")
        input(f"{BOLD}Press Enter when you are ready...{RESET}\n")
        discard(lines)
        if not alive(lines):
            sys.exit("The knob is not sending data. Is the trace firmware flashed (pio run -e trace -t upload)?")

        for i, (label, seconds, instruction) in enumerate(STEPS, 1):
            print(f"{BOLD}Step {i}/{len(STEPS)}:{RESET} {instruction}")
            for n in (3, 2, 1):
                print(f"  {YELLOW}{n}...{RESET}", end="", flush=True)
                end = time.time() + 1
                while time.time() < end:
                    drain(lines, out, "wait")
                    time.sleep(0.02)
            print(f"  {GREEN}{BOLD}NOW!{RESET}")

            start = time.time()
            counts = {}
            while (left := seconds - (time.time() - start)) > 0:
                for e in drain(lines, out, label):
                    counts[e] = counts.get(e, 0) + 1
                    print(f"\r    {GREEN}✓ detected: {names.get(e, e)}{RESET}" + " " * 20)
                bar = "█" * int(left) + "·" * (seconds - int(left))
                print(f"\r    {DIM}{bar} {left:4.1f} s{RESET}  ", end="", flush=True)
                time.sleep(0.02)

            summary = ", ".join(f"{names.get(k, k)} ×{v}" for k, v in counts.items())
            print(f"\r    Step done. Detected: {summary or 'nothing'}" + " " * 20 + "\n")

    print(f"{BOLD}Done.{RESET} Saved: {path}")
    print("Say in the chat that you are done - I will take it from there.\n")


if __name__ == "__main__":
    main()
