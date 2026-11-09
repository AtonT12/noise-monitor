#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "alert_controller.h"
#include "alert_sound.h"
#include "audio_bus.h"
#include "battery_monitor.h"
#include "calibration.h"
#include "config.h"
#include "noise_meter.h"
#include "wav_player.h"

namespace {

int32_t gMicrophoneWords[config::kI2sWordsPerBlock];
NoiseMeter gNoiseMeter(calibration::kDefaultOffsetDb);
AlertController gAlertController(
    config::kDefaultTriggerThresholdDb,
    config::kDefaultResetThresholdDb,
    config::kThresholdHoldMs,
    config::kCooldownMs,
    config::kQuietRearmMs);
BatteryMonitor gBatteryMonitor;

float gPlaybackGain = config::kDefaultPlaybackGain;
bool gAudioReady = false;
bool gFatalError = false;
uint32_t gLastTelemetryMs = 0;
uint32_t gLastReadWarningMs = 0;
uint32_t gLastBatteryCheckMs = 0;
float gBatteryVolts = 0.0F;
BatteryStatus gBatteryStatus = BatteryStatus::Absent;
bool gLowBatteryReported = false;
char gCommandBuffer[96];
size_t gCommandLength = 0;

void setUserLed(bool on) {
  // The XIAO ESP32-S3 onboard USER_LED is active LOW.
  digitalWrite(static_cast<uint8_t>(config::kUserLedPin), on ? LOW : HIGH);
}

void updateStatusLed(AlertState state, uint32_t nowMs) {
  // A low battery overrides the alert pattern: two short blinks every 3 s.
  if (gBatteryStatus == BatteryStatus::Low) {
    const uint32_t phase = nowMs % 3000U;
    setUserLed(phase < 60U || (phase >= 200U && phase < 260U));
    return;
  }

  switch (state) {
    case AlertState::MONITORING:
      setUserLed((nowMs % 2000U) < 70U);  // Slow heartbeat
      break;
    case AlertState::THRESHOLD_HOLD:
      setUserLed((nowMs % 160U) < 80U);   // Rapid flash
      break;
    case AlertState::PLAYING_ALERT:
      setUserLed(true);
      break;
    case AlertState::COOLDOWN:
      setUserLed((nowMs % 1200U) < 100U); // Single pulse
      break;
    case AlertState::WAITING_FOR_QUIET:
      setUserLed((nowMs % 1200U) < 70U ||
                 ((nowMs % 1200U) >= 180U && (nowMs % 1200U) < 250U));
      break;
  }
}

bool validFloat(const char* text, float& value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  char* end = nullptr;
  const float parsed = std::strtof(text, &end);
  if (end == text || !std::isfinite(parsed)) {
    return false;
  }
  while (*end == ' ' || *end == '\t') {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

bool saveFloatSetting(const char* key, float value) {
  Preferences preferences;
  if (!preferences.begin(config::kNvsNamespace, false)) {
    Serial.println("[NVS] Could not open settings namespace for writing.");
    return false;
  }
  const size_t bytesWritten = preferences.putFloat(key, value);
  preferences.end();
  if (bytesWritten != sizeof(float)) {
    Serial.printf("[NVS] Failed to save key '%s'.\n", key);
    return false;
  }
  return true;
}

void loadSettings() {
  Preferences preferences;
  if (!preferences.begin(config::kNvsNamespace, true)) {
    Serial.println("[NVS] Settings unavailable; using compiled defaults.");
    return;
  }

  float trigger = preferences.getFloat("trigger", config::kDefaultTriggerThresholdDb);
  float reset = preferences.getFloat("reset", config::kDefaultResetThresholdDb);
  float calibrationOffset =
      preferences.getFloat("cal", calibration::kDefaultOffsetDb);
  float gain = preferences.getFloat("gain", config::kDefaultPlaybackGain);
  preferences.end();

  // Firmware before the calibration split stored the absolute 0 dBFS
  // reference (default 120 dB) under the same key. The two encodings cannot
  // overlap: 60..150 dB versus -20..+20 dB. Anything outside the offset range
  // is therefore the older absolute value, so subtract the reference. The
  // stored bytes are left untouched, which keeps this conversion idempotent
  // until the user saves a new offset.
  if (calibrationOffset < calibration::kMinOffsetDb ||
      calibrationOffset > calibration::kMaxOffsetDb) {
    calibrationOffset -= calibration::kFullScaleSplDb;
    Serial.printf("[NVS] Converted legacy calibration value to %+.1f dB offset.\n",
                  calibrationOffset);
  }

  if (!std::isfinite(trigger) || trigger < 40.0F || trigger > 110.0F) {
    trigger = config::kDefaultTriggerThresholdDb;
  }
  if (!std::isfinite(reset) || reset < 30.0F || reset >= trigger) {
    reset = std::min(config::kDefaultResetThresholdDb, trigger - 1.0F);
  }
  if (!std::isfinite(calibrationOffset)) {
    calibrationOffset = calibration::kDefaultOffsetDb;
  }
  calibrationOffset = std::clamp(
      calibrationOffset, calibration::kMinOffsetDb, calibration::kMaxOffsetDb);
  if (!std::isfinite(gain) || gain < config::kMinPlaybackGain ||
      gain > config::kMaxPlaybackGain) {
    gain = config::kDefaultPlaybackGain;
  }

  gAlertController.configure(trigger, reset, config::kThresholdHoldMs,
                             config::kCooldownMs, config::kQuietRearmMs);
  gNoiseMeter.setCalibrationOffset(calibrationOffset);
  gPlaybackGain = gain;

  Serial.printf("[NVS] Loaded: trigger=%.1f, reset=%.1f, calibration=%+.1f, gain=%.2f\n",
                gAlertController.triggerThreshold(),
                gAlertController.resetThreshold(),
                gNoiseMeter.calibrationOffset(), gPlaybackGain);
}

void printStatus() {
  Serial.printf(
      "[STATUS] state=%s | trigger=%.1f dB | reset=%.1f dB | cal_offset=%+.1f dB | gain=%.2f | alerts=%lu\n",
      alertStateName(gAlertController.state()),
      gAlertController.triggerThreshold(),
      gAlertController.resetThreshold(),
      gNoiseMeter.calibrationOffset(), gPlaybackGain,
      static_cast<unsigned long>(gAlertController.alertCount()));

  const char* const reminder =
      alert_sound::wavAvailable() ? "/alert.wav" : "built-in chime";
  if (gBatteryStatus == BatteryStatus::Absent) {
    Serial.printf("[STATUS] battery=absent (USB power) | reminder=%s\n", reminder);
  } else {
    Serial.printf("[STATUS] battery=%.2f V (%u%%, %s) | reminder=%s\n",
                  gBatteryVolts,
                  static_cast<unsigned>(BatteryMonitor::percent(gBatteryVolts)),
                  batteryStatusName(gBatteryStatus), reminder);
  }
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  HELP                 Show this help");
  Serial.println("  STATUS               Show current settings and monitor state");
  Serial.println("  SET_THRESH <dB>      Set trigger threshold (40..110)");
  Serial.println("  SET_RESET <dB>       Set quiet/reset threshold (30..109, below trigger)");
  Serial.println("  SET_CAL <dB>         Set SPL calibration offset (-20..20)");
  Serial.println("  SET_VOL <0.05..0.65> Set software playback gain");
  Serial.println("  TEST_ALERT           Play the current alert WAV once");
  Serial.println("Note: displayed SPL values are unweighted estimates, not dBA.");
}

void playManualAlert() {
  Serial.println("[TEST] Manual alert playback requested.");
  const bool played = alert_sound::play(gPlaybackGain);
  Serial.println(played ? "[TEST] Playback finished." : "[TEST] Playback failed.");
  audio_bus::discardMicrophoneFor(config::kPostPlaybackDiscardMs);
  gNoiseMeter.reset();
  gAlertController.onPlaybackCompleted(millis());
}

void dispatchCommand(char* rawLine) {
  char* line = rawLine;
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  char* end = line + std::strlen(line);
  while (end > line && (end[-1] == ' ' || end[-1] == '\t')) {
    *--end = '\0';
  }
  if (*line == '\0') {
    return;
  }

  char* argument = std::strchr(line, ' ');
  if (argument != nullptr) {
    *argument++ = '\0';
    while (*argument == ' ' || *argument == '\t') {
      ++argument;
    }
  }
  for (char* p = line; *p != '\0'; ++p) {
    if (*p >= 'a' && *p <= 'z') {
      *p = static_cast<char>(*p - 'a' + 'A');
    }
  }

  if (std::strcmp(line, "HELP") == 0) {
    printHelp();
    return;
  }
  if (std::strcmp(line, "STATUS") == 0) {
    printStatus();
    return;
  }
  if (std::strcmp(line, "TEST_ALERT") == 0) {
    playManualAlert();
    return;
  }

  float value = 0.0F;
  if (argument == nullptr || !validFloat(argument, value)) {
    Serial.println("[CLI] Invalid or missing numeric argument. Type HELP.");
    return;
  }

  if (std::strcmp(line, "SET_THRESH") == 0) {
    if (value < 40.0F || value > 110.0F ||
        !gAlertController.setTriggerThreshold(value)) {
      Serial.println("[CLI] Trigger must be 40..110 dB and above reset threshold.");
      return;
    }
    const bool saved = saveFloatSetting("trigger", gAlertController.triggerThreshold());
    Serial.printf("[CLI] Trigger threshold %.1f dB (%s).\n",
                  gAlertController.triggerThreshold(), saved ? "saved" : "not saved");
  } else if (std::strcmp(line, "SET_RESET") == 0) {
    if (value < 30.0F || value > 109.0F ||
        !gAlertController.setResetThreshold(value)) {
      Serial.println("[CLI] Reset threshold must be 30..109 dB and below trigger.");
      return;
    }
    const bool saved = saveFloatSetting("reset", gAlertController.resetThreshold());
    Serial.printf("[CLI] Reset threshold %.1f dB (%s).\n",
                  gAlertController.resetThreshold(), saved ? "saved" : "not saved");
  } else if (std::strcmp(line, "SET_CAL") == 0) {
    if (value < calibration::kMinOffsetDb ||
        value > calibration::kMaxOffsetDb) {
      Serial.println("[CLI] Calibration offset must be in the range -20..20 dB.");
      return;
    }
    gNoiseMeter.setCalibrationOffset(value);
    const bool saved = saveFloatSetting("cal", gNoiseMeter.calibrationOffset());
    Serial.printf("[CLI] Calibration offset %+.1f dB (%s).\n",
                  gNoiseMeter.calibrationOffset(), saved ? "saved" : "not saved");
  } else if (std::strcmp(line, "SET_VOL") == 0) {
    if (value < config::kMinPlaybackGain || value > config::kMaxPlaybackGain) {
      Serial.println("[CLI] Playback gain must be in the range 0.05..0.65.");
      return;
    }
    gPlaybackGain = value;
    const bool saved = saveFloatSetting("gain", gPlaybackGain);
    Serial.printf("[CLI] Playback gain %.2f (%s).\n", gPlaybackGain,
                  saved ? "saved" : "not saved");
  } else {
    Serial.printf("[CLI] Unknown command '%s'. Type HELP.\n", line);
  }
}

void handleSerialInput() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      gCommandBuffer[gCommandLength] = '\0';
      dispatchCommand(gCommandBuffer);
      gCommandLength = 0;
    } else if (gCommandLength < sizeof(gCommandBuffer) - 1U) {
      gCommandBuffer[gCommandLength++] = c;
    } else {
      gCommandLength = 0;
      Serial.println("[CLI] Command too long; input discarded.");
    }
  }
}

