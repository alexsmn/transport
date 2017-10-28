#include "net/session_info.h"

#include "base/guid.h"

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace net {

SessionID CreateSessionID() {
  boost::uuids::basic_random_generator<boost::mt19937> gen;
  boost::uuids::uuid u = gen();
  return boost::uuids::to_string(u);
}

} // namespace net
