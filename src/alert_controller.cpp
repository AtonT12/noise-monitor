#include "alert_controller.h"

#include <algorithm>

AlertController::AlertController(float triggerThresholdDb,
                                 float resetThresholdDb,
                                 uint32_t holdMs,
                                 uint32_t cooldownMs,
                                 uint32_t quietRearmMs) {
  configure(triggerThresholdDb, resetThresholdDb, holdMs, cooldownMs,
            quietRearmMs);
}

void AlertController::configure(float triggerThresholdDb,
                                float resetThresholdDb,
                                uint32_t holdMs,
                                uint32_t cooldownMs,
                                uint32_t quietRearmMs) {
  triggerThresholdDb_ = std::clamp(triggerThresholdDb, 40.0F, 110.0F);
  resetThresholdDb_ = std::clamp(resetThresholdDb, 30.0F, 109.0F);
  if (resetThresholdDb_ >= triggerThresholdDb_) {
    resetThresholdDb_ = triggerThresholdDb_ - 1.0F;
  }
  holdMs_ = std::clamp(holdMs, 250U, 60000U);
  cooldownMs_ = std::clamp(cooldownMs, 1000U, 300000U);
  quietRearmMs_ = std::clamp(quietRearmMs, 500U, 60000U);
}

bool AlertController::setTriggerThreshold(float valueDb) {
  const float candidate = std::clamp(valueDb, 40.0F, 110.0F);
  if (candidate <= resetThresholdDb_) {
    return false;
  }
  triggerThresholdDb_ = candidate;
  return true;
}

bool AlertController::setResetThreshold(float valueDb) {
  const float candidate = std::clamp(valueDb, 30.0F, 109.0F);
  if (candidate >= triggerThresholdDb_) {
    return false;
  }
  resetThresholdDb_ = candidate;
  return true;
}

bool AlertController::update(float levelDb, uint32_t nowMs) {
  switch (state_) {
    case AlertState::MONITORING:
      if (levelDb >= triggerThresholdDb_) {
        state_ = AlertState::THRESHOLD_HOLD;
        stateSinceMs_ = nowMs;
      }
      break;

    case AlertState::THRESHOLD_HOLD:
      // Hysteresis: once triggered, brief dips are tolerated until the level
      // falls below the lower reset threshold.
      if (levelDb < resetThresholdDb_) {
        state_ = AlertState::MONITORING;
      } else if (static_cast<uint32_t>(nowMs - stateSinceMs_) >= holdMs_) {
        state_ = AlertState::PLAYING_ALERT;
        ++alertCount_;
        return true;
      }
      break;

    case AlertState::PLAYING_ALERT:
      // The main loop performs blocking playback, then calls onPlaybackCompleted().
      break;

    case AlertState::COOLDOWN:
      if (static_cast<uint32_t>(nowMs - stateSinceMs_) >= cooldownMs_) {
        state_ = AlertState::WAITING_FOR_QUIET;
        quietTiming_ = false;
      }
      break;

    case AlertState::WAITING_FOR_QUIET:
      if (levelDb > resetThresholdDb_) {
        quietTiming_ = false;
      } else if (!quietTiming_) {
        quietTiming_ = true;
        quietSinceMs_ = nowMs;
      } else if (static_cast<uint32_t>(nowMs - quietSinceMs_) >= quietRearmMs_) {
        state_ = AlertState::MONITORING;
        quietTiming_ = false;
      }
      break;
  }
  return false;
}

void AlertController::onPlaybackCompleted(uint32_t nowMs) {
  state_ = AlertState::COOLDOWN;
  stateSinceMs_ = nowMs;
  quietTiming_ = false;
}

float AlertController::triggerThreshold() const { return triggerThresholdDb_; }
float AlertController::resetThreshold() const { return resetThresholdDb_; }
uint32_t AlertController::holdDurationMs() const { return holdMs_; }
uint32_t AlertController::cooldownDurationMs() const { return cooldownMs_; }
uint32_t AlertController::quietRearmDurationMs() const { return quietRearmMs_; }
uint32_t AlertController::alertCount() const { return alertCount_; }
AlertState AlertController::state() const { return state_; }

const char* alertStateName(AlertState state) {
  switch (state) {
    case AlertState::MONITORING: return "MONITORING";
    case AlertState::THRESHOLD_HOLD: return "THRESHOLD_HOLD";
    case AlertState::PLAYING_ALERT: return "PLAYING_ALERT";
    case AlertState::COOLDOWN: return "COOLDOWN";
    case AlertState::WAITING_FOR_QUIET: return "WAITING_FOR_QUIET";
  }
  return "UNKNOWN";
}
