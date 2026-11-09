# Testing, Calibration and Validation

This document covers what has been verified, how to bring the prototype up, how to calibrate it, and what still requires bench work.

## 1. Current validation status

### Verified in the packaging environment

- `bash tools/run_host_tests.sh` — passed. Three standalone C++17 programs compile with `-Wall -Wextra -Werror -pedantic` and all assertions pass:
  - `test_alert_controller.cpp` — trigger equality, exact hold and cooldown boundaries, cancellation, quiet re-arm, repeated-noise suppression, and `millis()` rollover.
  - `test_wav_parser.cpp` — a valid PCM WAV containing an odd-sized unknown chunk, plus malformed `JUNK` and `fmt ` chunks declaring `0xFFFFFFF8` bytes.
  - `test_calibration.cpp` — legacy absolute-reference conversion, ±20 dB clamping, NaN/infinity fallback, and idempotence.
- Syntax and type check of every translation unit (`main.cpp`, `noise_meter.cpp`, `battery_monitor.cpp`, `alert_sound.cpp`, `wav_player.cpp`, `wav_parser.cpp`, `alert_controller.cpp`, `calibration.cpp`) with `g++ -std=gnu++17 -Wall -Wextra -Wformat=2 -Wshadow -fsyntax-only`, against minimal stub Arduino, Preferences, LittleFS and esp_sleep headers.
- `python -m py_compile tools/generate_test_tone.py` — passed.
- `data/alert.wav` — confirmed PCM mono, 16 kHz, 16-bit, 18,400 frames (1.15 s).
- Ad-hoc harnesses (not committed) exercised two hardware-facing modules:
  - `battery_monitor` — divider scaling (1050 mV at the pin → 2.100 V pack), every status boundary (2.49 / 2.50 / 3.29 / 3.30 / 3.49 / 3.50 V), and the state-of-charge curve (3.70 V → 35 %, 4.20 V → 100 %).
  - `alert_sound` — the chime renders 13,760 samples (0.86 s including the silence tail), peak 17 % of full scale at gain 0.25, mean 0.04 LSB (no DC offset), starts and ends at zero, and toggles the amplifier on and off exactly once per playback.

### Not verified

- **`pio run` has never been executed.** PlatformIO and the ESP32 toolchain were not installed in the packaging environment.
- **No hardware has been connected.** No XIAO ESP32-S3, INMP441, MAX98357A, speaker, divider, or battery was available.
- The stub headers used for the syntax check approximate the Arduino-ESP32 API. They verify names, types and control flow, not the real driver surface. A target build is the first real gate.
- The three host suites cover pure logic only. They say nothing about I2S, LittleFS, the microphone, the amplifier or the speaker.

Until the target build and the physical tests below are complete, this repository is a source project with host-tested logic, not a validated device.

## 2. Bring-up sequence

Complete the tests in order. Do not start by placing the electronics inside the plush toy.

### Test A — Firmware and serial connection

1. Open the project in PlatformIO.
2. Run `pio run`.
3. Connect the XIAO ESP32-S3 by USB.
4. Run `pio run -t upload`.
5. Open `pio device monitor -b 115200`.
6. Confirm the boot banner and the I2S initialisation message appear.

Expected output:

```text
========================================================
 Noise Monitor MVP - Version C
 ESP32-S3 @ 80 MHz | Full-duplex I2S | Local-only
 Measurement: calibrated unweighted SPL estimate; not dBA.
 Type HELP in Serial Monitor for configuration commands.
========================================================
[NVS] Loaded: trigger=70.0, reset=65.0, calibration=+0.0, gain=0.25
[BAT] No battery detected; running from USB.
[AUDIO] I2S full-duplex ready: 16 kHz, 32-bit stereo, shared clocks.
[WAV] LittleFS mounted.
[READY] Noise monitoring started.
Commands:
  HELP                 Show this help
  ...
```

The command list printed by `HELP` follows `[READY]` automatically on every boot; it is abbreviated above. After that, `[MONITOR]` telemetry lines appear twice per second.

If `/alert.wav` is missing or the filesystem was never uploaded, expect `[WARN] /alert.wav is missing; the built-in chime will be used.` instead of a clean start. That is not a failure — the chime fallback is intended.

If `[FATAL] I2S initialization failed` appears instead, the device stops monitoring and blinks the LED rapidly. Check the board target, the GPIO map and the common ground before anything else.

### Test B — Microphone input

1. Connect the INMP441 only.
2. Keep L/R tied to GND.
3. Confirm the serial `dBFS` and `SPL_est` values change when the room sound changes.
4. Check that covering or uncovering the microphone port changes the reading in a plausible direction.
5. Do not interpret the initial `SPL_est` value as an accurate SPL measurement.

### Test C — Speaker output

1. Connect the MAX98357A and speaker.
2. Confirm that amplifier VIN is supplied from a suitable 2.5–5.5 V source and that all grounds are common.
3. Upload the filesystem using `pio run -t uploadfs`.
4. Send `SET_VOL 0.10`.
5. Send `TEST_ALERT` and verify the test chime plays.
6. Increase the gain in small steps only if necessary; listen for distortion and check the speaker and amplifier for unusual heating.

Remember that `TEST_ALERT` also starts the cooldown, so automatic alerts are suppressed for 30 s afterwards.

### Test D — Trigger policy

For bench testing, lower the trigger threshold with `SET_THRESH` and adjust the reset threshold with `SET_RESET`. Keep the reset threshold below the trigger threshold. Create a sustained sound source above the threshold for longer than the hold time.

