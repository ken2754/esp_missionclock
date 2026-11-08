# ESP32 CYD NASA Apollo Mission Clock (`esp_missionclock`)

A retro CRT-styled NASA Mission Clock for the **ESP32 Cheap Yellow Display (CYD / ESP32-2432S028, 320x240)**.
It recreates historical Apollo missions utterance-by-utterance in real historical cadence, styled like a 1960-70s Apollo Mission Control / Glass CRT terminal.

---

## Features

- **1960–70s Glass CRT Aesthetic (12-Line Monospace Display)**:
  - Uses the crisp 8×16 monospace font (`lv_font_unscii_16`), formatted into **11 scrolling transcript log rows + 1 live status bar (Total 12 rows)**.
  - Strict CRT dual-green phosphorescent color palette:
    - **Background**: Deep Pure Black (`#000000`)
    - **Transcript Utterances & Clock**: High Phosphor Bright Green (`#00FF41`)
    - **Timestamps, Speakers & Borders**: Low Phosphor Glow / Dim Green (`#007722`)
- **Utterance-by-Utterance Historical Cadence**:
  - Transcripts play in their exact historical time intervals, recreating the tension and atmosphere of Mission Control.
- **Instant Touch Mode Toggle**:
  - **Tap anywhere on the display** to instantly toggle log timestamps between:
    - `[LOCAL]`: Offset Local Time (synced to the current wall clock, e.g. `[15:30:14]`)
    - `[HIST]`: Ground Elapsed Time (e.g. `[T-01:15:40]` or `[T+000:00:14]`)
- **Curated Apollo Missions (Apollo 8, 11, 13, 17)**:
  - **Apollo 17 (The Main Star)**: Includes the dramatic **T-30s automatic cut-off and 2h40m countdown hold**, propellant tank manual pressurization, recycle to T-22m, and the historic night launch (*"Look at the light!"*, *"Woo-hoo!"*) down to the exact second, through to the final lunar EVA closing speech.
  - **Apollo 11**: Staging, lunar descent, 1202 alarms, touchdown (*"Houston, Tranquility Base here. The Eagle has landed."*), and first steps (*"That's one small step for man, one giant leap for mankind."*).
  - **Apollo 13**: Launch, oxygen tank explosion crisis (*"Houston, we've had a problem."*), Aquarius lifeboat maneuvers, and splashdown.
  - **Apollo 8**: First human voyage to the Moon, lunar orbit insertion, Christmas Eve Genesis reading (*"In the beginning..."*), and return (*"There is a Santa Claus."*).
- **Hourly Joke & Surreal Banter (Rendered in White)**:
  - Every hour on the hour (`:00` to `:01`), if there is no historical transmission scheduled, an astronaut calls Houston to ask for the current time.
  - CAPCOM replies with the exact local hour (*"Apollo, mark. It's four o'clock."*), and the astronaut acknowledges (*"Roger, four o'clock. Copy that."*).
  - Accompanied by rotating surreal banter (trying to order Cocoa Beach pizza to orbit, accidentally calling Siri on the AGC, seeing a blue police telephone box outside the window, asking for the CM WiFi password, or asking for the Moon's daily weather).
  - **Differentiated in Crisp White (`#FFFFFF`)** to clearly distinguish it from authentic historical transmissions.
- **Smart Silence Compression (Smart Warp)**:
  - Automatically compresses hours-long orbital silences (>2 minutes) down to ~15 seconds with a simulated pause banner (`--- [45 MINUTES OF ORBITAL SILENCE] ---`) so your desk clock stays lively.
- **Captive Portal Web Configuration**:
  - Connect to `CYD-MissionClock-Setup` or browse to the device's IP to customize active mission, timezone, time display mode, hourly joke toggle, smart warp, and backlight brightness.
  - Full hardware compatibility with `esp_consoleclock` pinout and drivers.

---

## Pinout (CYD 2.8" ESP32-2432S028)

| Component | Function | GPIO Pin |
| :--- | :--- | :--- |
| **TFT Display (SPI2)** | SCK / MOSI / MISO | GPIO 14 / 13 / 12 |
| | DC / CS / Backlight PWM | GPIO 2 / 15 / 21 |
| **Touch XPT2046 (SPI3)** | CLK / MOSI / MISO | GPIO 25 / 32 / 39 |
| | CS / IRQ | GPIO 33 / 36 |
| **RGB LED** | R / G / B (Active LOW) | GPIO 4 / 16 / 17 |

---

## Build & Flash

```bash
# Build
idf.py build

# Flash and Monitor
idf.py -p /dev/ttyUSB0 flash monitor
```
