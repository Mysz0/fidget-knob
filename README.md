# Fidget Knob

A little desk puck you twist in your hand. It controls smart lights, music and
timers, and shows everything on a round LCD as a field of glyph sprites.

No touchscreen. Twist the whole body like a bottle cap to change a value, shake
it, tap the case or press the centre button. In Lights mode the radius of the
sprite field is the brightness and its colour is the bulb's colour.

Lights talk to an IKEA DIRIGERA hub over its local API. Everything else goes
over Bluetooth LE to an iPhone. Smart-light control stays on the local network.
Album-art lookup sends the artist and track or album name to Apple's public
iTunes Search API and downloads artwork from the returned URL.

```
fidget-knob/
├── docs/wiring.md             Confirmed LCD wiring and pending MPU notes
├── firmware/                  PlatformIO project (ESP32-C3)
│   ├── include/               Pin map and config template
│   ├── lib/knob_music/        Pure logic (protocols, codecs, modes), host-tested
│   ├── src/
│   │   ├── input/             IMU, gesture recognition, button
│   │   ├── haptics/           Vibration motor patterns
│   │   ├── render/            Display driver, glyphs, sprite field
│   │   ├── target/            Target interface + DIRIGERA client
│   │   ├── ble/               Knob service, Apple Media Service, HID remote
│   │   └── app/               State machine and one file per mode
│   ├── test/                  Unity tests, run on your computer
│   └── tools/                 Hub pairing, gesture recording
├── ios/                       SwiftUI companion app
├── arduino/                   Tiny bring-up sketches (LCD, IMU, pins)
├── design/ + tools/           Sprite packs and the generator for them
└── README.md                  Setup, modes and test commands
```

## Hardware

| Part | Role |
|---|---|
| Seeed Xiao ESP32-C3 | MCU, WiFi, LiPo charging |
| Waveshare 19192 1.28" round LCD (GC9A01) | Display |
| MPU-6050 | Twist, shake, wake-on-motion |
| MT35 3V vibration motor | Haptic detents |
| 2N2222A + 1 kΩ + 1N4148 | Motor driver |
| 400 mAh 1S LiPo | Power |

The AS5600 magnetic encoder is **not used** — rotation comes from the gyroscope,
so there are no moving parts.

### Wiring

Wire by the **Xiao silkscreen labels**, not by GPIO numbers. This is the
confirmed LCD mapping from my build; wire colours are intentionally omitted.

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

**MPU-6050 wiring is still to be documented.** I need to check its connections
on the physical build before publishing a verified map. The assignments in
[`firmware/include/pins.h`](firmware/include/pins.h) are the software's current
configuration, not confirmation of the actual MPU wiring.

See [wiring notes and the remaining checklist](docs/wiring.md).

### First bring-up

Before flashing the full firmware, run
[`arduino/LcdTest`](arduino/LcdTest/LcdTest.ino) — a self-contained Arduino IDE
sketch that only lights the panel. If that does not show colour, nothing else is
worth debugging.

## Firmware setup

