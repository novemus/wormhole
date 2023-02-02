#pragma once

#include "buffer.h"
#include "reactor.h"
#include <memory>
#include <functional>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/udp.hpp>

namespace novemus { namespace tubus {

typedef boost::asio::ip::udp::endpoint endpoint;
typedef std::function<void(const boost::system::error_code&)> callback;
typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

struct channel
{
    virtual ~channel() {}
    virtual void open() noexcept(false) = 0;
    virtual void close() noexcept(true) = 0;
    virtual void connect(const callback& handle) noexcept(true) = 0;
    virtual void accept(const callback& handle) noexcept(true) = 0;
    virtual void shutdown(const callback& handle) noexcept(true) = 0;
    virtual void read(const mutable_buffer& buffer, const io_callback& handle) noexcept(true) = 0;
    virtual void write(const const_buffer& buffer, const io_callback& handle) noexcept(true) = 0;
};

typedef std::shared_ptr<channel> channel_ptr;

channel_ptr create_channel(reactor_ptr reactor, const endpoint& bind, const endpoint& peer, uint64_t secret = 0) noexcept(true);

}}
