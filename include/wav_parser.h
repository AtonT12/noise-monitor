#pragma once

#include <cstddef>
#include <cstdint>

// Minimal seekable byte source used by the WAV parser. Keeping this interface
// independent of Arduino/LittleFS makes malformed-file handling host-testable.
class WavReader {
 public:
  virtual ~WavReader() = default;
  virtual uint64_t position() const = 0;
  virtual uint64_t size() const = 0;
  virtual size_t read(uint8_t* destination, size_t count) = 0;
  virtual bool seek(uint64_t absolutePosition) = 0;
};

struct WavPcmInfo {
  uint64_t dataOffset = 0;
  uint32_t dataBytes = 0;
  uint32_t sampleRate = 0;
};

// Accepts RIFF/WAVE containing uncompressed mono PCM, 16-bit little-endian.
// The caller is responsible for checking the required sample rate.
bool parsePcmWav(WavReader& reader, WavPcmInfo& info);
