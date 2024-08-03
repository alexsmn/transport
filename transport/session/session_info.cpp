#include "transport/session/session_info.h"

#include <boost/random/mersenne_twister.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace transport {

SessionID CreateSessionID() {
  boost::uuids::random_generator gen;
  boost::uuids::uuid u = gen();
  return boost::uuids::to_string(u);
}

}  // namespace transport
