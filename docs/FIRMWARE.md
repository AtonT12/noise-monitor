# Firmware Architecture

## 1. Runtime overview

The firmware is organized into the modules below. `alert_controller`,
`wav_parser`, and `calibration` are hardware-independent and are covered by
host tests in `tools/run_host_tests.sh`; everything else touches Arduino or
ESP-IDF and is only exercised by a target build.

| Module | Files | Responsibility |
|---|---|---|
| Application | `src/main.cpp` | Startup, serial CLI, NVS settings, battery policy, LED, main loop |
| Alert sound | `include/alert_sound.h`, `src/alert_sound.cpp` | WAV-or-chime selection and the built-in chime generator |
| Battery monitor | `include/battery_monitor.h`, `src/battery_monitor.cpp` | ADC sampling, status classification, state-of-charge estimate |
| Calibration | `include/calibration.h`, `src/calibration.cpp` | dBFS-to-SPL reference, offset limits, NVS value interpretation |
| Audio bus | `include/audio_bus.h`, `src/audio_bus.cpp` | I2S full-duplex setup, microphone reads, speaker writes, amplifier enable |
| Noise meter | `include/noise_meter.h`, `src/noise_meter.cpp` | PCM extraction, DC blocker, RMS, dBFS, calibration offset, EMA |
| Alert controller | `include/alert_controller.h`, `src/alert_controller.cpp` | Threshold hold, hysteresis, cooldown, quiet-to-rearm state machine |
| WAV parser | `include/wav_parser.h`, `src/wav_parser.cpp` | Platform-independent RIFF chunk parsing with overflow-safe bounds checks |
| WAV player | `include/wav_player.h`, `src/wav_player.cpp` | LittleFS mount, WAV validation, chunked PCM playback |

The code is written in C++17 for Arduino-ESP32 Core 3.x / ESP-IDF 5.x. Wiring is in [HARDWARE.md](HARDWARE.md); bring-up, calibration and validation status are in [TESTING.md](TESTING.md).

## 2. I2S full-duplex audio bus

Version C creates paired TX and RX channels on I2S controller 0 using the ESP-IDF standard-mode driver:

```cpp
i2s_new_channel(&channelConfig, &txHandle, &rxHandle);
```

Both channels share standard-mode clock and frame timing:

- Sample rate: 16,000 Hz.
- Data width: 32 bits.
- Slot mode: stereo (two 32-bit slots per frame).
- Slot mask: explicitly `I2S_STD_SLOT_BOTH` on TX and RX.
- Philips I2S framing.
- Shared BCLK and WS pins.

The TX channel is initialized directly with BOTH selected before it is enabled. The playback routine also writes an interleaved `[left, right]` pair containing the same sample in each slot. This code uses the ESP-IDF standard I2S driver, not Arduino `I2SClass::configureTX()`, so there is no later configuration call whose early-return behavior could leave the transmitter in mono/left mode.

RX intentionally remains stereo/BOTH because `readMicrophoneFrames()` sizes buffers as two 32-bit words per frame and `NoiseMeter` reads the even-indexed left word. The INMP441's L/R strap determines which slot contains microphone data; it does not require changing the DMA layout to mono. If RX is changed to mono/LEFT, both the read-buffer sizing and sample extraction must be updated together.

One consequence is worth knowing: the INMP441 tri-states its SD output outside its selected slot, so with RX reading both slots the right slot samples a floating line. `NoiseMeter` never reads it, so measurements are unaffected, but a 100 kΩ pull-down from DIN to GND removes the floating input and the associated noise pickup.

The microphone is connected to DIN and strapped to its left slot. Its 24-bit signed payload is extracted from the 32-bit slot by shifting right eight bits. The microphone signal is not downmixed with the unused right slot.

For playback, a mono 16-bit PCM sample is scaled by the configured software gain, converted to a left-aligned 32-bit word, and duplicated in both stereo slots. This keeps the MAX98357A channel-selection strap from changing the reminder content.

