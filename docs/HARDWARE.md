# Hardware and Wiring

## 1. Hardware topology

Version C uses one I2S clock domain. The ESP32-S3 generates BCLK and WS. The INMP441 sends its left-slot data to the ESP32-S3 DIN pin, while the MAX98357A receives duplicated left/right audio from the ESP32-S3 DOUT pin.

```text
                       Seeed Studio XIAO ESP32-S3
                    +--------------------------------+
INMP441 SCK  <-------| GPIO7 / D8  : I2S BCLK         |
INMP441 WS   <-------| GPIO8 / D9  : I2S WS           |
INMP441 SD   --------| GPIO1 / D0  : I2S DIN          |
MAX98357A DIN <------| GPIO9 / D10 : I2S DOUT         |
MAX98357A BCLK <-----| GPIO7 / D8  : shared BCLK      |
MAX98357A LRC  <-----| GPIO8 / D9  : shared WS        |
MAX98357A SD   <-----| GPIO4 / D3  : amplifier enable |
                    +--------------------------------+
```

All modules must share GND. The ESP32-S3 is the I2S clock master. TX and RX are paired channels on I2S controller 0 and share BCLK/WS. The firmware initializes TX and RX with explicit stereo slot masks; TX is explicitly configured for `I2S_STD_SLOT_BOTH`, so both left and right slots carry the reminder samples.

## 2. Pin assignment

| XIAO label | ESP32-S3 GPIO | Signal | Connection |
|---|---:|---|---|
| D8 | GPIO7 | I2S BCLK | INMP441 SCK and MAX98357A BCLK |
| D9 | GPIO8 | I2S WS/LRCLK | INMP441 WS and MAX98357A LRC/WS |
| D10 | GPIO9 | I2S DOUT | MAX98357A DIN |
| D0 | GPIO1 | I2S DIN | INMP441 SD |
| D3 | GPIO4 | Amplifier enable | MAX98357A SD_MODE / SD |
| D1 | GPIO2 | Battery sense | Divider mid-point (ADC1_CH1) |
| 3V3 | 3.3 V | Microphone supply | INMP441 VDD |
| GND | GND | Common ground | INMP441 GND, amplifier GND, battery negative |
| USER_LED | GPIO21 | Status LED | Onboard XIAO user LED; active low |

The code uses ESP32-S3 GPIO numbers. Check the pin map for the exact board revision before connecting anything.

## 3. INMP441 microphone wiring

| INMP441 module pin | Connect to | Notes |
|---|---|---|
| VDD | XIAO 3V3 | Use 3.3 V, not 5 V |
| GND | XIAO GND | Common ground |
| SCK | XIAO D8 / GPIO7 | I2S bit clock |
| WS | XIAO D9 / GPIO8 | I2S word select |
| SD | XIAO D0 / GPIO1 | Microphone data into ESP32-S3 |
| L/R | GND | Selects the left I2S slot |

The firmware reads the left sample from each stereo I2S frame. If the microphone L/R pin is strapped differently, update the slot selection and sample extraction together.

## 3a. Battery voltage sense

The XIAO ESP32-S3 has no battery-sense output, so the pack voltage is measured
through an external divider:

```text
BAT+ ---[ 100k ]---+--- D1 / GPIO2 (ADC1_CH1)
                   |
                [ 100k ]
                   |
                  GND
```

- Divider ratio 2.0; a full pack (4.2 V) presents 2.1 V to the pin, inside the
  default 11 dB ADC range.
- The divider draws about 21 uA continuously. For a 1500 mAh cell that is under
  0.1 % of the runtime budget.
- **Fit the divider even if you only ever run from USB.** Without it the ADC
  pin floats, and a floating reading between 2.5 V and 3.3 V (after the ratio)
  is indistinguishable from a critical battery, which triggers the protective
  deep sleep.
- Use 1 % resistors; a 5 % pair adds roughly +/-0.1 V of error, which is more
  than the gap between the low (3.50 V) and critical (3.30 V) thresholds.
- The firmware averages 16 calibrated `analogReadMilliVolts()` samples every
  10 s, so no extra filtering hardware is required.

## 4. MAX98357A amplifier and speaker wiring

