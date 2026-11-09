# Version C — Architecture Decisions

This note explains which ideas were retained from the two earlier prototypes and why.

## 1. Decisions retained from Version A

- One I2S clock domain for microphone input and speaker output.
- A streaming LittleFS WAV player, so the reminder asset can be replaced without rebuilding firmware.
- A lower reset threshold than trigger threshold.
- A cooldown followed by a continuous-quiet requirement before re-arming.
- Explicit documentation that a calibration offset alone does not create a certified sound-level meter.

The main change is that Version C uses the ESP-IDF 5.x standard I2S driver directly rather than Arduino's `ESP_I2S.h` wrapper. Paired TX/RX handles are allocated together, configured with matching frame timing, and share BCLK/WS.

## 2. Features adopted from Version B

- Preferences/NVS persistence for small settings.
- A Serial CLI for calibration and threshold changes.
- DC-blocking high-pass filtering and EMA smoothing.
- A GPIO-controlled amplifier shutdown state.
- Onboard LED status patterns.

## 3. Corrections and constraints applied in Version C

- The reminder state machine does not re-arm merely because cooldown elapsed. It must also observe a quiet interval.
- WAV parsing checks the format, sample rate, channel count, sample width, RIFF chunk bounds, and data length before enabling the amplifier. Chunk-size arithmetic is overflow-safe and covered by host tests.
- The I2S TX slot mask is set to BOTH before the TX channel is initialized; the RX channel retains stereo interleaved frames because the microphone processing code consumes the left word from each frame.
- PlatformIO is pinned to `espressif32@8.0.0` for a reproducible Arduino-ESP32 Core 3.3.12 toolchain.
- TX errors and zero-byte/stalled writes are treated as playback failures.
- The amplifier is shut down after every playback attempt, including a failed write.
- The firmware labels readings as unweighted SPL estimates; A-weighting is not implemented.
- The playback gain is described as a software scale, not a guaranteed speaker power limiter.
- The 3.7 V battery rail is used for the MAX98357A supply in the recommended battery-powered wiring; the XIAO 5V/VBUS pad is not treated as a battery boost output.

## 4. Remaining work before a reliable physical prototype

1. Compile with the pinned PlatformIO platform (`espressif32@8.0.0`) and exact XIAO board revision being used.
2. Verify I2S full-duplex RX/TX on the physical board.
3. Confirm the INMP441 module's data alignment and left-slot behavior.
4. Confirm the MAX98357A breakout's SD_MODE polarity and gain strap.
5. Test battery rail behavior and speaker load at low playback gain.
6. Calibrate the assembled enclosure against a reference sound level meter.
7. Measure average current and practical runtime before making a battery-life claim.

## 5. Why this remains an MVP

Version C deliberately avoids Wi-Fi, Bluetooth, cloud telemetry, speech recognition, audio recording, automatic battery percentage, deep sleep, and a custom PCB. Those features add integration work without proving the core monitor-to-reminder loop. Add them only after the current audio, power, and enclosure behavior is stable.