void printTelemetry(const NoiseReading& reading, uint32_t nowMs) {
  if (static_cast<uint32_t>(nowMs - gLastTelemetryMs) < 500U) {
    return;
  }
  gLastTelemetryMs = nowMs;
  Serial.printf("[MONITOR] dBFS=%6.1f | SPL_est=%5.1f dB (unweighted) | state=%s | alerts=%lu\n",
                reading.dbfs, reading.smoothedSplEstimateDb,
                alertStateName(gAlertController.state()),
                static_cast<unsigned long>(gAlertController.alertCount()));
}

[[noreturn]] void enterProtectiveSleep(float volts) {
  Serial.printf("[BAT] %.2f V is below the %.2f V cutoff. Sleeping for %lu minutes.\n",
                volts, config::kBatteryCriticalVolts,
                static_cast<unsigned long>(config::kBatterySleepRecheckMinutes));
  Serial.flush();

  audio_bus::setAmplifierEnabled(false);
  setUserLed(false);
  esp_sleep_enable_timer_wakeup(
      static_cast<uint64_t>(config::kBatterySleepRecheckMinutes) * 60ULL * 1000000ULL);
  esp_deep_sleep_start();

  // The chip restarts from setup() when the timer fires, so this is unreachable.
  for (;;) {
    delay(1000);
  }
}

