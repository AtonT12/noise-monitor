#include "audio_bus.h"

#include <algorithm>
#include <cmath>

#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"

namespace {

i2s_chan_handle_t gTxHandle = nullptr;
i2s_chan_handle_t gRxHandle = nullptr;
bool gReady = false;

constexpr size_t kWriteChunkFrames = 128;
constexpr size_t kSilenceBufferFrames = 128;

bool writeRawBytes(const void* data, size_t byteCount) {
  if (!gReady || gTxHandle == nullptr || data == nullptr || byteCount == 0) {
    return false;
  }

  const auto* bytes = static_cast<const uint8_t*>(data);
  size_t offset = 0;
  constexpr size_t kBytesPerStereoFrame = 2U * sizeof(int32_t);

  while (offset < byteCount) {
    size_t bytesWritten = 0;
    const esp_err_t err = i2s_channel_write(
        gTxHandle, bytes + offset, byteCount - offset, &bytesWritten,
        pdMS_TO_TICKS(config::kI2sTimeoutMs));

    if (err != ESP_OK || bytesWritten == 0 ||
        (bytesWritten % kBytesPerStereoFrame) != 0) {
      Serial.printf("[AUDIO] I2S TX write failed: %s, bytes=%u\n",
                    esp_err_to_name(err), static_cast<unsigned>(bytesWritten));
      return false;
    }
    offset += bytesWritten;
  }
  return true;
}

}  // namespace

namespace audio_bus {

bool begin() {
  if (gReady) {
    return true;
  }

  pinMode(static_cast<uint8_t>(config::kAmplifierSdPin), OUTPUT);
  digitalWrite(static_cast<uint8_t>(config::kAmplifierSdPin), LOW);

  i2s_chan_config_t channelConfig =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.dma_desc_num = 6;
  channelConfig.dma_frame_num = 256;

  esp_err_t err = i2s_new_channel(&channelConfig, &gTxHandle, &gRxHandle);
  if (err != ESP_OK) {
    Serial.printf("[AUDIO] i2s_new_channel failed: %s\n", esp_err_to_name(err));
    gTxHandle = nullptr;
    gRxHandle = nullptr;
    return false;
  }

  // Configure TX and RX explicitly rather than relying on post-startup
  // reconfiguration. TX must transmit both slots: the playback helper writes
  // [left, right] pairs containing the same sample in each slot.
  i2s_std_config_t txConfig = {};
  txConfig.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config::kSampleRateHz);
  txConfig.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  txConfig.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
  txConfig.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  txConfig.gpio_cfg.bclk = config::kI2sBclkPin;
  txConfig.gpio_cfg.ws = config::kI2sWsPin;
  txConfig.gpio_cfg.dout = config::kI2sDoutPin;
  txConfig.gpio_cfg.din = I2S_GPIO_UNUSED;
  txConfig.gpio_cfg.invert_flags.mclk_inv = false;
  txConfig.gpio_cfg.invert_flags.bclk_inv = false;
  txConfig.gpio_cfg.invert_flags.ws_inv = false;

  // RX remains stereo/BOTH so DMA returns a stable interleaved two-word frame.
  // The INMP441 L/R pin is strapped to GND, so its samples occupy the left
  // slot; NoiseMeter intentionally consumes only the even-indexed word.
  // Switching RX to mono/LEFT would change the DMA buffer layout and would
  // require changing readMicrophoneFrames() and NoiseMeter together.
  i2s_std_config_t rxConfig = txConfig;
  rxConfig.gpio_cfg.dout = I2S_GPIO_UNUSED;
  rxConfig.gpio_cfg.din = config::kI2sDinPin;
  rxConfig.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

  err = i2s_channel_init_std_mode(gTxHandle, &txConfig);
  if (err != ESP_OK) {
    Serial.printf("[AUDIO] TX init failed: %s\n", esp_err_to_name(err));
    end();
    return false;
  }

  err = i2s_channel_init_std_mode(gRxHandle, &rxConfig);
  if (err != ESP_OK) {
    Serial.printf("[AUDIO] RX init failed: %s\n", esp_err_to_name(err));
    end();
    return false;
  }

  err = i2s_channel_enable(gTxHandle);
  if (err != ESP_OK) {
    Serial.printf("[AUDIO] TX enable failed: %s\n", esp_err_to_name(err));
    end();
    return false;
  }

  err = i2s_channel_enable(gRxHandle);
  if (err != ESP_OK) {
    Serial.printf("[AUDIO] RX enable failed: %s\n", esp_err_to_name(err));
    end();
    return false;
  }

