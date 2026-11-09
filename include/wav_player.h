#pragma once

#include <Arduino.h>

namespace wav_player {

bool beginFilesystem();
bool alertFileExists();

// Supported WAV format: uncompressed PCM, mono, 16 kHz, 16-bit little-endian.
// The amplifier is enabled only after a valid WAV header has been parsed.
bool playAlert(float gain);

}  // namespace wav_player
