#pragma once

namespace alert_sound {

// Plays the reminder: the LittleFS WAV when it is present and valid, otherwise
// a built-in three-note chime. The amplifier is enabled only for the duration
// of playback and is always shut down again, including on a write failure.
//
// Returns true when some audio was produced. A false return means the I2S
// output failed, not that the WAV was missing.
bool play(float gain);

// True when /alert.wav exists on the mounted filesystem. Reported by STATUS so
// a missing uploadfs step is visible without listening to the device.
bool wavAvailable();

}  // namespace alert_sound
