#include <cassert>
#include <cmath>
#include <iostream>

#include "calibration.h"

int main() {
  using calibration::normalizeStoredOffset;

  // Fresh device: no NVS entry, the compiled default is passed in.
  assert(normalizeStoredOffset(0.0F) == 0.0F);

  // Legacy absolute reference (the old default) converts to a zero offset.
  assert(normalizeStoredOffset(120.0F) == 0.0F);

  // A legacy calibrated unit keeps its correction after the split.
  assert(normalizeStoredOffset(123.5F) == 3.5F);
  assert(normalizeStoredOffset(116.0F) == -4.0F);

  // Values already in the new encoding pass through untouched.
  assert(normalizeStoredOffset(3.5F) == 3.5F);
  assert(normalizeStoredOffset(-6.0F) == -6.0F);
  assert(normalizeStoredOffset(calibration::kMinOffsetDb) == -20.0F);
  assert(normalizeStoredOffset(calibration::kMaxOffsetDb) == 20.0F);

  // Legacy values beyond the supported offset clamp instead of wrapping.
  assert(normalizeStoredOffset(150.0F) == 20.0F);
  assert(normalizeStoredOffset(60.0F) == -20.0F);

  // Corrupted NVS content falls back to the default rather than propagating
  // a NaN into every comparison in the alert controller.
  assert(normalizeStoredOffset(std::nanf("")) == 0.0F);
  assert(normalizeStoredOffset(INFINITY) == 0.0F);

  // The conversion is idempotent: reading the same stored bytes twice yields
  // the same offset, so migration never needs a flash write.
  assert(normalizeStoredOffset(normalizeStoredOffset(123.5F)) == 3.5F);

  std::cout << "All calibration tests passed.\n";
  return 0;
}
