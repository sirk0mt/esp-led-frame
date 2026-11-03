#include "state/Storage.h"

#include <LittleFS.h>

#include <cstdio>
#include <cstring>

#include "core/Log.h"

namespace ledframe {

namespace {

constexpr char kTag[] = "storage";
constexpr char kBasePath[] = "/littlefs";
constexpr char kPartitionLabel[] = "littlefs";  // partitions.csv; LittleFS defaults to "spiffs"
constexpr uint8_t kMaxOpenFiles = 5;
constexpr size_t kPathMax = 48;

void tempPath(const char* path, char (&out)[kPathMax]) {
  std::snprintf(out, sizeof(out), "%s.tmp", path);
}

// Collects bytes in the buffer and writes them to the file in blocks. Fails for
// good after a short write or when the file would exceed limit.
class FileWriter : public IByteWriter {
 public:
  FileWriter(File& file, uint8_t* buffer, size_t capacity, size_t limit)
      : file_(file), buffer_(buffer), capacity_(capacity), limit_(limit) {}

  using IByteWriter::write;
  size_t write(const uint8_t* data, size_t length) override {
    if (failed_ || total_ + length > limit_) {
      failed_ = true;
      return 0;
    }
    size_t done = 0;
    while (done < length) {
      if (used_ == capacity_ && !flush()) {
        return 0;
      }
      const size_t room = capacity_ - used_;
      const size_t count = length - done < room ? length - done : room;
      std::memcpy(buffer_ + used_, data + done, count);
      used_ += count;
      done += count;
    }
    total_ += length;
    return length;
  }

  bool flush() {
    if (!failed_ && used_ > 0 && file_.write(buffer_, used_) != used_) {
      failed_ = true;
    }
    used_ = 0;
    return !failed_;
  }

  size_t total() const { return total_; }

 private:
  File& file_;
  uint8_t* buffer_;
  size_t capacity_;
  size_t limit_;
  size_t used_ = 0;
  size_t total_ = 0;
  bool failed_ = false;
};

// Reads the file through the buffer, one block at a time.
class FileReader : public IByteReader {
 public:
  FileReader(File& file, uint8_t* buffer, size_t capacity)
      : file_(file), buffer_(buffer), capacity_(capacity) {}

  size_t readBytes(char* out, size_t length) override {
    size_t done = 0;
    while (done < length) {
      if (position_ == end_) {
        position_ = 0;
        end_ = file_.read(buffer_, capacity_);
        if (end_ == 0) {
          break;  // End of the file or a read error: the parser sees short data.
        }
      }
      const size_t left = end_ - position_;
      const size_t count = length - done < left ? length - done : left;
      std::memcpy(out + done, buffer_ + position_, count);
      position_ += count;
      done += count;
    }
    return done;
  }

 private:
  File& file_;
  uint8_t* buffer_;
  size_t capacity_;
  size_t position_ = 0;
  size_t end_ = 0;
};

}  // namespace

bool Storage::begin() {
  mounted_ = LittleFS.begin(true, kBasePath, kMaxOpenFiles, kPartitionLabel);
  if (!mounted_) {
    LOG_E(kTag, "cannot mount or format LittleFS");
    return false;
  }
  LOG_I(kTag, "LittleFS %u of %u bytes used", static_cast<unsigned>(LittleFS.usedBytes()),
        static_cast<unsigned>(LittleFS.totalBytes()));
  return true;
}

Storage::ReadResult Storage::read(const char* path, Parser parse, void* context) {
  if (!mounted_ || !LittleFS.exists(path)) {
    return ReadResult::kMissing;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    LOG_W(kTag, "cannot open %s", path);
    return ReadResult::kMissing;
  }
  const size_t size = file.size();
  if (size > kMaxFileSize) {
    LOG_W(kTag, "%s is too large (%u bytes)", path, static_cast<unsigned>(size));
    file.close();
    return ReadResult::kInvalid;
  }
  FileReader reader(file, buffer_, sizeof(buffer_));
  const bool ok = parse(context, reader, size);
  file.close();
  return ok ? ReadResult::kOk : ReadResult::kInvalid;
}

bool Storage::writeAtomic(const char* path, Producer producer, void* context) {
  if (!mounted_) {
    return false;
  }
  char temp[kPathMax];
  tempPath(path, temp);
  File file = LittleFS.open(temp, "w");
  if (!file) {
    LOG_E(kTag, "cannot create %s", temp);
    return false;
  }
  FileWriter writer(file, buffer_, sizeof(buffer_), kMaxFileSize);
  const bool produced = producer(context, writer);
  const bool written = writer.flush();
  file.close();
  if (!produced || !written) {
    LOG_E(kTag, "cannot write %s (%u bytes so far)", path, static_cast<unsigned>(writer.total()));
    LittleFS.remove(temp);
    return false;
  }
  if (!LittleFS.rename(temp, path)) {
    LOG_E(kTag, "cannot rename %s", temp);
    LittleFS.remove(temp);
    return false;
  }
  LOG_D(kTag, "wrote %s (%u bytes)", path, static_cast<unsigned>(writer.total()));
  return true;
}

int Storage::registerFile(const char* path, Producer producer, void* context) {
  if (fileCount_ >= kMaxFiles || path == nullptr || producer == nullptr) {
    return -1;
  }
  Deferred& slot = files_[fileCount_];
  slot.path = path;
  slot.producer = producer;
  slot.context = context;
  // Remove a temporary file left by a write interrupted by a power loss.
  char temp[kPathMax];
  tempPath(path, temp);
  if (mounted_ && LittleFS.exists(temp)) {
    LittleFS.remove(temp);
  }
  return fileCount_++;
}

void Storage::schedule(int slot, uint32_t nowMs, uint32_t delayMs) {
  if (slot < 0 || slot >= fileCount_) {
    return;
  }
  files_[slot].pending = true;
  files_[slot].dueMs = nowMs + delayMs;
}

void Storage::tick(uint32_t nowMs) {
  for (uint8_t i = 0; i < fileCount_; ++i) {
    Deferred& slot = files_[i];
    if (slot.pending && static_cast<int32_t>(nowMs - slot.dueMs) >= 0) {
      writeSlot(slot);
    }
  }
}

void Storage::flushAll() {
  for (uint8_t i = 0; i < fileCount_; ++i) {
    if (files_[i].pending) {
      writeSlot(files_[i]);
    }
  }
}

bool Storage::pending() const {
  for (uint8_t i = 0; i < fileCount_; ++i) {
    if (files_[i].pending) {
      return true;
    }
  }
  return false;
}

bool Storage::writeSlot(Deferred& slot) {
  slot.pending = false;  // A failed write is not retried until the next change.
  return writeAtomic(slot.path, slot.producer, slot.context);
}

}  // namespace ledframe
