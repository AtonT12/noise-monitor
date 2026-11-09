#include "wav_player.h"

#include <LittleFS.h>
#include <algorithm>
#include <cstdint>
#include <limits>

#include "audio_bus.h"
#include "config.h"
#include "wav_parser.h"

namespace {

constexpr size_t kChunkSamples = 256;
constexpr size_t kChunkBytes = kChunkSamples * sizeof(int16_t);

class LittleFsWavReader final : public WavReader {
 public:
  explicit LittleFsWavReader(File& file) : file_(file) {}

  uint64_t position() const override {
    return static_cast<uint64_t>(file_.position());
  }

  uint64_t size() const override {
    return static_cast<uint64_t>(file_.size());
  }

  size_t read(uint8_t* destination, size_t count) override {
    return file_.read(destination, count);
  }

  bool seek(uint64_t absolutePosition) override {
    if (absolutePosition > size() ||
        absolutePosition > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
      return false;
    }
    return file_.seek(static_cast<uint32_t>(absolutePosition));
  }

 private:
  File& file_;
};

}  // namespace

namespace wav_player {

bool beginFilesystem() {
  if (!LittleFS.begin(false)) {
    Serial.println("[WAV] LittleFS mount failed; no auto-format was attempted.");
    return false;
  }
  Serial.println("[WAV] LittleFS mounted.");
  return true;
}

bool alertFileExists() {
  return LittleFS.exists(config::kAlertWavPath);
}

bool playAlert(float gain) {
  File file = LittleFS.open(config::kAlertWavPath, FILE_READ);
  if (!file) {
    Serial.println("[WAV] /alert.wav not found. Upload the LittleFS image.");
    return false;
  }

  LittleFsWavReader reader(file);
  WavPcmInfo wavInfo;
  if (!parsePcmWav(reader, wavInfo) ||
      wavInfo.sampleRate != config::kSampleRateHz ||
      !reader.seek(wavInfo.dataOffset)) {
    Serial.println(
        "[WAV] Invalid file. Expected PCM / mono / 16 kHz / 16-bit audio.");
    file.close();
    return false;
  }

  audio_bus::setAmplifierEnabled(true);
  bool success = true;
  uint32_t remainingBytes = wavInfo.dataBytes;
  int16_t pcm[kChunkSamples];

  while (remainingBytes >= sizeof(int16_t)) {
    const size_t requested =
        std::min(static_cast<size_t>(remainingBytes), kChunkBytes);
    const size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(pcm), requested);
    if (bytesRead < sizeof(int16_t) || bytesRead > requested ||
        (bytesRead % sizeof(int16_t)) != 0 || bytesRead > remainingBytes) {
      Serial.println("[WAV] Unexpected end of audio data.");
      success = false;
      break;
    }

    const size_t samplesRead = bytesRead / sizeof(int16_t);
    if (!audio_bus::writeSpeakerMono16(pcm, samplesRead, gain)) {
      Serial.println("[WAV] I2S playback failed.");
      success = false;
      break;
    }
    remainingBytes -= static_cast<uint32_t>(bytesRead);
  }

  file.close();
  // A short silence tail reduces audible clicks before hardware shutdown.
  const bool tailOk = audio_bus::writeSilence(40);
  audio_bus::setAmplifierEnabled(false);

  if (!tailOk) {
    success = false;
  }
  Serial.println(success ? "[WAV] Playback completed."
                         : "[WAV] Playback ended with an error.");
  return success;
}

}  // namespace wav_player
