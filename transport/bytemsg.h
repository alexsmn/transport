#pragma once

#include <cstdint>
#include <stdexcept>
#include <string.h>  // memcpy

namespace transport {

class ByteMessage {
 public:
  ByteMessage() = default;

  ByteMessage(void* data, size_t capacity, size_t size = 0)
      : data(reinterpret_cast<uint8_t*>(data)),
        capacity(capacity),
        size(size),
        pos(0) {}

  void* ptr() { return data + pos; }
  const void* ptr() const { return data + pos; }
  bool end() const { return pos >= size; }
  bool empty() const { return !size; }
  size_t max_read() const { return size - pos; }
  size_t max_write() const { return capacity - pos; }

  uint8_t byte_at(size_t pos) const {
    if (pos >= size)
      throw std::runtime_error("Random access error");
    return data[pos];
  }

  void Clear() { size = 0, pos = 0; }
  void Reset() { pos = 0; }

  void Read(void* data, size_t size) {
    if (pos + size > this->size)
      throw std::runtime_error("read error");
    if (data)
      memcpy(data, this->data + pos, size);
    pos += size;
  }

  void* GetWriteBuffer(size_t size) {
    if (pos + size > capacity)
      throw std::runtime_error("Write error");

    void* buffer = data + pos;
    pos += size;

    if (this->size < pos)
      this->size = pos;

    return buffer;
  }

  void Write(const void* data, size_t size) {
    void* buffer = GetWriteBuffer(size);
    if (data)
      memcpy(buffer, data, size);
  }

  template <typename T>
  T ReadT() {
    T data;
    Read(&data, sizeof(data));
    return data;
  }
  template <typename T>
  void ReadT(T& data) {
    Read(&data, sizeof(data));
  }

  template <typename T>
  void WriteT(const T& data) {
    Write(&data, sizeof(data));
  }

  uint8_t ReadByte() { return ReadT<uint8_t>(); }
  uint16_t ReadWord() { return ReadT<uint16_t>(); }
  uint32_t ReadLong() { return ReadT<uint32_t>(); }

  void WriteByte(uint8_t v) { WriteT(v); }
  void WriteWord(uint16_t v) { WriteT(v); }
  void WriteLong(uint32_t v) { WriteT(v); }

  void Seek(size_t pos) {
    if (pos >= size) {
      throw std::runtime_error("Seek error");
    }
    this->pos = pos;
  }

  void Pop(size_t count) {
    if (count > size) {
      throw std::runtime_error{"Too much data to pop"};
    }

    std::rotate(data, data + count, data + size);
    size -= count;

    if (pos >= count) {
      pos -= count;
    } else {
      pos = 0;
    }
  }

  size_t capacity = 0;
  size_t size = 0;
  size_t pos = 0;
  uint8_t* data = nullptr;
};

}  // namespace transport
