# Version C — Review Hardening Patch

This update addresses the firmware review findings supplied after the first Version C archive.

## 1. I2S TX/RX slot configuration

The reviewed Version C source already used the native ESP-IDF standard I2S driver and explicitly selected `I2S_STD_SLOT_BOTH`; it did not use Arduino `I2SClass::configureTX()`. The TX and RX initialization has now been split into separate, explicit configuration structures:

- TX: stereo, `I2S_STD_SLOT_BOTH`; each PCM sample is written to both left and right slots.
- RX: stereo, `I2S_STD_SLOT_BOTH`; this preserves the interleaved two-word DMA layout expected by `readMicrophoneFrames()` and `NoiseMeter`.
- The INMP441 is physically strapped to its left slot, and the noise meter processes only the left word.

RX is intentionally not changed to mono/LEFT. Doing so would change the DMA buffer layout and require a matching change to frame sizing and sample extraction. The documentation now records this dependency explicitly.

## 2. Overflow-safe WAV parsing

WAV parsing has been separated into `wav_parser.cpp`, a platform-independent C++ module. It validates the declared RIFF boundary and each padded chunk length before reading or seeking. Position and size arithmetic uses `uint64_t`; forward skips compare the requested byte count with `fileSize - currentPosition` before adding.

The malformed chunk size `0xFFFFFFF8` is rejected for both unknown chunks and `fmt ` chunks. The amplifier is not enabled unless parsing and the required 16 kHz format check succeed.

## 3. Automated regression tests

The host test command now builds and runs two independent test programs:

- `test_alert_controller.cpp`: exact trigger threshold, hold boundary, cooldown boundary, quiet re-arm boundary, cancellation, repeated-noise suppression, and `millis()` rollover.
- `test_wav_parser.cpp`: a valid WAV containing an odd-sized unknown chunk, plus malformed oversized `JUNK` and `fmt ` chunks declaring `0xFFFFFFF8` bytes.

Run from the project root:

```bash
bash tools/run_host_tests.sh
```

## 4. Reproducible PlatformIO platform

`platformio.ini` now pins `platform = espressif32@8.0.0`. The PlatformIO 8.0.0 release notes state that it bundles Arduino-ESP32 Core 3.3.12. This replaces the floating pioarduino stable URL.

## 5. P0 pass: power, battery, audio fallback, calibration model

This pass closes the four gaps that blocked battery-powered use. It changes
runtime behaviour, so re-read the wiring section before flashing an existing
prototype.

### 5.1 CPU frequency

`setup()` now calls `setCpuFrequencyMhz(80)` before anything else. 16 kHz DSP
needs a small fraction of one core, so the default 240 MHz only wastes current.

The WiFi and Bluetooth libraries are deliberately **not** linked in. Calling
`WiFi.mode(WIFI_OFF)` would pull in the WiFi/lwIP stack purely to switch off
something that was never started; not initialising it at all is both smaller
and equally effective.

### 5.2 Battery monitoring and protective sleep

New module: `battery_monitor.{h,cpp}`. A 100 kΩ / 100 kΩ divider from `BAT+` to
GND feeds **D1 / GPIO2** (ADC1_CH1) — GPIO1 was already used for the
microphone data line, so the sense pin moved. The pack is averaged over 16
calibrated `analogReadMilliVolts()` samples every 10 s.

| Status | Threshold | Action |
|---|---:|---|
| `Absent` | < 2.50 V | No warning |
| `Ok` | ≥ 3.50 V | Nothing |
| `Low` | 3.30–3.50 V | LED double-blink, one Serial warning |
| `Critical` | < 3.30 V | Amplifier off, timer deep sleep, re-check in 10 minutes |

The first check runs *before* `audio_bus::begin()`, so a critical cell is never
asked to drive the amplifier. The divider must be fitted even on USB-only
units: a floating ADC pin can read like a critical battery.

### 5.3 Built-in chime fallback

New module: `alert_sound.{h,cpp}`. `alert_sound::play()` tries `/alert.wav`
first and synthesises a three-note G5-E5-C5 chime when the file is missing,
unreadable, or not the required format. Previously a forgotten `uploadfs` step
produced a device that silently did nothing.

`STATUS` now prints the active reminder source, and the boot log names the
fallback explicitly.

### 5.4 Calibration model split

`cal` used to store the absolute 0 dBFS reference (default 120 dB), which forced
users to enter "120 + correction". It now stores only the correction:

```text
SPL_estimate = dBFS + 120 dB + calibration_offset
```

- `calibration.h` owns the 120 dB reference, the ±20 dB offset limits, and the
  NVS value interpretation.
- `SET_CAL` accepts −20…+20 dB.
- Values read from NVS outside that range can only be the older absolute
  encoding, so `calibration::normalizeStoredOffset()` subtracts the reference.
  The conversion is idempotent and never rewrites flash, so a unit calibrated
  before this change keeps its correction without a migration step.

### 5.5 Tests

`test_calibration.cpp` covers the legacy conversion, clamping, NaN/infinity
handling, and idempotence. `tools/run_host_tests.sh` now builds three programs.

## Validation boundary

Host tests pass in the packaging environment. A target `pio run` and physical I2S/audio/power tests have not yet been completed. See `VALIDATION_STATUS.md`.
