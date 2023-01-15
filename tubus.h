#pragma once

#include "buffer.h"
#include <memory>
#include <functional>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/udp.hpp>

namespace novemus { namespace tubus {

struct channel
{
    typedef std::function<void(const boost::system::error_code&)> callback;
    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

    virtual ~channel() {}
    virtual void open() noexcept(false) = 0;
    virtual void close() noexcept(true) = 0;
    virtual void connect(const callback& handle) noexcept(true) = 0;
    virtual void accept(const callback& handle) noexcept(true) = 0;
    virtual void shutdown(const callback& handle) noexcept(true) = 0;
    virtual void read(const mutable_buffer& buffer, const io_callback& handle) noexcept(true) = 0;
    virtual void write(const const_buffer& buffer, const io_callback& handle) noexcept(true) = 0;
};

std::shared_ptr<channel> create_channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t secret = 0) noexcept(true);

}}