The RX and TX channels share clocks; they are not intended to record the speaker output or perform echo cancellation. Microphone measurements are ignored while the WAV player is active, and buffered samples are discarded for a short interval after playback.

## 3. Noise meter pipeline

For each block of input frames:

1. Select the INMP441 left-channel word.
2. Shift the 24-bit signed payload into the low bits of an `int32_t`.
3. Normalize the sample to approximately `[-1.0, +1.0]`.
4. Apply a first-order DC blocker:

   `y[n] = x[n] - x[n-1] + alpha * y[n-1]`

5. Calculate RMS over the block:

   `RMS = sqrt(sum(y[n]^2) / N)`

6. Convert to dBFS:

   `dBFS = 20 * log10(max(RMS, floor))`

7. Add the calibration offset to produce an unweighted SPL estimate.
8. Smooth the estimate with an exponential moving average:

   `EMA[n] = alpha * current + (1 - alpha) * EMA[n-1]`

The DC blocker and EMA are engineering conveniences for a stable prototype reading. They do not implement A-weighting or make the unit a certified sound level meter.

## 4. Alert state machine

| State | Entry condition | Behavior / exit |
|---|---|---|
| `MONITORING` | Boot or successful re-arm | Wait for level >= trigger threshold |
| `THRESHOLD_HOLD` | Trigger threshold crossed | Start hold timer; cancel if level drops below reset threshold; trigger when hold time elapses |
| `PLAYING_ALERT` | Hold timer expires | Main loop attempts WAV playback; microphone values are not evaluated |
| `COOLDOWN` | Playback attempt ends | Ignore alert conditions for the configured cooldown duration |
| `WAITING_FOR_QUIET` | Cooldown expires | Require level <= reset threshold continuously for the quiet-rearm interval |

The controller uses unsigned elapsed-time subtraction (`now - startedAt`) so timer comparisons remain safe across the normal `millis()` rollover.

The reset threshold is lower than the trigger threshold. This creates hysteresis: small fluctuations near the trigger threshold do not constantly reset the hold timer. After playback, sustained high noise cannot trigger repeated reminders; the controller must first observe a quiet interval.

## 5. Amplifier control

GPIO4 controls the MAX98357A shutdown/SD_MODE pin:

- LOW: shutdown.
- HIGH: enabled for playback.

The firmware enables the amplifier only after it has opened and validated the WAV file, waits briefly for the amplifier to wake, writes audio blocks, writes a short silence tail, and then drives the shutdown pin LOW.

The amplifier breakout must expose a compatible shutdown pin. Verify the breakout's labeling and circuit before wiring this GPIO.

## 6. WAV playback

The player reads `/alert.wav` from LittleFS. It supports only:

- RIFF/WAVE container.
- Uncompressed PCM (`format = 1`).
- Mono audio.
- 16,000 samples per second.
- 16-bit little-endian samples.

The parser walks RIFF chunks, skips unknown chunks, and checks that the format and data sizes fit inside the declared RIFF boundary and the physical file. It uses 64-bit position arithmetic and subtraction-based range checks before every seek, so a corrupted size such as `0xFFFFFFF8` is rejected rather than wrapping a 32-bit seek target. The parser is a platform-independent C++ module and is host-tested with valid, malformed, and oversized chunk cases.

The player streams short blocks instead of loading the whole WAV into RAM. TX write errors or stalled writes are reported; the amplifier is shut down after the playback attempt.

When replacing the WAV, export it in the exact supported format and run `pio run -t uploadfs`.

## 7. NVS settings and CLI

Arduino-ESP32 `Preferences` stores small configuration values in the ESP32 NVS partition. Current keys are:

| Key | Setting |
|---|---|
| `trigger` | Trigger threshold |
| `reset` | Quiet/reset threshold |
| `cal` | Calibration offset |
| `gain` | Playback gain |

The Serial command handler accepts one command per line. Invalid numeric values and invalid threshold ordering are rejected. Successful setting changes are written to NVS immediately.