  gReady = true;
  Serial.println("[AUDIO] I2S full-duplex ready: 16 kHz, 32-bit stereo, shared clocks.");
  return true;
}

void end() {
  digitalWrite(static_cast<uint8_t>(config::kAmplifierSdPin), LOW);
  gReady = false;

  if (gRxHandle != nullptr) {
    i2s_channel_disable(gRxHandle);
    i2s_del_channel(gRxHandle);
    gRxHandle = nullptr;
  }
  if (gTxHandle != nullptr) {
    i2s_channel_disable(gTxHandle);
    i2s_del_channel(gTxHandle);
    gTxHandle = nullptr;
  }
}

bool isReady() {
  return gReady;
}

size_t readMicrophoneFrames(int32_t* interleavedWords, size_t frameCapacity,
                            uint32_t timeoutMs) {
  if (!gReady || gRxHandle == nullptr || interleavedWords == nullptr ||
      frameCapacity == 0) {
    return 0;
  }

  const size_t requestedBytes =
      frameCapacity * config::kI2sSlotsPerFrame * sizeof(int32_t);
  size_t bytesRead = 0;
  const esp_err_t err = i2s_channel_read(
      gRxHandle, interleavedWords, requestedBytes, &bytesRead,
      pdMS_TO_TICKS(timeoutMs));

  if (err != ESP_OK || bytesRead == 0) {
    return 0;
  }

  const size_t bytesPerFrame = config::kI2sSlotsPerFrame * sizeof(int32_t);
  return bytesRead / bytesPerFrame;
}

bool writeSpeakerMono16(const int16_t* samples, size_t sampleCount, float gain) {
  if (!gReady || gTxHandle == nullptr || samples == nullptr || sampleCount == 0) {
    return false;
  }

  gain = constrain(gain, config::kMinPlaybackGain, config::kMaxPlaybackGain);
  int32_t stereoBuffer[kWriteChunkFrames * config::kI2sSlotsPerFrame];

  size_t offset = 0;
  while (offset < sampleCount) {
    const size_t batch = std::min(kWriteChunkFrames, sampleCount - offset);
    for (size_t i = 0; i < batch; ++i) {
      const float scaled = static_cast<float>(samples[offset + i]) * gain;
      const int32_t sample16 = static_cast<int32_t>(
          constrain(scaled, -30000.0F, 30000.0F));
      // MAX98357A receives identical data in left and right slots. This avoids
      // dependency on the breakout's left/right channel strap setting.
      const int32_t sample32 = sample16 * 65536L;
      stereoBuffer[i * 2] = sample32;
      stereoBuffer[i * 2 + 1] = sample32;
    }

    const size_t bytes = batch * config::kI2sSlotsPerFrame * sizeof(int32_t);
    if (!writeRawBytes(stereoBuffer, bytes)) {
      return false;
    }
    offset += batch;
  }
  return true;
}

bool writeSilence(uint32_t durationMs) {
  if (!gReady || gTxHandle == nullptr) {
    return false;
  }

  int32_t silence[kSilenceBufferFrames * config::kI2sSlotsPerFrame] = {};
  const size_t totalFrames =
      (static_cast<size_t>(durationMs) * config::kSampleRateHz) / 1000U;
  size_t remaining = totalFrames;
  while (remaining > 0) {
    const size_t batch = std::min(kSilenceBufferFrames, remaining);
    const size_t bytes = batch * config::kI2sSlotsPerFrame * sizeof(int32_t);
    if (!writeRawBytes(silence, bytes)) {
      return false;
    }
    remaining -= batch;
  }
  return true;
}

void setAmplifierEnabled(bool enabled) {
  digitalWrite(static_cast<uint8_t>(config::kAmplifierSdPin), enabled ? HIGH : LOW);
  if (enabled) {
    delay(config::kAmplifierWakeDelayMs);
  }
}

void discardMicrophoneFor(uint32_t durationMs) {
  if (!gReady || gRxHandle == nullptr) {
    return;
  }

  int32_t discardBuffer[128 * config::kI2sSlotsPerFrame];
  const uint32_t startedAt = millis();
  while (static_cast<uint32_t>(millis() - startedAt) < durationMs) {
    const size_t frames = readMicrophoneFrames(discardBuffer, 128, 100);
    if (frames == 0) {
      delay(1);
    }
  }
}

}  // namespace audio_bus