| MAX98357A module pin | Connect to | Notes |
|---|---|---|
| VIN | Protected battery positive rail (BAT+) | MAX98357A supply range is 2.5–5.5 V; the single-cell battery rail varies with charge |
| GND | Common GND | Must share ground with XIAO |
| BCLK | XIAO D8 / GPIO7 | Shared I2S BCLK |
| LRC / WS | XIAO D9 / GPIO8 | Shared I2S WS |
| DIN | XIAO D10 / GPIO9 | I2S data from ESP32-S3 |
| SD / SD_MODE | XIAO D3 / GPIO4 | LOW = shutdown; HIGH = enabled on common breakouts; verify breakout pin labeling |
| GAIN | Leave at breakout default for first test | Do not assume the breakout's gain strap is identical across vendors |
| SPK+ / SPK- | Speaker + / - | Connect the speaker only across these two terminals |

The MAX98357A accepts a 2.5–5.5 V supply. A protected one-cell Li-Po rail is nominally 3.7 V and varies during discharge, which is within the IC's specified range. A small local bulk capacitor (for example, 100–220 µF, with suitable voltage rating) near the amplifier supply can help with burst current; follow the breakout manufacturer's layout guidance and observe polarity.

**Do not power the amplifier from the XIAO 5V/VBUS pin when the board is running from its battery.** The 5V/VBUS pin is not a boost converter output. The recommended wiring uses the battery rail for the amplifier, and the XIAO 3V3 rail for the microphone.

**Speaker warning:** MAX98357A outputs are bridge-tied. Neither `SPK+` nor `SPK-` is ground. Do not connect either output to GND or to another amplifier output.

The firmware duplicates the same audio samples in both I2S slots. The native ESP-IDF TX channel is initialized in stereo mode with `I2S_STD_SLOT_BOTH`; no post-startup `configureTX()` call is used. This makes the reminder independent of whether the amplifier breakout selects left, right, or a mono mix.

The RX channel also uses stereo/BOTH so each DMA frame has a stable interleaved left/right layout. The INMP441 L/R pin is physically strapped to GND, so its signal is in the left slot; the firmware processes the left word only. Do not change RX to mono/LEFT without also changing the DMA frame sizing and sample extraction code.

GPIO4 is used to put the amplifier into shutdown between playback events. Check that the breakout exposes a compatible active-low shutdown/SD_MODE pin before connecting GPIO4.

## 5. Battery and charging

- Use a protected, pre-terminated 3.7 V single-cell Li-Po battery in the approximate 800–1200 mAh range for the first prototype.
- Connect it only to the battery connector/pads intended by the exact XIAO board revision, with correct polarity.
- Do not solder directly to a bare pouch cell; use a battery with a protected lead/connector.
- Keep the battery mechanically isolated from the speaker frame, board edges, solder joints, and any object that could puncture or compress it.
- Do not charge the battery while it is buried in plush filling. Use a removable electronics compartment and inspect the battery and wiring regularly.
- The speaker amplifier is powered from the battery rail, so its supply voltage and available output power change with battery voltage.

The software gain is a digital signal multiplier, not a calibrated power limiter. Start with the default gain, listen at close range in a controlled test, and verify speaker temperature and distortion. A software gain value cannot guarantee that the speaker remains below its rated power for every breakout gain setting and supply voltage.

## 6. Physical placement in a 20 cm plush toy

| Component | Suggested location | Design notes |
|---|---|---|
| Microphone | Head / upper face | Keep the acoustic port exposed; do not pack filling against the port |
| XIAO ESP32-S3 | Central torso | Mount on an insulated carrier board or standoffs |
| Amplifier | Lower torso, near speaker | Keep speaker current wiring short; leave room for airflow |
| Speaker | Lower torso or back | Aim the sound opening away from the microphone port |
| Battery | Lower torso in a separate pocket | Prevent compression and make it removable |
| USB-C access | Rear seam / service flap | Must remain accessible for programming and charging |

Separate the microphone opening and speaker opening to reduce acoustic coupling. The firmware blocks microphone evaluation while the speaker is playing and discards buffered audio afterward, but physical separation still improves reliability.

## 7. Bring-up order

1. Test the XIAO ESP32-S3 alone over USB.
2. Connect the INMP441 only and verify microphone telemetry.
3. Connect the MAX98357A and speaker, initially using USB power for the XIAO and a suitable amplifier supply.
4. Run `TEST_ALERT` at low software gain.
5. Test battery supply only after the USB-powered prototype behaves correctly.
6. Integrate the assembly into a removable, insulated compartment after bench tests pass.
