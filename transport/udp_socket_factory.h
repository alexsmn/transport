#pragma once

#include <functional>
#include <memory>

namespace transport {

class UdpSocket;
struct UdpSocketContext;

using UdpSocketFactory =
    std::function<std::shared_ptr<UdpSocket>(UdpSocketContext&& context)>;

}  // namespace transport
