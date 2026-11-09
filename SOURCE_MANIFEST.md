# Source Manifest — Noise Monitor MVP Version C

| Path | Type | Purpose |
|---|---|---|
| `platformio.ini` | PlatformIO | Target board, framework, filesystem and compiler configuration |
| `partitions.csv` | CSV | 8 MB flash partition layout with NVS, two app slots, LittleFS and coredump |
| `include/config.h` | C++ header | GPIO map, audio format, DSP constants, battery thresholds and MVP defaults |
| `include/calibration.h` | C++ header | dBFS-to-SPL reference, offset limits and NVS value interpretation |
| `include/audio_bus.h` | C++ header | Full-duplex I2S API |
| `include/alert_sound.h` | C++ header | WAV-or-chime reminder selection |
| `include/battery_monitor.h` | C++ header | Pack voltage sampling and status classification |
| `src/audio_bus.cpp` | C++ source | ESP-IDF I2S TX/RX, amplifier control and PCM output |
| `src/alert_sound.cpp` | C++ source | Three-note chime synthesiser and fallback selection |
| `src/battery_monitor.cpp` | C++ source | ADC averaging, thresholds and state-of-charge curve |
| `src/calibration.cpp` | C++ source | Legacy NVS calibration conversion and clamping |
| `include/noise_meter.h` | C++ header | Noise measurement interface |
| `src/noise_meter.cpp` | C++ source | INMP441 data extraction, DC block, RMS, dBFS and EMA |
| `include/alert_controller.h` | C++ header | Alert state machine interface |
| `src/alert_controller.cpp` | C++ source | Hold, hysteresis, cooldown and quiet-to-rearm policy |
| `include/wav_parser.h` | C++ header | Platform-independent seekable WAV reader interface and parsed PCM metadata |
| `src/wav_parser.cpp` | C++ source | RIFF/WAVE chunk parser with overflow-safe bounds checks |
| `include/wav_player.h` | C++ header | LittleFS WAV playback interface |
| `src/wav_player.cpp` | C++ source | LittleFS adapter and chunked I2S playback |
| `src/main.cpp` | C++ source | Application startup, NVS, Serial CLI, battery policy, telemetry and LED |
| `data/alert.wav` | WAV | Included low-level test chime; replace with a spoken prompt if desired |
| `tools/generate_test_tone.py` | Python | Regenerate the test WAV |
| `tools/run_host_tests.sh` | Shell | Compile and run the three host-side C++ test programs |
| `tests/test_alert_controller.cpp` | C++ test | State-machine boundary and timing tests |
| `tests/test_wav_parser.cpp` | C++ test | Valid and malformed RIFF/WAV chunk tests |
| `tests/test_calibration.cpp` | C++ test | Legacy calibration conversion, clamping and idempotence |
| `docs/HARDWARE.md` | Markdown | Wiring, supply and plush-enclosure guidance |
| `docs/FIRMWARE.md` | Markdown | Firmware modules and runtime behavior |
| `docs/CALIBRATION_AND_TESTING.md` | Markdown | Bring-up sequence, calibration and acceptance tests |
| `docs/ARCHITECTURE_DECISIONS.md` | Markdown | Version A/B feature choices and Version C design rationale |
| `docs/PATCH_NOTES.md` | Markdown | Review findings, corrective changes, and validation scope |
| `docs/VALIDATION_STATUS.md` | Markdown | Exact validation performed and outstanding checks |
| `README.md` | Markdown | Project overview and quick start |
| `LICENSE` | Text | Project license |
