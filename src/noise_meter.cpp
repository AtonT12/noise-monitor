#include "noise_meter.h"

#include <algorithm>
#include <cmath>

#include "calibration.h"
#include "config.h"

NoiseMeter::NoiseMeter(float calibrationOffsetDb)
    : calibrationOffsetDb_(calibrationOffsetDb) {}

NoiseReading NoiseMeter::process(const int32_t* interleavedWords,
                                 size_t frameCount) {
  NoiseReading reading;
  reading.dbfs = config::kDbfsFloor;

  if (interleavedWords == nullptr || frameCount == 0) {
    reading.instantSplEstimateDb =
        config::kDbfsFloor + calibration::kFullScaleSplDb + calibrationOffsetDb_;
    reading.smoothedSplEstimateDb = emaPrimed_
        ? emaDb_ : reading.instantSplEstimateDb;
    return reading;
  }

  double sumSquares = 0.0;
  for (size_t frame = 0; frame < frameCount; ++frame) {
    // INMP441 is strapped to LEFT: [left, right, left, right, ...].
    const int32_t sample24 = interleavedWords[frame * 2] >> config::kMicRightShift;
    const float input = static_cast<float>(sample24) / config::kMicFullScale;

    // First-order DC blocker: y[n] = x[n] - x[n-1] + a * y[n-1].
    const float output = input - previousInput_ +
                         config::kDcBlockAlpha * previousOutput_;
    previousInput_ = input;
    previousOutput_ = output;
    sumSquares += static_cast<double>(output) * static_cast<double>(output);
  }

  const double rms = std::sqrt(sumSquares / static_cast<double>(frameCount));
  reading.rmsNormalized = static_cast<float>(rms);
  reading.dbfs = static_cast<float>(20.0 * std::log10(std::max(rms, 1.0e-6)));
  reading.dbfs = std::max(reading.dbfs, config::kDbfsFloor);
  reading.instantSplEstimateDb =
      reading.dbfs + calibration::kFullScaleSplDb + calibrationOffsetDb_;

  if (!emaPrimed_) {
    emaDb_ = reading.instantSplEstimateDb;
    emaPrimed_ = true;
  } else {
    emaDb_ = config::kEmaAlpha * reading.instantSplEstimateDb +
             (1.0F - config::kEmaAlpha) * emaDb_;
  }
  reading.smoothedSplEstimateDb = emaDb_;
  return reading;
}

void NoiseMeter::setCalibrationOffset(float offsetDb) {
  calibrationOffsetDb_ = std::clamp(offsetDb, calibration::kMinOffsetDb,
                                    calibration::kMaxOffsetDb);
  reset();
}

float NoiseMeter::calibrationOffset() const {
  return calibrationOffsetDb_;
}

void NoiseMeter::reset() {
  previousInput_ = 0.0F;
  previousOutput_ = 0.0F;
  emaDb_ = 0.0F;
  emaPrimed_ = false;
}