void checkBattery(bool atBoot) {
  gBatteryVolts = gBatteryMonitor.readVolts();
  gBatteryStatus = gBatteryMonitor.status(gBatteryVolts);

  if (gBatteryStatus == BatteryStatus::Critical) {
    enterProtectiveSleep(gBatteryVolts);
  }

  if (atBoot) {
    if (gBatteryStatus == BatteryStatus::Absent) {
      Serial.println("[BAT] No battery detected; running from USB.");
    } else {
      Serial.printf("[BAT] %.2f V (%u%%, %s)\n", gBatteryVolts,
                    static_cast<unsigned>(BatteryMonitor::percent(gBatteryVolts)),
                    batteryStatusName(gBatteryStatus));
    }
  } else if (gBatteryStatus == BatteryStatus::Low && !gLowBatteryReported) {
    Serial.printf("[BAT] Low battery %.2f V (%u%%). Recharge before the next session.\n",
                  gBatteryVolts,
                  static_cast<unsigned>(BatteryMonitor::percent(gBatteryVolts)));
  }

  gLowBatteryReported = (gBatteryStatus == BatteryStatus::Low);
}

void serviceAlert() {
  Serial.printf("[ALERT #%lu] Sustained noise level exceeded %.1f dB.\n",
                static_cast<unsigned long>(gAlertController.alertCount()),
                gAlertController.triggerThreshold());

  const bool played = alert_sound::play(gPlaybackGain);
  Serial.println(played ? "[ALERT] Reminder playback completed." :
                          "[ALERT] Reminder playback was not completed.");

  // Ignore mic samples captured during playback, then clear the DSP filter
  // history before returning to normal monitoring.
  audio_bus::discardMicrophoneFor(config::kPostPlaybackDiscardMs);
  gNoiseMeter.reset();
  gAlertController.onPlaybackCompleted(millis());
}

}  // namespace

