// LittleFS storage with atomic and deferred writes (ESP32 only).
//
// Not thread-safe: use it from one task (the Arduino loop task in App).
//
// Files are streamed: a producer serializes straight into the file and a
// parser reads straight from it, through one small buffer, so no file is ever held
// in RAM as a whole.
//
// Deferred writes: a file is registered once with a producer that serializes the
// current data. schedule() arms the write; calling it again before the write pushes
// the deadline (debounce), so "5 s after the last change" is schedule(slot, now,
// 5000) on every change. tick() writes due files and flushAll() writes every
// pending file at once (before restart or OTA).
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ByteStream.h"

namespace ledframe {

class Storage {
 public:
  // Writes the file content to out. Returns false when it could not produce it.
  using Producer = bool (*)(void* context, IByteWriter& out);
  // Reads a file of `length` bytes from in. Returns false for broken content.
  using Parser = bool (*)(void* context, IByteReader& in, size_t length);

  enum class ReadResult : uint8_t { kOk, kMissing, kInvalid };

  static constexpr uint8_t kMaxFiles = 4;
  // Largest file read or written. 32 presets with 16 long parameters each take
  // about 18 KB of JSON; a larger file is refused when it is written, so the
  // device can always read back what it wrote.
  static constexpr size_t kMaxFileSize = 32768;
  // Block size between the streams and LittleFS.
  static constexpr size_t kBufferSize = 256;

  // Mounts the "littlefs" partition and formats it when mounting fails (first
  // start). Removes temporary files left by an interrupted write.
  bool begin();
  bool mounted() const { return mounted_; }

  // Passes the content of path to parse. kMissing when the file does not exist or
  // cannot be opened, kInvalid when it is too large or parse returns false.
  ReadResult read(const char* path, Parser parse, void* context);

  // Writes path + ".tmp" with the producer, then renames it over path, so a power
  // loss leaves either the old or the new file.
  bool writeAtomic(const char* path, Producer producer, void* context);

  // Returns a slot for schedule(), or -1 when all slots are taken.
  int registerFile(const char* path, Producer producer, void* context);
  void schedule(int slot, uint32_t nowMs, uint32_t delayMs);
  void tick(uint32_t nowMs);
  void flushAll();
  bool pending() const;
  // True when tick(nowMs) would write a file.
  bool due(uint32_t nowMs) const;

 private:
  struct Deferred {
    const char* path = nullptr;
    Producer producer = nullptr;
    void* context = nullptr;
    bool pending = false;
    uint32_t dueMs = 0;
  };

  bool writeSlot(Deferred& slot);

  bool mounted_ = false;
  Deferred files_[kMaxFiles];
  uint8_t fileCount_ = 0;
  uint8_t buffer_[kBufferSize] = {};
};

}  // namespace ledframe
