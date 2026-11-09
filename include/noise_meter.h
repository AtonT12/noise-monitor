#pragma once

#include <cstddef>
#include <cstdint>

struct NoiseReading {
  float rmsNormalized = 0.0F;
  float dbfs = -120.0F;
  float instantSplEstimateDb = 0.0F;
  float smoothedSplEstimateDb = 0.0F;
};

class NoiseMeter {
 public:
  explicit NoiseMeter(float calibrationOffsetDb);

  // Input is interleaved stereo 32-bit I2S data. Only the microphone's LEFT
  // slot is used. Output is an unweighted SPL estimate, not dBA.
  NoiseReading process(const int32_t* interleavedWords, size_t frameCount);
  void setCalibrationOffset(float offsetDb);
  float calibrationOffset() const;
  void reset();

 private:
  float calibrationOffsetDb_;
  float previousInput_ = 0.0F;
  float previousOutput_ = 0.0F;
  float emaDb_ = 0.0F;
  bool emaPrimed_ = false;
};
