#include "isdm/transport/udp_transport.h"

#include <cerrno>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace isdm::transport {

UdpTransport::UdpTransport(Config config) : config_(config) {}

UdpTransport::UdpTransport() : UdpTransport(Config{}) {}

UdpTransport::~UdpTransport() { close(); }

bool UdpTransport::open() {
    if (is_open()) return true;
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;

    if (config_.reuse_address) {
        int enabled = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0) {
            close();
            return false;
        }
    }
    if (config_.receive_buffer_size > 0 &&
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &config_.receive_buffer_size,
                     sizeof(config_.receive_buffer_size)) < 0) {
        close();
        return false;
    }
    if (config_.send_buffer_size > 0 &&
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &config_.send_buffer_size,
                     sizeof(config_.send_buffer_size)) < 0) {
        close();
        return false;
    }
    if (config_.non_blocking) {
        const int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            close();
            return false;
        }
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.bind_endpoint.port());
    for (int i = 0; i < 4; ++i) {
        reinterpret_cast<std::uint8_t*>(&address.sin_addr.s_addr)[i] =
            config_.bind_endpoint.octet(static_cast<std::size_t>(i));
    }
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        close();
        return false;
    }

    if (config_.multicast_group.has_value()) {
        const auto& group = *config_.multicast_group;
        if (!group.is_multicast()) {
            close();
            return false;
        }
        ip_mreq membership{};
        auto* group_bytes = reinterpret_cast<std::uint8_t*>(&membership.imr_multiaddr.s_addr);
        for (int i = 0; i < 4; ++i) {
            group_bytes[i] = group.octet(static_cast<std::size_t>(i));
        }
        if (config_.multicast_interface.has_value()) {
            auto* interface_bytes = reinterpret_cast<std::uint8_t*>(&membership.imr_interface.s_addr);
            for (int i = 0; i < 4; ++i) {
                interface_bytes[i] = config_.multicast_interface->octet(static_cast<std::size_t>(i));
            }
        } else {
            membership.imr_interface.s_addr = htonl(INADDR_ANY);
        }
        if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership,
                         sizeof(membership)) < 0) {
            close();
            return false;
        }
        // Bind the multicast send interface so loopback-group traffic is routable.
        in_addr multicast_if{};
        if (config_.multicast_interface.has_value()) {
            auto* interface_bytes = reinterpret_cast<std::uint8_t*>(&multicast_if.s_addr);
            for (int i = 0; i < 4; ++i) {
                interface_bytes[i] =
                    config_.multicast_interface->octet(static_cast<std::size_t>(i));
            }
        } else {
            multicast_if.s_addr = htonl(INADDR_ANY);
        }
        if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &multicast_if,
                         sizeof(multicast_if)) < 0) {
            close();
            return false;
        }
        const unsigned char loopback = config_.multicast_loopback ? 1U : 0U;
        if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback,
                         sizeof(loopback)) < 0) {
            close();
            return false;
        }
    }

    sockaddr_in actual{};
    socklen_t length = sizeof(actual);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&actual), &length) < 0) {
        close();
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&actual.sin_addr.s_addr);
    local_endpoint_ = Endpoint(bytes[0], bytes[1], bytes[2], bytes[3], ntohs(actual.sin_port));
    return true;
}

void UdpTransport::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    local_endpoint_ = Endpoint{};
}

bool UdpTransport::is_open() const noexcept { return fd_ >= 0; }

std::size_t UdpTransport::send_to(DataView data, const Endpoint& destination) {
    if (!is_open() || data.size() > static_cast<std::size_t>(SSIZE_MAX)) return 0;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(destination.port());
    auto* bytes = reinterpret_cast<std::uint8_t*>(&address.sin_addr.s_addr);
    for (int i = 0; i < 4; ++i) bytes[i] = destination.octet(static_cast<std::size_t>(i));
    const auto result = ::sendto(fd_, data.data(), data.size(), 0,
                                 reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    return result < 0 ? 0 : static_cast<std::size_t>(result);
}

std::size_t UdpTransport::receive_from(MutableDataView buffer, Endpoint& source) {
    if (!is_open() || buffer.data() == nullptr || buffer.size() == 0) return 0;
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    const auto result = ::recvfrom(fd_, buffer.data(), buffer.size(), 0,
                                   reinterpret_cast<sockaddr*>(&address), &length);
    if (result < 0) return 0;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&address.sin_addr.s_addr);
    source = Endpoint(bytes[0], bytes[1], bytes[2], bytes[3], ntohs(address.sin_port));
    return static_cast<std::size_t>(result);
}

}  // namespace isdm::transport
