/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <tubus/channel.h>
#include <wormhole/tunnel.h>
#include <wormhole/logger.h>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <mutex>
#include <queue>

namespace wormhole {

template<class channel, bool accept> 
class tubus_tunnel : public tunnel, public std::enable_shared_from_this<tubus_tunnel<channel, accept>>
{
    typedef std::shared_ptr<channel> channel_ptr;
    typedef typename channel::endpoint endpoint_type;
    typedef std::queue<std::pair<packet_ptr, callback>> defer_queue;

    boost::asio::io_context& m_io;
    boost::asio::deadline_timer m_timer;
    channel_ptr m_channel;
    endpoint_type m_gateway;
    endpoint_type m_faraway;
    uint64_t m_secret;
    defer_queue m_wq;
    defer_queue m_rq;
    std::mutex m_mutex;

    void write(packet_ptr packet, const callback& handler) noexcept(true)
    {
        m_channel->write(*packet, [this, weak = this->weak_from_this(), packet, handler](const boost::system::error_code& ec, size_t size)
        {
            if (auto self = weak.lock())
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (!m_wq.empty())
                {
                    auto next = m_wq.front();
                    if (m_channel->writable() > next.first->size())
                    {
                        m_wq.pop();
                        write(next.first, std::move(next.second));
                    }
                }
            }

            if (ec)
            {
                handler(ec);
            }
            else if (size < packet->size())
            {
                handler(boost::asio::error::message_size);
            }
            else
            {
                handler(ec);
            }
        });
    }

    void read(packet_ptr packet, const callback& handler) noexcept(true)
    {
        auto callback = [this, weak = this->weak_from_this(), handler](const boost::system::error_code& ec)
        {
            if (auto self = weak.lock())
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (!m_rq.empty())
                    m_rq.pop();

                if (!m_rq.empty())
                {
                    auto next = m_rq.front();
                    read(next.first, std::move(next.second));
                }
            }

            handler(ec);
        };

        m_channel->read(packet->header(), [this, weak = this->weak_from_this(), packet, callback](const boost::system::error_code& ec, size_t size) mutable
        {
            if (ec)
            {
                callback(ec);
            }
            else if (size < packet::header_size || packet->capacity() > packet::max_payload_size)
            {
                callback(boost::asio::error::message_size);
            }
            else if (auto self = weak.lock())
            {
                packet->shrink(packet->capacity());

                if (packet->capacity() == 0)
                {
                    callback(ec);
                    return;
                }

                m_channel->read(packet->payload(), [packet, callback](const boost::system::error_code& ec, size_t size)
                {
                    if (ec)
                        callback(ec);
                    else if (size != packet->capacity())
                        callback(boost::asio::error::message_size);
                    else
                        callback(ec);
                });
            }
            else
                callback(boost::asio::error::shut_down);
        });
    }

    void connect(const callback& handler, const boost::posix_time::ptime& deadline) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        try
        {
            m_channel = channel::create(m_io, m_secret);
            m_channel->open(m_gateway);
        }
        catch(const boost::system::system_error& ex)
        {
            boost::asio::post(m_io, std::bind(handler, ex.code()));
            return;
        }

        auto callback = [this, weak = this->weak_from_this(), handler, deadline](const boost::system::error_code& ec)
        {
            if (ec)
            {
                if (auto self = weak.lock())
                {
                    if (ec.value() == boost::asio::error::connection_refused)
                    {
                        m_timer.expires_from_now(boost::posix_time::seconds(2));
                        m_timer.async_wait([this, weak, handler, deadline](const boost::system::error_code& ec) mutable
                        {
                            if (ec)
                                handler(ec);
                            else if (boost::posix_time::second_clock::universal_time() > deadline)
                                handler(boost::asio::error::timed_out);
                            else if (auto self = weak.lock())
                                connect(std::move(handler), deadline);
                            else
                                handler(boost::asio::error::operation_aborted);
                        });
                        return;
                    }
                }
            }
  
            handler(ec);
        };

        if (accept)
            m_channel->accept(m_faraway, callback);
        else
            m_channel->connect(m_faraway, callback);
    }

public:

    tubus_tunnel(boost::asio::io_context& io, const endpoint_type& gateway, const endpoint_type& faraway, uint64_t secret)
        : m_io(io)
        , m_timer(io)
        , m_gateway(gateway)
        , m_faraway(faraway)
        , m_secret(secret)
    {
    }

    void open(const callback& handler) noexcept(true) override
    {
        connect(std::move(handler), boost::posix_time::microsec_clock::universal_time() + boost::posix_time::seconds(30));
    }

    void close(const callback& handler) noexcept(true) override
    {
        if (m_channel)
            m_channel->shutdown(handler);
        else
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::shut_down));
    }

    void send(packet_ptr packet, const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_channel)
        {
            if (m_channel->writable() < packet->size() || m_wq.size())
                m_wq.emplace(packet, handler);
            else
                write(packet, std::move(handler));
        }
        else
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::shut_down));
    }

    void receive(packet_ptr packet, const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_channel)
        {
            m_rq.emplace(packet, handler);
            if (m_rq.size() == 1)
                read(packet, std::move(handler));
        }
        else
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::shut_down));
    }
};

std::shared_ptr<tunnel> create_udp_tunnel(
    boost::asio::io_context& io, const udp_endpoint& gateway, const udp_endpoint& faraway, bool accept, uint64_t secret) noexcept(true)
{
    _dbg_ << "create udp tunnel: gateway=" << gateway << " faraway=" << faraway << " accept=" << accept << " obsure=" << static_cast<bool>(secret);

    return accept
        ? std::shared_ptr<tunnel>(new tubus_tunnel<tubus::udp_channel, true>(io, gateway, faraway, secret))
        : std::shared_ptr<tunnel>(new tubus_tunnel<tubus::udp_channel, false>(io, gateway, faraway, secret));
}

std::shared_ptr<tunnel> create_tcp_tunnel(
    boost::asio::io_context& io, const tcp_endpoint& gateway, const tcp_endpoint& faraway, bool accept, uint64_t secret) noexcept(true)
{
    _dbg_ << "create tcp tunnel: gateway=" << gateway << " faraway=" << faraway << " accept=" << accept << " obsure=" << static_cast<bool>(secret);

    return accept
        ? std::shared_ptr<tunnel>(new tubus_tunnel<tubus::tcp_channel, true>(io, gateway, faraway, secret))
        : std::shared_ptr<tunnel>(new tubus_tunnel<tubus::tcp_channel, false>(io, gateway, faraway, secret));
}

}