Needs [PlatformIO](https://platformio.org/install/cli).

> If you installed it with `pipx`, run `~/.local/pipx/venvs/platformio/bin/python
> -m ensurepip` once. pipx creates venvs without `pip`, and PlatformIO shells out
> to it to install esptool's dependencies — without it the first build dies with
> `MissingPackageManifestError`.

```bash
cd firmware
cp include/config.h.example include/config.h
```

Fill in your WiFi credentials, then pair with the hub:

```bash
pip3 install requests
python3 tools/pair.py 192.168.1.50
```

It will prompt you to press the action button on the bottom of the hub, then
offer to write the token and certificate into `config.h`.

```bash
pio run -t upload
```

`config.h` is gitignored — it holds a token that grants full control of your
home.

### If pairing is fighting you

Set `#define DIRIGERA_INSECURE` in `config.h` to skip certificate validation and
confirm the token and wiring work. Then comment it out and pin the real
certificate. Leaving it on lets anything on your LAN impersonate the hub.

## Case files

This repository publishes the software and wiring documentation. Enclosure
CAD files and printable models are not included in this release. The case is
still being refined; those files may be added later.

## Interaction (Lights mode)

| Input | Effect |
|---|---|
| Twist | Adjust brightness (or hue in colour mode), one detent per step |
| Shake | Toggle brightness ↔ colour |
| Button, tap | Turn the target on/off |
| Button, hold | Target picker — twist to scroll, release to select |
| Pick it up | Wakes |

## Modes

The knob boots into Lights, which works with no phone at all. The other modes
are picked from the iPhone app and the choice is stored on the knob, so it
survives sleep. Only Lights uses WiFi; everything else is Bluetooth.

| Mode | What it does |
|---|---|
| Lights | Brightness and colour of DIRIGERA lights |
| Music | Now playing from any app via Apple Media Service: twist for volume, tap to play/pause, shake to skip. Covers come from the app as a sprite mosaic or a photo |
| Timer | Kitchen-timer style: twist to set, tap to start or pause, shake to reset. Sleeps through a running timer and wakes to buzz |
| Clock | Time from the phone, drawn over the sprite field |
| Dice | Shake or tap to roll |
| Ambient | The sprite field for its own sake |
| Photos | Up to five pictures from the phone, kept in flash |
| Notify | The iPhone's own notifications (ANCS) |
| Games | Slow, turn-based ones on the sprite lattice |

Not on an iPhone? The knob also shows up as a standard Bluetooth media remote
(HID), so Android or a computer can pair it from its own Bluetooth settings and
take play/pause, next and volume from it.

Looks are curated rather than drawn: sprite packs, palettes and a few effects,
all generated from [`design/sprite_packs.txt`](design/sprite_packs.txt) by
`python3 tools/gen_sprites.py`, which writes both the firmware header and the
Swift file so the app preview always matches the knob.

## iPhone app

SwiftUI, iOS 26. The Xcode project is generated with
[XcodeGen](https://github.com/yonaskolb/XcodeGen) from `ios/project.yml`.

```bash
brew install xcodegen
cd ios
xcodegen
open FidgetKnob.xcodeproj
```

Set your own team under Signing & Capabilities (or in `project.yml`) and run it
on a real iPhone. The simulator has no Bluetooth, so it can show the UI but
will never find the knob. The app handles pairing, mode switching, looks,
album covers (looked up in the public iTunes Search API, no login) and the
timer alarm.

## Power

IKEA and Philips buttons feel instant and last years because they use Zigbee.
The ESP32-C3 has no Zigbee radio, so it cannot work that way.

The plan was to keep the WiFi association alive through automatic light sleep.
**That is not possible on the stock Arduino core** — it ships compiled with
`CONFIG_PM_ENABLE` off, so `esp_pm_configure()` fails at runtime and there is no
way around it short of building Arduino as an ESP-IDF component.

So the device deep-sleeps instead, and makes waking cheap. Channel, BSSID and
the DHCP lease are cached in RTC memory, which survives deep sleep, cutting a
reconnect from ~2 s to **~300–600 ms** by skipping the channel scan and the DHCP
handshake. The lease is renewed every 32 wakes so the device cannot squat on an
address the router has reassigned.

| State | Draw |
|---|---|
| Awake, screen lit | ~60–80 mA |
| Awake, screen off | ~25 mA |
| Deep sleep, watching for motion | ~20–30 µA |

It sleeps after 2 minutes of stillness. That works out to about 1 mAh per
interaction — **300+ interactions per charge**, so a few weeks of normal use.
Battery life depends on how often you pick it up, not on standby.

## Adding other targets

Everything above [`src/target/target.h`](firmware/src/target/target.h) is
written against the `Target` interface and knows nothing about DIRIGERA, HTTP or
JSON. PC volume, media transport or anything else needs one new implementation
of that interface, registered at startup. No other module changes.

## Tests and tools

```bash
cd firmware
pio test -e native            # host-side unit tests, no hardware needed
pio run -e trace -t upload    # streams every IMU sample as CSV over serial
pio run -e pindiag -t upload  # wiring self-test: shorts to GND and between pins
```

With the trace firmware on the knob, `tools/record_gestures.py` walks you
through each gesture and saves a labelled recording to `tools/recordings/`.
`tools/live_gestures.py` prints taps, shakes and twists as the normal firmware
sees them. Both need `pyserial` (PlatformIO's own Python has it).

The iOS tests run from Xcode (Cmd+U) or:

```bash
xcodebuild test -project ios/FidgetKnob.xcodeproj -scheme FidgetKnob \
  -destination 'platform=iOS Simulator,name=iPhone 17'
```

### Building from iCloud Drive or Dropbox

If the repo lives in a synced folder, PlatformIO's `.pio` directory gets
synced too and builds become slow or fail with half-written files. Point the
build somewhere local by adding this to `[platformio]` in `platformio.ini`:

```ini
build_dir   = /tmp/fidget-pio/build
libdeps_dir = /tmp/fidget-pio/libdeps
```

## Inspiration and authorship

The screen's glyph animation was inspired by an Instagram video from
[@kindofdevice_](https://www.instagram.com/kindofdevice_/) by
[Kind of Studio](https://kindofstudio.co/). Other parts combine my own ideas
with that inspiration. All project files and wiring are my own work.

## Still to document

- [ ] Check and document the MPU-6050 wiring on the physical build.
- [ ] Add the remaining verified button and motor wiring details.
- [ ] Publish the case files once the fit is final.
