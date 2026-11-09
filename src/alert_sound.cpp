#include "alert_sound.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <Arduino.h>

#include "audio_bus.h"
#include "config.h"
#include "wav_player.h"

namespace {

constexpr float kTwoPi = 6.28318530718F;
constexpr uint32_t kChunkFrames = 128;

struct Note {
  float frequencyHz;
  uint32_t durationMs;
  float amplitude;
};

// Descending G5-E5-C5. Deliberately short and soft: the speaker sits inside a
// plush toy, so a bright or long chime is louder than it looks on paper.
constexpr Note kChimeNotes[] = {
    {783.99F, 200, 0.75F},
    {659.25F, 200, 0.80F},
    {523.25F, 420, 0.85F},
};

constexpr size_t kChimeNoteCount = sizeof(kChimeNotes) / sizeof(kChimeNotes[0]);

// One damped note with a 12 ms attack, an exponential decay and a short
// release so the note never ends on a step (which would click).
bool writeNote(const Note& note, float gain) {
  int16_t buffer[kChunkFrames];

  const uint32_t totalFrames =
      (config::kSampleRateHz * note.durationMs) / 1000U;
  if (totalFrames == 0) {
    return true;
  }

  const float phaseIncrement =
      kTwoPi * note.frequencyHz / static_cast<float>(config::kSampleRateHz);
  const uint32_t attackFrames = (config::kSampleRateHz * 12U) / 1000U;
  const uint32_t releaseFrames = (config::kSampleRateHz * 40U) / 1000U;

  float phase = 0.0F;
  uint32_t written = 0;
  while (written < totalFrames) {
    const uint32_t batch = std::min(kChunkFrames, totalFrames - written);
    for (uint32_t i = 0; i < batch; ++i) {
      const uint32_t frame = written + i;
      const float progress = static_cast<float>(frame) /
                             static_cast<float>(totalFrames);

      float envelope = std::exp(-3.2F * progress);
      if (frame < attackFrames) {
        envelope *= static_cast<float>(frame) / static_cast<float>(attackFrames);
      }
      if (totalFrames - frame < releaseFrames) {
        envelope *= static_cast<float>(totalFrames - frame) /
                    static_cast<float>(releaseFrames);
      }

      // Fundamental plus a quiet second harmonic: a pure sine sounds thin on
      // a 28 mm speaker.
      const float sample =
          (0.85F * std::sin(phase) + 0.15F * std::sin(2.0F * phase)) *
          envelope * note.amplitude;
      const float scaled = std::clamp(sample * 32767.0F, -32768.0F, 32767.0F);
      buffer[i] = static_cast<int16_t>(scaled);

      phase += phaseIncrement;
      if (phase >= kTwoPi) {
        phase -= kTwoPi;
      }
    }

    if (!audio_bus::writeSpeakerMono16(buffer, batch, gain)) {
      return false;
    }
    written += batch;
  }
  return true;
}

bool playChime(float gain) {
  audio_bus::setAmplifierEnabled(true);

  bool success = true;
  for (size_t i = 0; i < kChimeNoteCount; ++i) {
    if (!writeNote(kChimeNotes[i], gain)) {
      Serial.println("[CHIME] I2S write failed.");
      success = false;
      break;
    }
  }
  // A short silence tail pushes the last samples through the DMA ring before
  // the amplifier is shut down, which avoids an audible click.
  if (success && !audio_bus::writeSilence(config::kChimeTailMs)) {
    success = false;
  }

  audio_bus::setAmplifierEnabled(false);
  return success;
}

}  // namespace

namespace alert_sound {

bool wavAvailable() {
  return wav_player::alertFileExists();
}

bool play(float gain) {
  if (wavAvailable()) {
    if (wav_player::playAlert(gain)) {
      return true;
    }
    Serial.println("[ALERT] Reminder WAV is unusable; using the built-in chime.");
  } else {
    Serial.println("[ALERT] /alert.wav is missing; using the built-in chime.");
    Serial.println("[ALERT] Run 'pio run -t uploadfs' to install a custom reminder.");
  }
  return playChime(gain);
}

}  // namespace alert_sound
