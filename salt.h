#pragma once

#include "buffer.h"
#include <memory>
#include <functional>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/local/datagram_protocol.hpp>

namespace salt {

struct channel
{
    typedef std::function<void(const boost::system::error_code&)> callback;
    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

    virtual ~channel() {}
    virtual void connect(const callback& handle) noexcept(true) = 0;
    virtual void accept(const callback& handle) noexcept(true) = 0;
    virtual void shutdown(const callback& handle) noexcept(true) = 0;
    virtual void read(const mutable_buffer& buf, const io_callback& handle) noexcept(true) = 0;
    virtual void write(const const_buffer& buf, const io_callback& handle) noexcept(true) = 0;
};
typedef std::shared_ptr<channel> channel_ptr;
typedef boost::asio::ip::udp::endpoint udp_endpoint;
typedef boost::asio::local::datagram_protocol::endpoint local_endpoint;

channel_ptr create_udp_channel(reactor_ptr reactor, const udp_endpoint& bind, const udp_endpoint& peer);
channel_ptr create_local_channel(reactor_ptr reactor, const local_endpoint& bind, const local_endpoint& peer);

}
