#pragma once

#include "buffer.h"
#include "reactor.h"
#include <boost/asio/ip/udp.hpp>

namespace tubus { namespace udp {

struct receiver
{
    virtual ~receiver() {}
    virtual void receive(const boost::asio::ip::udp::endpoint& peer, const mutable_buffer& buffer) noexcept(true) = 0;
    virtual void mistake(const boost::asio::ip::udp::endpoint& peer, const boost::system::error_code& error) noexcept(true) = 0;
};

struct transport
{
    virtual ~transport() {}
    virtual void subscribe(const boost::asio::ip::udp::endpoint& peer, std::shared_ptr<receiver> receiver) noexcept(true) = 0;  
    virtual void dispatch(const boost::asio::ip::udp::endpoint& peer, const const_buffer& buffer) noexcept(true) = 0;
};

std::shared_ptr<transport> create_transport(std::shared_ptr<reactor> reactor, const boost::asio::ip::udp::endpoint& bind) noexcept(false);

}}