void setup() {
  setCpuFrequencyMhz(config::kCpuFrequencyMhz);

  pinMode(static_cast<uint8_t>(config::kUserLedPin), OUTPUT);
  setUserLed(false);
  pinMode(static_cast<uint8_t>(config::kAmplifierSdPin), OUTPUT);
  digitalWrite(static_cast<uint8_t>(config::kAmplifierSdPin), LOW);

  Serial.begin(config::kSerialBaudRate);
  const uint32_t serialWaitStart = millis();
  while (!Serial && static_cast<uint32_t>(millis() - serialWaitStart) < 1500U) {
    delay(10);
  }

  Serial.println();
  Serial.println("========================================================");
  Serial.println(" Noise Monitor MVP - Version C");
  Serial.printf(" ESP32-S3 @ %lu MHz | Full-duplex I2S | Local-only\n",
                static_cast<unsigned long>(getCpuFrequencyMhz()));
  Serial.println(" Measurement: calibrated unweighted SPL estimate; not dBA.");
  Serial.println(" Type HELP in Serial Monitor for configuration commands.");
  Serial.println("========================================================");

  loadSettings();

  // Check the pack before bringing up audio: a critical cell must not be
  // asked to drive the amplifier even once.
  gBatteryMonitor.begin();
  checkBattery(true);
  gLastBatteryCheckMs = millis();

  gAudioReady = audio_bus::begin();
  if (!gAudioReady) {
    Serial.println("[FATAL] I2S initialization failed. Check pin wiring and board target.");
    gFatalError = true;
    return;
  }

  if (!wav_player::beginFilesystem()) {
    Serial.println("[WARN] LittleFS is unavailable; the built-in chime will be used.");
  } else if (!wav_player::alertFileExists()) {
    Serial.println("[WARN] /alert.wav is missing; the built-in chime will be used.");
    Serial.println("[WARN] Run 'pio run -t uploadfs' to install a custom reminder.");
  }

  Serial.println("[READY] Noise monitoring started.");
  printHelp();
}

void loop() {
  handleSerialInput();

  const uint32_t batteryNowMs = millis();
  if (static_cast<uint32_t>(batteryNowMs - gLastBatteryCheckMs) >=
      config::kBatteryCheckIntervalMs) {
    gLastBatteryCheckMs = batteryNowMs;
    checkBattery(false);
  }

  if (gFatalError || !gAudioReady) {
    const uint32_t nowMs = millis();
    setUserLed((nowMs % 250U) < 125U);
    delay(10);
    return;
  }

  const size_t framesRead = audio_bus::readMicrophoneFrames(
      gMicrophoneWords, config::kAudioBlockFrames, 100);
  if (framesRead == 0) {
    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - gLastReadWarningMs) > 2000U) {
      Serial.println("[MIC] No I2S microphone frames received. Check power, clocks, and SD wiring.");
      gLastReadWarningMs = nowMs;
    }
    delay(2);
    return;
  }

  const NoiseReading reading = gNoiseMeter.process(gMicrophoneWords, framesRead);
  const uint32_t nowMs = millis();
  const bool triggerAlert = gAlertController.update(
      reading.smoothedSplEstimateDb, nowMs);

  updateStatusLed(gAlertController.state(), nowMs);
  printTelemetry(reading, nowMs);

  if (triggerAlert) {
    serviceAlert();
  }
}
