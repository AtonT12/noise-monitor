#pragma once

#include <Arduino.h>
#include <cstdint>

// PascalCase is deliberate: Arduino.h defines LOW and HIGH as macros, so an
// enumerator named LOW would be expanded to 0 inside this enum.
enum class BatteryStatus : uint8_t {
  Absent,    // No cell fitted, or the divider is not populated.
  Ok,
  Low,
  Critical,  // Below the protection threshold; the caller should stop drawing current.
};

// Reads a protected single-cell Li-Po through a 1:2 resistor divider.
// The ADC pin is sampled with the calibrated millivolt API, so the reading
// does not depend on the raw 12-bit scale or the attenuation setting.
class BatteryMonitor {
 public:
  void begin();

  // Averaged pack voltage in volts, already multiplied by the divider ratio.
  float readVolts();

  BatteryStatus status(float volts) const;

  // Rough state of charge for a typical 1S Li-Po, in percent.
  static uint8_t percent(float volts);
};

const char* batteryStatusName(BatteryStatus status);
