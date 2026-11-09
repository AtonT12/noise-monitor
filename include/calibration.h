#pragma once

namespace calibration {

// The INMP441 datasheet sensitivity is -26 dBFS at 94 dB SPL, so a digital
// signal at 0 dBFS corresponds to 120 dB SPL. This is a property of the
// microphone, not a user setting.
constexpr float kFullScaleSplDb = 120.0F;

// The user-adjustable part: the correction measured against a reference sound
// level meter once the unit is assembled in its enclosure.
constexpr float kDefaultOffsetDb = 0.0F;
constexpr float kMinOffsetDb = -20.0F;
constexpr float kMaxOffsetDb = 20.0F;

// Interprets a value read from NVS.
//
// Firmware before the calibration split stored the absolute 0 dBFS reference
// (60..150 dB, default 120) under the same key that now holds an offset. The
// two encodings cannot overlap, so anything outside the offset range is
// treated as the older absolute value and converted by subtracting the
// reference. The result is always inside [kMinOffsetDb, kMaxOffsetDb];
// non-finite input falls back to zero.
float normalizeStoredOffset(float storedValue);

}  // namespace calibration
