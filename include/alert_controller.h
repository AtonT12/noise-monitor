#pragma once

#include <cstdint>

enum class AlertState : uint8_t {
  MONITORING,
  THRESHOLD_HOLD,
  PLAYING_ALERT,
  COOLDOWN,
  WAITING_FOR_QUIET,
};

class AlertController {
 public:
  AlertController(float triggerThresholdDb, float resetThresholdDb,
                  uint32_t holdMs, uint32_t cooldownMs, uint32_t quietRearmMs);

  void configure(float triggerThresholdDb, float resetThresholdDb,
                 uint32_t holdMs, uint32_t cooldownMs, uint32_t quietRearmMs);
  bool setTriggerThreshold(float valueDb);
  bool setResetThreshold(float valueDb);

  // Returns true once when sustained noise qualifies for a reminder.
  bool update(float levelDb, uint32_t nowMs);

  // Call after an attempted playback (successful or not) to start the lockout.
  void onPlaybackCompleted(uint32_t nowMs);

  float triggerThreshold() const;
  float resetThreshold() const;
  uint32_t holdDurationMs() const;
  uint32_t cooldownDurationMs() const;
  uint32_t quietRearmDurationMs() const;
  uint32_t alertCount() const;
  AlertState state() const;

 private:
  float triggerThresholdDb_;
  float resetThresholdDb_;
  uint32_t holdMs_;
  uint32_t cooldownMs_;
  uint32_t quietRearmMs_;

  AlertState state_ = AlertState::MONITORING;
  uint32_t stateSinceMs_ = 0;
  uint32_t quietSinceMs_ = 0;
  bool quietTiming_ = false;
  uint32_t alertCount_ = 0;
};

const char* alertStateName(AlertState state);
