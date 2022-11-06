#pragma once

#include "buffer.h"
#include "reactor.h"
#include <memory>
#include <iostream>
#include <functional>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace salt {

template<class protocol> class transport
{
    reactor_ptr m_reactor;
    typename protocol::endpoint m_bind;
    typename protocol::endpoint m_peer;
    std::shared_ptr<typename protocol::socket> m_socket;

public:

    transport(reactor_ptr reactor, const typename protocol::endpoint& bind, const typename protocol::endpoint& peer)
        : m_reactor(reactor)
        , m_bind(bind)
        , m_peer(peer)
        , m_socket(std::make_shared<typename protocol::socket>(reactor->get_io()))
    {
        m_socket->open(bind.protocol());
        m_socket->non_blocking(true);
        m_socket->bind(bind);
    }

    ~transport()
    {
        if (m_socket->is_open())
        {
            boost::system::error_code ec;
            m_socket->close(ec);
        }
    }

    void open() noexcept(false)
    {
        m_socket->open(m_bind.protocol());
        m_socket->non_blocking(true);
        m_socket->bind(m_bind);
    }

    void close() noexcept(false)
    {
        m_socket->close();
    }

    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;
    
    void async_receive(const mutable_buffer& buffer, const io_callback& callback) noexcept(true)
    {
        auto from = std::make_shared<typename protocol::endpoint>();

        io_callback checker = [sock = m_socket, buff = buffer, call = callback, peer = m_peer, from, checker](const boost::system::error_code& error, size_t bytes)
        {
            if (error || *from == peer)
            {
                call(error, bytes);
            }
            else
            {
                sock->async_receive_from(boost::asio::buffer(buff.data(), buff.size()), *from, checker);
            }
        };

        m_socket->async_receive_from(boost::asio::buffer(buffer.data(), buffer.size()), *from, checker);
    }

    void async_send(const const_buffer& buffer, const io_callback& callback) noexcept(true)
    {
        m_socket->async_send_to(boost::asio::buffer(buffer.data(), buffer.size()), m_peer, callback);
    }
};

}
