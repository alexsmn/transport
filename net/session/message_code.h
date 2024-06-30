#pragma once

namespace net {

enum MessageCode {
	NETS_CREATE    = 1,
	NETS_OPEN      = 2,
	NETS_CLOSE     = 3,
	NETS_MESSAGE   = 4,
	NETS_ACK       = 5,
	NETS_TEST      = 6,
	NETS_SEQUENCE  = 7,

  NETS_RESPONSE  = 0x80,
};

} // namespace net
