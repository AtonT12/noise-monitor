#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

namespace audio_bus {

// Start one ESP32-S3 I2S controller with paired TX/RX channels in full-duplex
// standard mode. Both directions share BCLK and WS at 16 kHz, 32-bit stereo.
bool begin();
void end();
bool isReady();

// Read interleaved stereo 32-bit frames. The INMP441 is strapped to its LEFT
// slot; callers should use the even-indexed word from each frame.
size_t readMicrophoneFrames(int32_t* interleavedWords, size_t frameCapacity,
                            uint32_t timeoutMs = 100);

// Write mono signed 16-bit PCM to both I2S slots. The helper converts it to
// left-aligned 32-bit stereo words and applies a software gain.
bool writeSpeakerMono16(const int16_t* samples, size_t sampleCount, float gain);
bool writeSilence(uint32_t durationMs);

// The MAX98357A SD_MODE pin is used as an active-high enable/shutdown control.
void setAmplifierEnabled(bool enabled);
void discardMicrophoneFor(uint32_t durationMs);

}  // namespace audio_bus
