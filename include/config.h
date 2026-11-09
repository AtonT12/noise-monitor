#pragma once

#include <Arduino.h>
#include <driver/gpio.h>

namespace config {

// Board: Seeed Studio XIAO ESP32-S3.
// GPIO numbers are used in code; XIAO header labels are shown in HARDWARE.md.
constexpr gpio_num_t kI2sBclkPin = GPIO_NUM_7;   // D8
constexpr gpio_num_t kI2sWsPin = GPIO_NUM_8;     // D9
constexpr gpio_num_t kI2sDoutPin = GPIO_NUM_9;   // D10 -> MAX98357A DIN
constexpr gpio_num_t kI2sDinPin = GPIO_NUM_1;    // D0 <- INMP441 SD
constexpr gpio_num_t kAmplifierSdPin = GPIO_NUM_4;  // D3; LOW=shutdown, HIGH=enabled
constexpr gpio_num_t kBatteryAdcPin = GPIO_NUM_2;   // D1 <- 100k/100k divider mid-point
constexpr gpio_num_t kUserLedPin = GPIO_NUM_21;  // Active-low onboard user LED

constexpr uint32_t kSerialBaudRate = 115200;

// The DSP loop needs a few percent of one core at 16 kHz. 80 MHz cuts the
// MCU share of the battery budget by roughly a third versus the 240 MHz
// default. The WiFi/BT stacks are never initialised in this firmware, so
// their radios stay off without linking those libraries in.
constexpr uint32_t kCpuFrequencyMhz = 80;

// Full-duplex I2S uses one shared clock domain and 32-bit stereo slots.
constexpr uint32_t kSampleRateHz = 16000;
constexpr size_t kAudioBlockFrames = 512;  // 32 ms at 16 kHz
constexpr size_t kI2sSlotsPerFrame = 2;
constexpr size_t kI2sWordsPerBlock = kAudioBlockFrames * kI2sSlotsPerFrame;
constexpr uint8_t kMicRightShift = 8;  // INMP441 24-bit payload in a 32-bit slot
constexpr float kMicFullScale = 8388608.0F;  // 2^23

// DSP. This is unweighted level estimation; no IEC A-weighting is implemented.
// The dBFS-to-SPL reference and the user calibration offset live in
// calibration.h, because they are a measurement-model concern rather than a
// board setting.
constexpr float kDbfsFloor = -120.0F;
constexpr float kDcBlockAlpha = 0.995F;
constexpr float kEmaAlpha = 0.25F;

// Reminder policy defaults; tune them for the intended room.
constexpr float kDefaultTriggerThresholdDb = 70.0F;
constexpr float kDefaultResetThresholdDb = 65.0F;
constexpr uint32_t kThresholdHoldMs = 3000;
constexpr uint32_t kCooldownMs = 30000;
constexpr uint32_t kQuietRearmMs = 2000;
constexpr uint32_t kPostPlaybackDiscardMs = 500;

// Audio playback. Start quietly and verify the actual speaker level.
constexpr char kAlertWavPath[] = "/alert.wav";
constexpr float kDefaultPlaybackGain = 0.25F;
constexpr float kMinPlaybackGain = 0.05F;
constexpr float kMaxPlaybackGain = 0.65F;
constexpr uint32_t kAmplifierWakeDelayMs = 15;
constexpr uint32_t kI2sTimeoutMs = 1000;
constexpr uint32_t kChimeTailMs = 40;

// Battery monitoring. The DIVIDER MUST BE FITTED: 100k from BAT+ to the ADC
// pin, 100k from the ADC pin to GND. Without it the pin floats and the
// critical-battery check can trigger a protective sleep on a USB-powered unit.
// A protected single-cell Li-Po reads 4.2 V full and 3.3 V empty.
constexpr float kBatteryDividerRatio = 2.0F;
constexpr uint32_t kBatteryCheckIntervalMs = 10000;
constexpr float kBatteryAbsentVolts = 2.50F;
constexpr float kBatteryLowVolts = 3.50F;
constexpr float kBatteryCriticalVolts = 3.30F;
constexpr uint32_t kBatterySleepRecheckMinutes = 10;

constexpr char kNvsNamespace[] = "noisemon";

}  // namespace config
