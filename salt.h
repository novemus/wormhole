#pragma once

#include "buffer.h"
#include <memory>
#include <type_traits>
#include <functional>
#include <boost/shared_array.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/udp.hpp>

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

std::shared_ptr<channel> create_udp_channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer);
std::shared_ptr<channel> create_local_channel(const boost::asio::local::datagram_protocol::endpoint& bind, const boost::asio::local::datagram_protocol::endpoint& peer);

}
