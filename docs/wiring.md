# Wiring

Board: **Seeed Xiao ESP32-C3**. Use the labels printed on the board.
No wire-colour convention is required.

## LCD wiring — confirmed

This mapping is transcribed from the final LCD wiring table for the physical
build. It also matches the display constants in
[`firmware/include/pins.h`](../firmware/include/pins.h).

| LCD panel pin | Xiao ESP32-C3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| DIN | D10 |
| CLK | D2 |
| CS | D6 |
| DC | D4 |
| RST | D5 |
| BL | D3 |

For the display-only bring-up sketch, see
[`arduino/LcdTest/LcdTest.ino`](../arduino/LcdTest/LcdTest.ino).

## MPU-6050 wiring — pending confirmation

The MPU's physical connections need to be checked before a verified wiring
map can be added here. No MPU pin mapping is being claimed as confirmed.

The firmware and the IMU bring-up sketch contain configured pin assignments.
Those remain in the source for reference and can be changed when the actual
wiring has been confirmed:

- [`firmware/include/pins.h`](../firmware/include/pins.h)
- [`arduino/MpuTest/MpuTest.ino`](../arduino/MpuTest/MpuTest.ino)

Checklist for the next update:

- [ ] Check the MPU module's power and ground connections.
- [ ] Trace and record SDA and SCL to the Xiao labels.
- [ ] Confirm whether INT is connected and record its Xiao label.
- [ ] Check the physical connections against the firmware configuration.
- [ ] Add the verified MPU table here and in the README.

## Other connections

Button, motor driver and battery connections still need a verified build
reference. The firmware's configured pin constants are available in `pins.h`;
this document currently confirms only the LCD connections above.

## Case

The enclosure's CAD and printable models are not part of this public release.
