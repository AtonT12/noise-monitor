#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include "wav_parser.h"

class MemoryWavReader final : public WavReader {
 public:
  explicit MemoryWavReader(std::vector<uint8_t> bytes)
      : bytes_(std::move(bytes)) {}

  uint64_t position() const override { return position_; }
  uint64_t size() const override { return bytes_.size(); }

  size_t read(uint8_t* destination, size_t count) override {
    if (position_ > bytes_.size()) {
      return 0;
    }
    const size_t available = bytes_.size() - static_cast<size_t>(position_);
    const size_t actual = count < available ? count : available;
    for (size_t i = 0; i < actual; ++i) {
      destination[i] = bytes_[static_cast<size_t>(position_) + i];
    }
    position_ += actual;
    return actual;
  }

  bool seek(uint64_t absolutePosition) override {
    if (absolutePosition > bytes_.size()) {
      return false;
    }
    position_ = absolutePosition;
    return true;
  }

 private:
  std::vector<uint8_t> bytes_;
  uint64_t position_ = 0;
};

namespace {

void appendId(std::vector<uint8_t>& out, const char* id) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<uint8_t>(id[i]));
  }
}

void appendLe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

void appendLe32(std::vector<uint8_t>& out, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFU));
  }
}

void patchLe32(std::vector<uint8_t>& out, size_t offset, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out[offset + (shift / 8U)] =
        static_cast<uint8_t>((value >> shift) & 0xFFU);
  }
}

std::vector<uint8_t> makeValidWav() {
  std::vector<uint8_t> bytes;
  appendId(bytes, "RIFF");
  appendLe32(bytes, 0);  // Patched with the final RIFF size below.
  appendId(bytes, "WAVE");

  // An odd-sized, padded unknown chunk verifies safe chunk skipping.
  appendId(bytes, "JUNK");
  appendLe32(bytes, 3);
  bytes.push_back(0x11);
  bytes.push_back(0x22);
  bytes.push_back(0x33);
  bytes.push_back(0x00);  // RIFF pad byte.

  appendId(bytes, "fmt ");
  appendLe32(bytes, 16);
  appendLe16(bytes, 1);       // PCM
  appendLe16(bytes, 1);       // Mono
  appendLe32(bytes, 16000);   // Sample rate
  appendLe32(bytes, 32000);   // Byte rate
  appendLe16(bytes, 2);       // Block align
  appendLe16(bytes, 16);      // Bits per sample

  appendId(bytes, "data");
  appendLe32(bytes, 4);
  appendLe16(bytes, 0x1234);
  appendLe16(bytes, 0x5678);

  patchLe32(bytes, 4, static_cast<uint32_t>(bytes.size() - 8U));
  return bytes;
}

std::vector<uint8_t> makeMalformedHugeChunk(const char* chunkId) {
  std::vector<uint8_t> bytes;
  appendId(bytes, "RIFF");
  appendLe32(bytes, 12);  // WAVE (4) + chunk header (8), no chunk payload.
  appendId(bytes, "WAVE");
  appendId(bytes, chunkId);
  appendLe32(bytes, 0xFFFFFFF8U);
  return bytes;
}

}  // namespace

int main() {
  {
    MemoryWavReader reader(makeValidWav());
    WavPcmInfo info;
    assert(parsePcmWav(reader, info));
    assert(info.sampleRate == 16000U);
    assert(info.dataBytes == 4U);
    assert(info.dataOffset + info.dataBytes == reader.size());
  }

  {
    // This exact malformed size previously risked wrapping a 32-bit seek
    // target back to the current chunk header and looping forever.
    MemoryWavReader reader(makeMalformedHugeChunk("JUNK"));
    WavPcmInfo info;
    assert(!parsePcmWav(reader, info));
  }

  {
    // The same bounds check must reject an oversized fmt chunk as well.
    MemoryWavReader reader(makeMalformedHugeChunk("fmt "));
    WavPcmInfo info;
    assert(!parsePcmWav(reader, info));
  }

  std::cout << "All WAV parser tests passed.\n";
  return 0;
}
