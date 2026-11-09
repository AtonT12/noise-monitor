#include "battery_monitor.h"

#include <cstddef>

#include "config.h"

namespace {

constexpr int kSampleCount = 16;

// Open-circuit discharge curve of a typical 1S Li-Po, piecewise linear.
constexpr float kCurveVolts[] = {3.30F, 3.50F, 3.60F, 3.70F, 3.80F,
                                 3.90F, 4.00F, 4.10F, 4.20F};
constexpr uint8_t kCurvePercent[] = {0, 5, 15, 35, 55, 70, 82, 92, 100};

constexpr size_t kCurvePoints = sizeof(kCurveVolts) / sizeof(kCurveVolts[0]);

}  // namespace

void BatteryMonitor::begin() {
  // analogReadMilliVolts() applies the factory ADC calibration and works with
  // the default 11 dB attenuation, which covers the 0..2.1 V divider output.
  analogReadResolution(12);
}

float BatteryMonitor::readVolts() {
  uint32_t totalMillivolts = 0;
  for (int sample = 0; sample < kSampleCount; ++sample) {
    totalMillivolts +=
        analogReadMilliVolts(static_cast<uint8_t>(config::kBatteryAdcPin));
  }
  const float pinVolts =
      (static_cast<float>(totalMillivolts) / kSampleCount) / 1000.0F;
  return pinVolts * config::kBatteryDividerRatio;
}

BatteryStatus BatteryMonitor::status(float volts) const {
  // Check "absent" first: an unpopulated divider reads close to 0 V, which
  // must not be mistaken for a deeply discharged cell.
  if (volts < config::kBatteryAbsentVolts) {
    return BatteryStatus::Absent;
  }
  if (volts < config::kBatteryCriticalVolts) {
    return BatteryStatus::Critical;
  }
  if (volts < config::kBatteryLowVolts) {
    return BatteryStatus::Low;
  }
  return BatteryStatus::Ok;
}

uint8_t BatteryMonitor::percent(float volts) {
  if (volts <= kCurveVolts[0]) {
    return 0;
  }
  if (volts >= kCurveVolts[kCurvePoints - 1]) {
    return 100;
  }
  for (size_t i = 1; i < kCurvePoints; ++i) {
    if (volts <= kCurveVolts[i]) {
      const float span = kCurveVolts[i] - kCurveVolts[i - 1];
      const float position = (volts - kCurveVolts[i - 1]) / span;
      const float interpolated =
          kCurvePercent[i - 1] + position * (kCurvePercent[i] - kCurvePercent[i - 1]);
      return static_cast<uint8_t>(interpolated);
    }
  }
  return 100;
}

const char* batteryStatusName(BatteryStatus status) {
  switch (status) {
    case BatteryStatus::Absent: return "ABSENT/USB";
    case BatteryStatus::Ok: return "OK";
    case BatteryStatus::Low: return "LOW";
    case BatteryStatus::Critical: return "CRITICAL";
  }
  return "UNKNOWN";
}
