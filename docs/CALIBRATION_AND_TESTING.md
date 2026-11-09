# Calibration and Testing

## 1. Test sequence

Complete the tests in order. Do not start by placing the electronics inside the plush toy.

### Test A — Firmware and serial connection

1. Open the project in PlatformIO.
2. Run `pio run`.
3. Connect the XIAO ESP32-S3 by USB.
4. Run `pio run -t upload`.
5. Open `pio device monitor -b 115200`.
6. Confirm the boot banner and I2S initialization message appear.

Expected messages include:

```text
Noise Monitor MVP - Version C
[AUDIO] I2S full-duplex ready: 16 kHz, 32-bit stereo, shared clocks.
[WAV] LittleFS mounted.
[READY] Noise monitoring started.
```

### Test B — Microphone input

1. Connect the INMP441 only.
2. Keep L/R tied to GND.
3. Confirm the serial `dBFS` and `SPL_est` values change when the room sound changes.
4. Check that covering or uncovering the microphone port changes the reading in a plausible direction.
5. Do not interpret the initial `SPL_est` value as an accurate SPL measurement.

### Test C — Speaker output

1. Connect the MAX98357A and speaker.
2. Confirm that the amplifier VIN is supplied from a suitable 2.5–5.5 V source and all grounds are common.
3. Upload the filesystem using `pio run -t uploadfs`.
4. Send `SET_VOL 0.10` in the Serial Monitor.
5. Send `TEST_ALERT` and verify the short test chime plays.
6. Increase the gain in small steps only if necessary; listen for distortion and check the speaker and amplifier for unusual heating.

### Test D — Trigger policy

For bench testing, lower the trigger threshold with `SET_THRESH` and adjust the reset threshold with `SET_RESET`. Keep the reset threshold below the trigger threshold. Create a sustained sound source above the threshold for longer than the hold time.

Verify all of the following:

- A short transient does not cause a reminder.
- A sustained high level causes one reminder.
- The device stays in cooldown after playback.
- The device does not repeat reminders while noise remains high.
- The device re-arms only after cooldown has elapsed and the environment stays below the reset threshold for the quiet-rearm interval.
- A new sustained high-noise event can trigger after re-arming.

Return the threshold values to appropriate test settings after bench testing.

## 2. Calibration procedure

The firmware computes an unweighted estimate:

```text
dBFS = 20 * log10(RMS)
SPL_estimate = dBFS + 120 dB + calibration_offset
```

The 120 dB term is the INMP441 full-scale reference (−26 dBFS at 94 dB SPL) and
is fixed in `calibration.h`. The offset starts at 0 dB and is the only value the
user adjusts; it is limited to ±20 dB, which covers normal enclosure losses.

Recommended prototype calibration process:

1. Assemble the microphone in the intended enclosure, because fabric and internal placement can change the acoustic response.
2. Place the device and a reference sound level meter close together, with their microphone openings in similar positions and orientations.
3. Use a stable test sound and record the firmware's `dBFS` and the reference meter's displayed value.
4. Calculate a first offset: `offset = reference_level - measured_SPL_estimate`.
5. Set the offset with `SET_CAL <value>` (±20 dB).
6. Repeat at multiple sound levels and, where possible, multiple frequencies. A single offset is not sufficient to correct frequency response or non-linearity.
7. Record the test conditions, reference instrument, enclosure state, and any remaining error.

This procedure is suitable for improving a prototype threshold detector. It does not establish IEC 61672 compliance or certified measurement accuracy. The firmware does not apply A-weighting, so do not display or describe its result as dBA.

## 3. DSP and alert logic validation

Run the host-side test suite from the project root:

```bash
bash tools/run_host_tests.sh
```

The C++ host test suite checks the alert controller and RIFF/WAV parser:

- Trigger threshold equality and the exact hold-duration boundary.
- Cooldown equality and the exact quiet-rearm boundary.
- Falling below the reset threshold cancels a pending hold.
- Continued high sound does not repeatedly trigger alerts.
- A new event can trigger after re-arming.
- Timer arithmetic works across `uint32_t` rollover.
- A valid WAV containing an odd-sized unknown RIFF chunk parses correctly.
- Oversized `JUNK` and `fmt ` chunks declaring `0xFFFFFFF8` bytes fail cleanly.

Run `bash tools/run_host_tests.sh` to compile both test programs with strict warnings enabled. Host tests do not test I2S, LittleFS hardware behavior, the microphone, the amplifier, or the speaker.

## 4. Basic acceptance checklist

- [ ] Firmware compiles in PlatformIO.
- [ ] Firmware uploads to XIAO ESP32-S3.
- [ ] Serial telemetry appears consistently.
- [ ] Microphone levels respond to environmental sound.
- [ ] `/alert.wav` mounts and passes format validation.
- [ ] `TEST_ALERT` plays without clipping or unexpected noise.
- [ ] A sustained threshold crossing triggers one reminder.
- [ ] The system waits for quiet before re-arming.
- [ ] NVS settings survive a restart.
- [ ] Amplifier enters shutdown after playback.
- [ ] Battery, board, wires, and speaker are mechanically secured before enclosure integration.

## 5. Troubleshooting guide

| Issue | Checks |
|---|---|
| `i2s_new_channel` fails | Confirm the correct XIAO ESP32-S3 board target and that no other library has already allocated I2S0 |
| TX or RX init fails | Confirm the shared BCLK/WS assignments and the selected standard I2S mode |
| Mic stays near -120 dBFS | Check microphone 3.3 V, common GND, SCK, WS, SD, and L/R strap; verify that the module is actually an I2S INMP441 breakout |
| Level is consistently offset | Recheck 24-bit alignment and use a reference meter to adjust `SET_CAL` |
| WAV is rejected | Re-export as PCM mono 16 kHz / 16-bit WAV, then upload the LittleFS image; malformed RIFF chunks and unsupported chunk boundaries are rejected intentionally |
| Amplifier does not wake | Confirm GPIO4 reaches the breakout SD/SD_MODE pin and that the breakout uses active-low shutdown |
| Speaker is quiet | Check the breakout's gain strap and supply voltage; raise software gain only in small steps |
| Speaker distorts | Reduce gain, check speaker impedance/rating, and verify the module supply and wiring |
| Settings do not persist | Check `[NVS]` logs and verify the NVS partition is present in the partition table |
