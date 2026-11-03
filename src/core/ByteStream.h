// Byte streams for files.
//
// Serializers write straight into a file and parsers read straight from it, so no
// buffer ever holds a whole file. The interfaces also satisfy ArduinoJson's custom
// writer (write(uint8_t), write(const uint8_t*, size_t)) and reader (read(),
// readBytes(char*, size_t)), so serializeJson() and deserializeJson() take them as
// they are. Storage implements them on LittleFS files; the array versions below
// serve the buffer variants of the serializers and native tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ledframe {

class IByteWriter {
 public:
  virtual ~IByteWriter() = default;
  // Writes length bytes. Returns how many were taken; fewer means an error.
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  size_t write(uint8_t byte) { return write(&byte, 1); }
};

class IByteReader {
 public:
  virtual ~IByteReader() = default;
  // Reads up to length bytes. Returns how many were read; fewer means the end of
  // the data or an error.
  virtual size_t readBytes(char* out, size_t length) = 0;
  // Next byte, or -1 at the end.
  int read() {
    char byte = 0;
    return readBytes(&byte, 1) == 1 ? static_cast<uint8_t>(byte) : -1;
  }
};

// Writes into a fixed buffer. Bytes past the capacity are dropped and reported.
class ArrayWriter : public IByteWriter {
 public:
  ArrayWriter(char* out, size_t capacity) : out_(out), capacity_(out != nullptr ? capacity : 0) {}

  using IByteWriter::write;
  size_t write(const uint8_t* data, size_t length) override {
    const size_t room = capacity_ - length_;
    const size_t count = length < room ? length : room;
    if (count > 0) {
      std::memcpy(out_ + length_, data, count);
    }
    length_ += count;
    overflow_ |= count < length;
    return count;
  }

  size_t length() const { return length_; }
  bool overflow() const { return overflow_; }

 private:
  char* out_;
  size_t capacity_;
  size_t length_ = 0;
  bool overflow_ = false;
};

// Reads from a buffer.
class ArrayReader : public IByteReader {
 public:
  ArrayReader(const char* data, size_t length)
      : data_(data), length_(data != nullptr ? length : 0) {}

  size_t readBytes(char* out, size_t length) override {
    const size_t left = length_ - position_;
    const size_t count = length < left ? length : left;
    if (count > 0) {
      std::memcpy(out, data_ + position_, count);
    }
    position_ += count;
    return count;
  }

 private:
  const char* data_;
  size_t length_;
  size_t position_ = 0;
};

// Buffer form of a stream serializer: runs write(IByteWriter&) into out and adds the
// terminating '\0'. Returns the length, or 0 when out is too small.
template <typename Write>
size_t writeToArray(char* out, size_t capacity, Write write) {
  ArrayWriter writer(out, capacity > 0 ? capacity - 1 : 0);
  const size_t length = write(writer);
  if (out == nullptr || writer.overflow()) {
    return 0;
  }
  out[length] = '\0';
  return length;
}

}  // namespace ledframe
