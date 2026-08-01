#include "transport/transport_string.h"

#include <gmock/gmock.h>

namespace transport {

TEST(TransportString, ParseSerialPortNumber) {
  EXPECT_EQ(1, TransportString::ParseSerialPortNumber("COM1"));
  EXPECT_EQ(3, TransportString::ParseSerialPortNumber("COM03"));
  EXPECT_EQ(10, TransportString::ParseSerialPortNumber("COM10"));
  EXPECT_EQ(250, TransportString::ParseSerialPortNumber("COM250"));

  EXPECT_EQ(0, TransportString::ParseSerialPortNumber("5"));
  EXPECT_EQ(0, TransportString::ParseSerialPortNumber(""));
  EXPECT_EQ(0, TransportString::ParseSerialPortNumber("C"));
  EXPECT_EQ(0, TransportString::ParseSerialPortNumber("COM"));
  EXPECT_EQ(0, TransportString::ParseSerialPortNumber("AB"));
  EXPECT_EQ(0, TransportString::ParseSerialPortNumber("ABCDEF"));
  EXPECT_EQ(0, TransportString::ParseSerialPortNumber("COM-3"));
  EXPECT_EQ(0, TransportString::ParseSerialPortNumber("COM0"));
  EXPECT_EQ(0, TransportString::ParseSerialPortNumber("COM2.9"));
}

}  // namespace transport