A `cal` value outside the ±20 dB offset range can only come from firmware that stored the absolute 0 dBFS reference, so `loadSettings()` subtracts the 120 dB reference before clamping. `calibration::normalizeStoredOffset()` implements the same rule and is covered by `tests/test_calibration.cpp`, but the firmware currently performs the conversion inline rather than calling it. See the known-issues list in the [README](../README.md#11-known-issues).

For this MVP, hold duration, cooldown duration, and quiet-rearm duration remain compile-time constants in `include/config.h`.

## 8. LED status patterns

The XIAO ESP32-S3 onboard user LED is active low. Version C uses the LED as a simple status indicator, driven from the main loop with `millis()` phases:

| State | Pattern | Timing |
|---|---|---|
| `MONITORING` | Slow heartbeat | 70 ms on, 1930 ms off |
| `THRESHOLD_HOLD` | Rapid flash | 80 ms on, 80 ms off |
| `PLAYING_ALERT` | Solid on | Set once before blocking playback; not refreshed while audio runs |
| `COOLDOWN` | Single periodic pulse | 100 ms on, 1100 ms off |
| `WAITING_FOR_QUIET` | Double pulse | 70 ms on, 110 ms off, 70 ms on, 950 ms off |
| Low battery (overrides the state pattern) | Two short blinks every 3 s | 60 ms on, 140 ms off, 60 ms on, 2740 ms off |
| Fatal audio initialization error | Fast blink | 125 ms on, 125 ms off |

The `PLAYING_ALERT` row deserves a caveat: playback is blocking, so the pattern is written once before `alert_sound::play()` is entered and never updated until it returns.

## 8a. Power and battery policy

`setup()` sets the CPU to 80 MHz before any other initialisation. The WiFi and
Bluetooth stacks are never started, so their radios stay off without linking
those libraries into the image.

The pack is sampled every 10 s through the D1 divider:

| Status | Threshold | Action |
|---|---:|---|
| `Absent` | < 2.50 V | No warning; treated as USB power |
| `Ok` | ≥ 3.50 V | Nothing |
| `Low` | 3.30–3.50 V | LED double-blink, one Serial warning per episode |
| `Critical` | < 3.30 V | Amplifier shut down, timer deep sleep, re-check after 10 minutes |

The first check runs before the I2S bus is brought up, so a critical cell is
never asked to drive the amplifier. `STATUS` reports the last averaged voltage,
the estimated state of charge, and which reminder source is active.

## 8b. Reminder source selection

`alert_sound::play()` prefers `/alert.wav` and falls back to a synthesised
three-note chime when the file is missing, unreadable, or not 16 kHz mono
16-bit PCM. An empty filesystem therefore produces a working reminder instead
of a silent device. `STATUS` prints `reminder=/alert.wav` or
`reminder=built-in chime` so the active source is visible without listening.

The fallback also covers a partial WAV failure: if playback starts and then a write error occurs, the chime plays immediately afterwards. A single alert can therefore produce a truncated WAV followed by a chime.

`TEST_ALERT` routes through the same function, so a manual test produces the
same source selection as an automatic alert. It also calls
`AlertController::onPlaybackCompleted()`, which means a manual test starts the
normal cooldown and suppresses automatic alerts for the cooldown period plus
the quiet-rearm interval. This is intentional — a test session should not be
interrupted by the device — but it is easy to mistake for a broken detector.

## 9. Deliberate MVP limitations

- No A-weighting or dBA output.
- No audio recording or speech recognition.
- No Wi-Fi, Bluetooth, cloud service, or mobile app.
- Battery percentage is a piecewise-linear estimate, not a fuel gauge.
- No runtime configuration for hold/cooldown/quiet durations.
- No multitasking audio pipeline; alert playback is blocking while the monitor loop is paused.

These limitations keep the first hardware bring-up manageable. Add features only after the end-to-end monitor-to-reminder loop is stable.
