#pragma once

#include <cassert>
#include <windows.h>

namespace net::detail {

class SerialPort {
 public:
  SerialPort() : file_(INVALID_HANDLE_VALUE) {}
  ~SerialPort() { close(); }

  bool open(const char* name) {
    assert(file_ == INVALID_HANDLE_VALUE);
    file_ = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return file_ != INVALID_HANDLE_VALUE;
  }

  void close() {
    if (file_ != INVALID_HANDLE_VALUE) {
      CloseHandle(file_);
      file_ = INVALID_HANDLE_VALUE;
    }
  }

  bool is_opened() const { return file_ != INVALID_HANDLE_VALUE; }

  bool SetCommTimeouts(DWORD read_interval,
                       DWORD read_total_multiplier,
                       DWORD read_total_constant,
                       DWORD write_total_multiplier,
                       DWORD write_total_constant) const {
    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = read_interval;
    timeouts.ReadTotalTimeoutMultiplier = read_total_multiplier;
    timeouts.ReadTotalTimeoutConstant = read_total_constant;
    timeouts.WriteTotalTimeoutMultiplier = write_total_multiplier;
    timeouts.WriteTotalTimeoutConstant = write_total_constant;
    return ::SetCommTimeouts(file_, &timeouts) != 0;
  }

  bool SetCommState(const DCB& dcb) {
    return ::SetCommState(file_, const_cast<DCB*>(&dcb)) != 0;
  }

  bool SetCommState(DWORD baud_rate,
                    BYTE byte_size,
                    BYTE parity,
                    BYTE stop_bits) {
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(file_, &dcb))
      return false;

    dcb.BaudRate = baud_rate;
    dcb.ByteSize = byte_size;
    dcb.Parity = parity;
    dcb.StopBits = stop_bits;
    return ::SetCommState(file_, &dcb) != 0;
  }

  int read(void* buf, size_t len) const {
    DWORD read;
    if (!ReadFile(file_, buf, static_cast<DWORD>(len), &read, NULL))
      return -1;
    return (int)read;
  }

  int Write(const void* buf, size_t len) const {
    DWORD written;
    if (!WriteFile(file_, buf, static_cast<DWORD>(len), &written, NULL))
      return -1;
    return (int)written;
  }

  HANDLE file_;
};

}  // namespace net::detail
