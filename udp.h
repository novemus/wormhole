#pragma once

#include <memory>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/generic/datagram_protocol.hpp>

namespace novemus { namespace udp {

typedef boost::asio::generic::datagram_protocol::socket socket;
typedef std::shared_ptr<socket> socket_ptr;

socket_ptr connect(const boost::asio::ip::udp::endpoint& peer) noexcept(false);
socket_ptr connect(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer) noexcept(false);
socket_ptr accept(const boost::asio::ip::udp::endpoint& bind) noexcept(false);

}}