Verify all of the following:

- A short transient does not cause a reminder.
- A sustained high level causes exactly one reminder.
- The device stays in cooldown after playback.
- The device does not repeat reminders while the noise remains high.
- The device re-arms only after cooldown has elapsed **and** the environment has stayed below the reset threshold for the quiet-rearm interval.
- A new sustained high-noise event can trigger after re-arming.

Return the thresholds to appropriate values after bench testing.

## 3. Calibration procedure

The firmware computes an unweighted estimate:

```text
dBFS = 20 * log10(RMS)
SPL_estimate = dBFS + 120 dB + calibration_offset
```

The 120 dB term is the INMP441 full-scale reference (−26 dBFS at 94 dB SPL) and is fixed in `include/calibration.h`. The offset starts at 0 dB and is the only value the user adjusts; it is limited to ±20 dB, which covers normal enclosure losses.

Recommended prototype calibration:

1. Assemble the microphone in the intended enclosure, because fabric and internal placement change the acoustic response.
2. Place the device and a reference sound level meter close together, with their microphone openings in similar positions and orientations.
3. Use a stable test sound and record the firmware's `dBFS` and the reference meter's displayed value.
4. Calculate the first offset: `offset = reference_level - measured_SPL_estimate`.
5. Apply it with `SET_CAL <value>` (±20 dB).
6. Repeat at multiple sound levels and, where possible, multiple frequencies. A single offset cannot correct frequency response or non-linearity.
7. Record the test conditions, the reference instrument, the enclosure state and the remaining error.

This procedure improves a prototype threshold detector. It does not establish IEC 61672 compliance or certified measurement accuracy, and because no A-weighting is applied, the result must not be described as dBA.

### Legacy calibration values

Firmware before the calibration split stored the absolute 0 dBFS reference (60–150 dB, default 120) under the key now used for the offset. Anything outside the offset range is therefore treated as the older encoding and converted by subtracting 120 dB:

| Stored value | Interpreted as | Reason |
|---:|---:|---|
| `0.0` | `+0.0 dB` | Fresh device, compiled default |
| `3.5` | `+3.5 dB` | Already an offset |
| `120.0` | `+0.0 dB` | Legacy default reference, no correction |
| `123.5` | `+3.5 dB` | Legacy reference with a +3.5 dB correction |
| `150.0` | `+20.0 dB` | Legacy value clamped to the offset limit |

The conversion is idempotent and never rewrites flash, so a unit calibrated before the change keeps its correction without a migration step.

## 4. Host-side test suite

Run from the project root:

```bash
bash tools/run_host_tests.sh
```

The script compiles and runs three independent programs with strict warnings enabled:

| Program | Covers |
|---|---|
| `test_alert_controller` | Threshold equality, exact hold and cooldown boundaries, cancellation below the reset threshold, quiet re-arm, suppression of repeated alerts, `uint32_t` rollover |
| `test_wav_parser` | Valid WAV with an odd-sized unknown chunk; oversized `JUNK` and `fmt ` chunks; RIFF boundary and pad-byte handling |
| `test_calibration` | Legacy conversion, clamping, NaN/infinity fallback, idempotence |

These tests do not exercise I2S, LittleFS hardware behaviour, the microphone, the amplifier or the speaker. They are a regression net for the pure-logic modules, not a substitute for a target build.

## 5. Acceptance checklist

- [ ] Firmware compiles in PlatformIO (`pio run`).
- [ ] Firmware uploads to the XIAO ESP32-S3.
- [ ] Serial telemetry appears consistently at 500 ms intervals.
- [ ] Microphone levels respond to environmental sound.
- [ ] `/alert.wav` mounts and passes format validation.
- [ ] `TEST_ALERT` plays without clipping or unexpected noise.
- [ ] A sustained threshold crossing triggers exactly one reminder.
- [ ] The system waits for quiet before re-arming.
- [ ] NVS settings survive a restart.
- [ ] The amplifier enters shutdown after playback.
- [ ] Battery, board, wires and speaker are mechanically secured before enclosure integration.

## 6. Behaviour that still needs bench confirmation

| Item | What to verify |
|---|---|
| CPU at 80 MHz | I2S playback stays glitch-free and the 32 ms read cadence holds during playback at `SET_VOL 0.65` |
| Full-duplex I2S | One `i2s_new_channel()` call produces working TX and RX on controller 0 with LRCK at exactly 16 kHz |
| Battery divider | With the divider fitted and no cell, `STATUS` reports `absent`; with a cell, the reading matches a multimeter within ±0.1 V |
| Low-battery warning | Pull the supply to 3.4 V and confirm the LED double-blink and the single Serial line |
| Protective sleep | Pull below 3.3 V and confirm the amplifier shuts down, the unit sleeps, and it wakes and re-checks after 10 minutes |
| Chime fallback | Erase the filesystem (upload firmware without `uploadfs`) and confirm a reminder still plays |
| Calibration migration | Flash a unit that stored `cal = 120`, boot it, and confirm the log reports `+0.0 dB offset` with an unchanged level reading |
| Deep-sleep wake | Confirm settings survive the wake path and the boot banner reappears on Serial |
| Speaker temperature | Run several consecutive reminders at the production gain and check the amplifier and speaker for heating |

Generic hardware and serial faults are covered in the troubleshooting table in the [README](../README.md#9-troubleshooting).
