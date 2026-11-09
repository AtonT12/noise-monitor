#include "calibration.h"

#include <algorithm>
#include <cmath>

namespace calibration {

float normalizeStoredOffset(float storedValue) {
  if (!std::isfinite(storedValue)) {
    return 0.0F;
  }

  if (storedValue < kMinOffsetDb || storedValue > kMaxOffsetDb) {
    storedValue -= kFullScaleSplDb;
  }

  return std::clamp(storedValue, kMinOffsetDb, kMaxOffsetDb);
}

}  // namespace calibration
