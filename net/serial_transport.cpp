#include "net/serial_transport.h"

#include "net/base/net_errors.h"

using namespace std::chrono_literals;

namespace net {

SerialTransport::SerialTransport(boost::asio::io_service& io_service)
    : timer_{io_service} {
}

int SerialTransport::Read(void* data, size_t len) {
  return file_.read(data, len);
}

int SerialTransport::Write(const void* data, size_t len) {
  int res = file_.Write(data, len);
  return (res >= 0) ? res : ERR_FAILED;
}

Error SerialTransport::Open() {
  assert(!file_.is_opened());

  if (!file_.open(m_file_name.c_str()))
    return ERR_FAILED;

  if (!file_.SetCommTimeouts(MAXDWORD, 0, 0, 0, 0) ||
      !file_.SetCommState(m_dcb)) {
    file_.close();
    return ERR_FAILED;
  }
  
  // TODO: Fix ASAP.
  timer_.StartRepeating(10ms, [this] { OnTimer(); });

  connected_ = true;
  delegate_->OnTransportOpened();
  return OK;
}

void SerialTransport::Close() {
  timer_.Stop();

  file_.close();
  connected_ = false;
}

void SerialTransport::OnTimer() {
  assert(connected_);

  /*static clock_t time = clock();
  static bool done = false;
  if (!done && (clock() - time)/CLOCKS_PER_SEC >= 10) {
    done = true;
    {
      char data[1024];
      const char* str = "68 10 10 68 28 01 1E 01 03 01 01 00 01 5B 09 19 13 1B 02 09 04 16";
      int len = parse_msg(str, data, sizeof(data));
      recv_msg(data, len);
    }
    // test for float format
    {
      char data[1024];
      const char* str = "68 F5 F5 68 28 02 24 11 03 00 02 FD 03 1B 2F 9D 3F 00 8B 60 2C 0F 93 03 09 FE 03 39 B4 98 3F 00 8B 60 2C 0F 93 03 09 FF 03 48 B1 4D 44 00 C6 60 2C 0F 93 03 09 00 04 C3 F5 A2 C2 00 C6 60 2C 0F 93 03 09 08 04 00 00 A1 41 00 DE 60 2C 0F 93 03 09 18 04 00 B8 5F 43 00 EC 60 2C 0F 93 03 09 F1 03 CD CC 61 43 00 FE 60 2C 0F 93 03 09 F2 03 8F 82 64 43 00 FE 60 2C 0F 93 03 09 F3 03 29 DC 68 43 00 38 61 2C 0F 93 03 09 13 04 00 08 48 42 00 5F 61 2C 0F 93 03 09 F5 03 4E 62 80 3F 00 72 61 2C 0F 93 03 09 F7 03 0A 97 2C 44 00 AE 61 2C 0F 93 03 09 F8 03 5C 8F 96 42 00 AE 61 2C 0F 93 03 09 14 04 00 72 64 43 00 D6 61 2C 0F 93 03 09 F9 03 33 B3 61 43 00 E9 61 2C 0F 93 03 09 FA 03 7B 54 64 43 00 E9 61 2C 0F 93 03 09 FB 03 00 80 68 43 00 24 62 2C 0F 93 03 09 8C 16";
      int len = parse_msg(str, data, sizeof(data));
      recv_msg(data, len);
    }
  }*/

  // read iec101 message
  delegate_->OnTransportDataReceived();
}

std::string SerialTransport::GetName() const {
  return "Serial";
}

} // namespace net
