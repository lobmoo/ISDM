#pragma once

#include <cstddef>

#include "isdm/core/data_view.h"
#include "isdm/transport/endpoint.h"

namespace isdm::transport {

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual bool open() = 0;
    virtual void close() noexcept = 0;
    virtual bool is_open() const noexcept = 0;
    virtual std::size_t send_to(DataView data, const Endpoint& destination) = 0;
    virtual std::size_t receive_from(MutableDataView buffer, Endpoint& source) = 0;
    virtual Endpoint local_endpoint() const noexcept = 0;
};

}  // namespace isdm::transport
