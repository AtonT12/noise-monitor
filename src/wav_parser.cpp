#include "wav_parser.h"

#include <limits>

namespace {

bool readExact(WavReader& reader, uint8_t* destination, size_t count) {
  return reader.read(destination, count) == count;
}

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

bool idEquals(const uint8_t* p, const char* id) {
  return p[0] == static_cast<uint8_t>(id[0]) &&
         p[1] == static_cast<uint8_t>(id[1]) &&
         p[2] == static_cast<uint8_t>(id[2]) &&
         p[3] == static_cast<uint8_t>(id[3]);
}

// Move forward only when the requested range is contained in the source.
// Subtraction is checked before addition, preventing size_t/uint32 wraparound.
bool skipBytes(WavReader& reader, uint64_t byteCount) {
  const uint64_t current = reader.position();
  const uint64_t fileSize = reader.size();
  if (current > fileSize || byteCount > (fileSize - current)) {
    return false;
  }
  return reader.seek(current + byteCount);
}

}  // namespace

bool parsePcmWav(WavReader& reader, WavPcmInfo& info) {
  info = {};

  if (reader.position() > reader.size() ||
      reader.size() - reader.position() < 12U) {
    return false;
  }

  uint8_t riff[12];
  if (!readExact(reader, riff, sizeof(riff)) || !idEquals(riff, "RIFF") ||
      !idEquals(riff + 8, "WAVE")) {
    return false;
  }

  // RIFF size counts bytes after the first eight bytes ("RIFF" + size).
  // Promote before addition so a hostile 32-bit size cannot wrap.
  const uint64_t riffEnd = 8ULL + static_cast<uint64_t>(readLe32(riff + 4));
  if (riffEnd < 12U || riffEnd > reader.size()) {
    return false;
  }

  bool formatFound = false;
  uint32_t sampleRate = 0;

  while (true) {
    const uint64_t chunkHeaderPosition = reader.position();
    if (chunkHeaderPosition > riffEnd || riffEnd - chunkHeaderPosition < 8U) {
      break;
    }

    uint8_t chunkHeader[8];
    if (!readExact(reader, chunkHeader, sizeof(chunkHeader))) {
      return false;
    }

    const uint32_t chunkSize32 = readLe32(chunkHeader + 4);
    const uint64_t chunkSize = static_cast<uint64_t>(chunkSize32);
    const uint64_t chunkDataStart = reader.position();
    const uint64_t remainingInRiff = riffEnd - chunkDataStart;
    const uint64_t paddedChunkSize = chunkSize + (chunkSize & 1ULL);

    // Validate the full chunk (including RIFF's odd-byte padding) before
    // reading or seeking. This rejects sizes such as 0xFFFFFFF8 immediately.
    if (paddedChunkSize > remainingInRiff) {
      return false;
    }

    if (idEquals(chunkHeader, "fmt ")) {
      if (chunkSize < 16U) {
        return false;
      }

      uint8_t fmt[16];
      if (!readExact(reader, fmt, sizeof(fmt))) {
        return false;
      }

      const uint16_t audioFormat = readLe16(fmt);
      const uint16_t channels = readLe16(fmt + 2);
      sampleRate = readLe32(fmt + 4);
      const uint32_t byteRate = readLe32(fmt + 8);
      const uint16_t blockAlign = readLe16(fmt + 12);
      const uint16_t bitsPerSample = readLe16(fmt + 14);

      if (audioFormat != 1U || channels != 1U || sampleRate == 0U ||
          bitsPerSample != 16U || blockAlign != 2U ||
          static_cast<uint64_t>(byteRate) !=
              static_cast<uint64_t>(sampleRate) * blockAlign) {
        return false;
      }
      formatFound = true;

      // The first 16 bytes were consumed; skip optional fmt extension and pad.
      const uint64_t consumed = 16U;
      const uint64_t remainder = paddedChunkSize - consumed;
      if (!skipBytes(reader, remainder)) {
        return false;
      }
    } else if (idEquals(chunkHeader, "data")) {
      if (!formatFound || chunkSize == 0U || (chunkSize & 1ULL) != 0U) {
        return false;
      }
      if (chunkSize > std::numeric_limits<uint32_t>::max()) {
        return false;
      }
      info.dataOffset = chunkDataStart;
      info.dataBytes = static_cast<uint32_t>(chunkSize);
      info.sampleRate = sampleRate;
      return true;
    } else {
      if (!skipBytes(reader, paddedChunkSize)) {
        return false;
      }
    }
  }

  return false;
}
