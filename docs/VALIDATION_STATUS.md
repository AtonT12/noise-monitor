# Validation Status

## Checks completed in this environment

- `bash tools/run_host_tests.sh` — passed. Three standalone C++17 programs
  compile with `-Wall -Wextra -Werror -pedantic` and all assertions pass:
  - `test_alert_controller.cpp`: trigger equality, exact hold and cooldown
    boundaries, cancellation, quiet re-arm, repeated-noise suppression, and
    `millis()` rollover.
  - `test_wav_parser.cpp`: valid PCM WAV with an odd-sized unknown chunk, plus
    malformed `JUNK` / `fmt ` chunks declaring `0xFFFFFFF8` bytes.
  - `test_calibration.cpp`: legacy absolute-reference conversion, ±20 dB
    clamping, NaN/infinity fallback, and idempotence.
- Syntax and type check of every changed translation unit
  (`main.cpp`, `noise_meter.cpp`, `battery_monitor.cpp`, `alert_sound.cpp`,
  `wav_player.cpp`, `wav_parser.cpp`, `alert_controller.cpp`,
  `calibration.cpp`) with `g++ -std=gnu++17 -Wall -Wextra -Wformat=2 -Wshadow
  -fsyntax-only` against minimal stub Arduino/Preferences/LittleFS/esp_sleep
  headers. This caught two defects that a read-through missed: an enumerator
  named `LOW`, which collides with the Arduino `LOW` macro, and a
  `std::min(size_t, uint32_t)` mismatch.
- `python -m py_compile tools/generate_test_tone.py` — passed.
- Ad-hoc host harnesses (not committed) exercised the two new modules:
  - `battery_monitor`: divider scaling (1050 mV at the pin → 2.100 V pack),
    every status boundary (2.49 / 2.50 / 3.29 / 3.30 / 3.49 / 3.50 V), and the
    SoC curve (3.70 V → 35 %, 4.20 V → 100 %).
  - `alert_sound`: the chime renders 13,760 samples (0.86 s), peak 17 % of full
    scale at gain 0.25, mean 0.04 LSB (no DC offset), starts and ends at zero,
    and toggles the amplifier on and off exactly once per playback.
- `data/alert.wav` — verified as PCM mono, 16 kHz, 16-bit WAV.
- PlatformIO platform — pinned to `espressif32@8.0.0`. The PlatformIO 8.0.0
  release notes (published 2026-10-19) state that it bundles Arduino-ESP32
  Core 3.3.12 based on ESP-IDF 5.5.5.

## Checks not completed

- **PlatformIO target build (`pio run`) was not executed.** PlatformIO and the
  ESP32 toolchain are not installed in this environment.
- **No hardware was connected.** No XIAO ESP32-S3, INMP441, MAX98357A, speaker,
  divider, or battery was available during this review.
- The stub headers used for the syntax check are approximations. They verify
  names, types, and control flow, not the real Arduino-ESP32 API surface or
  the ESP-IDF I2S driver. A target build is still the first real gate.

## Behaviour introduced by the P0 pass that needs bench confirmation

| Item | What to verify |
|---|---|
| CPU at 80 MHz | I2S playback stays glitch-free and the 32 ms read cadence holds during playback at `SET_VOL 0.65` |
| Battery divider | With the divider fitted and no cell, `STATUS` reports `absent`; with a cell, the reading matches a multimeter within ±0.1 V |
| Low-battery warning | Pull the supply to 3.4 V and confirm the LED double-blink and the single Serial line |
| Protective sleep | Pull below 3.3 V and confirm the amplifier shuts down, the unit sleeps, and it wakes and re-checks after 10 minutes |
| Chime fallback | Erase the filesystem (upload the firmware without `uploadfs`) and confirm a reminder still plays |
| Calibration migration | Flash a unit that stored `cal = 120`, boot it, and confirm the log reports `0.0 dB offset` and the level reading is unchanged |
| Deep-sleep wake | Confirm settings survive the wake path and the boot banner reappears on Serial |

Until these are done, this archive remains a source project with host-tested
pure logic, not a validated device. Run the PlatformIO build and follow
`CALIBRATION_AND_TESTING.md` before connecting a battery or closing the plush
enclosure.
