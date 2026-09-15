#pragma once

#include <cstdint>
#include <optional>

#include "isdm/transport/transport.h"

namespace isdm::transport {

class UdpTransport final : public ITransport {
public:
    struct Config {
        Endpoint bind_endpoint{0, 0, 0, 0, 0};
        std::optional<Endpoint> multicast_group;
        std::optional<Endpoint> multicast_interface;
        bool multicast_loopback = true;
        bool non_blocking = true;
        bool reuse_address = true;
        int receive_buffer_size = 4 * 1024 * 1024;
        int send_buffer_size = 4 * 1024 * 1024;
    };

    explicit UdpTransport(Config config);
    UdpTransport();
    ~UdpTransport() override;

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    bool open() override;
    void close() noexcept override;
    bool is_open() const noexcept override;
    std::size_t send_to(DataView data, const Endpoint& destination) override;
    std::size_t receive_from(MutableDataView buffer, Endpoint& source) override;
    Endpoint local_endpoint() const noexcept override { return local_endpoint_; }

private:
    Config config_;
    int fd_ = -1;
    Endpoint local_endpoint_;
};

}  // namespace isdm::transport
