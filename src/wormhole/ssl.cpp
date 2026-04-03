/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <wormhole/tunnel.h>
#include <wormhole/logger.h>
#include <boost/asio/ssl.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <queue>

#ifdef _WIN32
#include <mstcpip.h>
#else
#include <netinet/tcp.h>
#endif

namespace wormhole {

template<bool accept, bool server> 
class ssl_tunnel : public tunnel, public std::enable_shared_from_this<ssl_tunnel<accept, server>>
{
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream;
    typedef boost::asio::ip::tcp::acceptor tcp_acceptor;
    typedef boost::asio::ip::tcp::endpoint tcp_endpoint;
    typedef std::queue<std::pair<packet_ptr, callback>> defer_queue;

    enum status { s_none, s_half, s_open };

    boost::asio::io_context& m_io;
    boost::asio::io_context::strand m_strand;
    boost::asio::ssl::context m_ssl;
    boost::asio::deadline_timer m_timer;
    tcp_acceptor m_acceptor;
    ssl_stream m_stream;
    tcp_endpoint m_gateway;
    tcp_endpoint m_faraway;
    defer_queue m_wq;
    defer_queue m_rq;
    status m_status = status::s_none;

    void do_write(packet_ptr packet, const callback& handler) noexcept(true)
    {
        auto callback = [this, weak = this->weak_from_this(), handler](const boost::system::error_code& ec)
        {
            if (auto self = weak.lock())
            {
                if (!m_wq.empty())
                    m_wq.pop();

                if (!m_wq.empty())
                {
                    auto& next = m_wq.front();
                    do_write(next.first, next.second);
                }

                boost::asio::post(m_io, std::bind(handler, ec));
            }
            else
                handler(boost::asio::error::shut_down);
        };

        if (m_status != status::s_open)
        {
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::shut_down));
            return;
        }

        boost::asio::async_write(m_stream, *packet, m_strand.wrap([this, weak = this->weak_from_this(), packet, callback](const boost::system::error_code& ec, size_t size)
        {
            if (auto self = weak.lock())
            {
                if (ec)
                    boost::asio::post(m_strand, std::bind(callback, ec));
                else if (size != packet->size())
                    boost::asio::post(m_strand, std::bind(callback, boost::asio::error::message_size));
                else
                    boost::asio::post(m_strand, std::bind(callback, ec));
            }
            else
                callback(ec);
        }));
    }

    void do_read(packet_ptr packet, const callback& handler) noexcept(true)
    {
        auto callback = [this, weak = this->weak_from_this(), handler](const boost::system::error_code& ec)
        {
            if (auto self = weak.lock())
            {
                if (!m_rq.empty())
                    m_rq.pop();

                if (!m_rq.empty())
                {
                    auto& next = m_rq.front();
                    do_read(next.first, next.second);
                }

                boost::asio::post(m_io, std::bind(handler, ec));
            }
            else
                handler(boost::asio::error::shut_down);
        };

        if (m_status != status::s_open)
        {
            boost::asio::post(m_io, std::bind(handler, boost::asio::error::shut_down));
            return;
        }

        boost::asio::async_read(m_stream, packet->header(), m_strand.wrap([this, weak = this->weak_from_this(), packet, callback](const boost::system::error_code& ec, size_t size)
        {
            if (auto self = weak.lock())
            {
                if (ec)
                    boost::asio::post(m_strand, std::bind(callback, ec));
                else if (size < packet::header_size || packet->capacity() > packet::max_payload_size)
                    boost::asio::post(m_strand, std::bind(callback, boost::asio::error::message_size));
                else
                {
                    packet->shrink(packet->capacity());
                    if (packet->capacity() == 0)
                    {
                        boost::asio::post(m_strand, std::bind(callback, ec));
                        return;
                    }

                    boost::asio::post(m_strand, [this, weak, packet, callback]()
                    {
                        if (auto self = weak.lock())
                        {
                            if (m_status == status::s_open)
                            {
                                boost::asio::async_read(m_stream, packet->payload(), m_strand.wrap([this, weak, packet, callback](const boost::system::error_code& ec, size_t size)
                                {
                                    if (auto self = weak.lock())
                                    {
                                        if (ec)
                                            boost::asio::post(m_strand, std::bind(callback, ec));
                                        else if (size != packet->capacity())
                                            boost::asio::post(m_strand, std::bind(callback, boost::asio::error::message_size));
                                        else
                                            boost::asio::post(m_strand, std::bind(callback, ec));
                                    }
                                    else
                                        callback(ec);
                                }));
                            }
                            else
                                boost::asio::post(m_strand, std::bind(callback, boost::asio::error::shut_down));
                        }
                        else
                            callback(boost::asio::error::shut_down);
                    });
                }
            }
            else
                callback(boost::asio::error::shut_down);
        }));
    }

    void do_accept(const callback& handler, const boost::posix_time::ptime& deadline) noexcept(true)
    {
        if (m_stream.next_layer().is_open())
        {
            boost::system::error_code ec;
            m_stream.next_layer().close(ec);
        }

        if (!deadline.is_not_a_date_time())
        {
            m_acceptor.open(m_gateway.protocol());
            m_acceptor.set_option(boost::asio::socket_base::reuse_address(true));
            m_acceptor.set_option(boost::asio::socket_base::keep_alive(true));
            m_acceptor.bind(m_gateway);
            m_acceptor.listen();

            m_timer.expires_at(deadline);
            m_timer.async_wait(m_strand.wrap([this, weak = this->weak_from_this()](const boost::system::error_code& error)
            {
                if (!error)
                {
                    if (auto self = weak.lock())
                    {
                        boost::system::error_code ec;
                        m_acceptor.close(ec);
                    }
                }
            }));
        }

        m_acceptor.async_accept(m_stream.next_layer(), m_strand.wrap([this, weak = this->weak_from_this(), handler](const boost::system::error_code& err)
        {
            if (auto self = weak.lock())
            {
                auto expected = [&]()
                {
                    auto actual = m_stream.next_layer().remote_endpoint();

                    if (m_faraway.port() != 0 && m_faraway.port() != actual.port())
                        return false;
                    if (!m_faraway.address().is_unspecified() && m_faraway.address() != actual.address())
                        return false;

                    return true;
                };

                if (err || expected())
                {
                    if (!err)
                    {
                        set_keepalive_time();

                        auto role = server ? boost::asio::ssl::stream_base::server : boost::asio::ssl::stream_base::client;
                        m_stream.async_handshake(role, m_strand.wrap([this, weak, handler](const boost::system::error_code& err)
                        {
                            if (auto self = weak.lock())
                            {
                                m_status = status::s_open;
                                boost::asio::post(m_io, std::bind(handler, err));
                            }
                            else
                                handler(err);
                        }));
                    }
                    else
                        boost::asio::post(m_io, std::bind(handler, err));

                    boost::system::error_code ec;
                    m_timer.cancel(ec);
                    m_acceptor.close(ec);
                }
                else
                {
                    boost::asio::post(m_io, [this, weak, handler]()
                    {
                        if (auto self = weak.lock())
                        {
                            if (m_status == status::s_none)
                                handler(boost::asio::error::operation_aborted);
                            else
                                do_accept(handler, boost::posix_time::ptime());
                        }
                        else
                            handler(boost::asio::error::operation_aborted);
                    });
                }
            }
            else
                handler(boost::asio::error::operation_aborted);
        }));
    }

    void do_connect(const callback& handler, const boost::posix_time::ptime& deadline) noexcept(true)
    {
        if (m_stream.next_layer().is_open())
        {
            boost::system::error_code ec;
            m_stream.next_layer().close(ec);
        }

        m_stream.next_layer().open(m_gateway.protocol());
        m_stream.next_layer().set_option(boost::asio::socket_base::reuse_address(true));
        m_stream.next_layer().set_option(boost::asio::socket_base::keep_alive(true));
        m_stream.next_layer().bind(m_gateway);

        m_stream.next_layer().async_connect(m_faraway, m_strand.wrap([this, weak = this->weak_from_this(), handler, deadline](const boost::system::error_code& err)
        {
            if (auto self = weak.lock())
            {
                if (err != boost::asio::error::connection_refused || boost::posix_time::microsec_clock::universal_time() > deadline)
                {
                    if (!err)
                    {
                        set_keepalive_time();
 
                        auto role = server ? boost::asio::ssl::stream_base::server : boost::asio::ssl::stream_base::client;
                        m_stream.async_handshake(role, m_strand.wrap([this, weak, handler](const boost::system::error_code& err)
                        {
                            if (auto self = weak.lock())
                            {
                                m_status = status::s_open;
                                boost::asio::post(m_io, std::bind(handler, err));
                            }
                            else
                                handler(err);
                        }));
                    }
                    else
                        boost::asio::post(m_io, std::bind(handler, err));
                }
                else if (m_status == status::s_none)
                {
                    boost::asio::post(m_io, std::bind(handler, boost::asio::error::operation_aborted));
                }
                else
                {
                    m_timer.expires_from_now(boost::posix_time::seconds(1));
                    m_timer.async_wait(m_strand.wrap([this, weak, handler, deadline](const boost::system::error_code& err)
                    {
                        if (auto self = weak.lock())
                        {
                            if (!err)
                            {
                                if (m_status == status::s_none)
                                    boost::asio::post(m_io, std::bind(handler, boost::asio::error::operation_aborted));
                                else
                                    do_connect(handler, boost::posix_time::ptime());
                            }
                            else
                                boost::asio::post(m_io, std::bind(handler, err));
                        }
                        else
                            handler(boost::asio::error::operation_aborted);
                    }));
                }
            }
            else
                handler(boost::asio::error::operation_aborted);
        }));
    }

    void set_keepalive_time()
    {
        boost::asio::ip::tcp::socket::keep_alive keep;
        boost::system::error_code ec;
        m_stream.next_layer().get_option(keep, ec);

        int32_t timeout = 30000;
        try
        {
            const char *env = std::getenv("TUBUS_TCP_KEEPALIVE_TIMEOUT");
            if (env)
                timeout = std::stoi(env);
        }
        catch (const std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
        }

        if (!m_stream.next_layer().get_option(keep, ec) && keep.value()) 
        {
#ifdef _WIN32
            tcp_keepalive keepalive;
            keepalive.onoff = 1;
            keepalive.keepalivetime = static_cast<unsigned long>(timeout);
            keepalive.keepaliveinterval = std::max(1000UL, keepalive.keepalivetime);
            DWORD returned = 0;
            WSAIoctl(m_stream.next_layer().native_handle(), SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &returned, NULL, NULL);
#else
            setsockopt(m_stream.next_layer().native_handle(), IPPROTO_TCP, TCP_KEEPIDLE, &timeout, sizeof(timeout));
#endif
        }
    }

    static boost::asio::ssl::context make_ssl_context(const security::privacy& crypto) noexcept(true)
    {
        boost::asio::ssl::context ssl(boost::asio::ssl::context::sslv23);
        if (server)
            ssl.set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::sslv23_server);
        else
            ssl.set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::sslv23_client);

        if (!crypto.cert.empty() && !crypto.key.empty())
        {
            ssl.use_certificate_file(crypto.cert, boost::asio::ssl::context::pem);
            ssl.use_private_key_file(crypto.key, boost::asio::ssl::context::pem);
        }

        if (!crypto.ca.empty())
        {
            if (server)
                ssl.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert | boost::asio::ssl::verify_client_once);
            else
                ssl.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
            ssl.load_verify_file(crypto.ca);
        }

        return std::move(ssl);
    }

public:

    ssl_tunnel(boost::asio::io_context& io, const tcp_endpoint& gateway, const tcp_endpoint& faraway, const security::privacy& crypto)
        : m_io(io)
        , m_strand(io)
        , m_ssl(make_ssl_context(crypto))
        , m_timer(io)
        , m_acceptor(io)
        , m_stream(io, m_ssl)
        , m_gateway(gateway)
        , m_faraway(faraway)
    {
    }

    void open(const callback& handler) noexcept(true) override
    {
        boost::asio::post(m_strand, [this, weak = this->weak_from_this(), handler]()
        {
            if (auto self = weak.lock())
            {
                if (m_status == status::s_none)
                {
                    m_status = status::s_half;
                    auto deadline = boost::posix_time::microsec_clock::universal_time() + boost::posix_time::seconds(10);
                    if (accept)
                        do_accept(handler, deadline);
                    else
                        do_connect(handler, deadline);
                }
                else
                    handler(boost::asio::error::already_started); 
            }
            else
                handler(boost::asio::error::operation_aborted);
        });
    }

    void close(const callback& handler) noexcept(true) override
    {
        boost::asio::post(m_strand, [this, weak = this->weak_from_this(), handler]()
        {
            boost::system::error_code ec;
            if (auto self = weak.lock())
            {
                if (m_status == status::s_open)
                {
                    m_status = status::s_none;
                    m_stream.next_layer().cancel(ec);

                    m_timer.expires_from_now(boost::posix_time::seconds(1));
                    m_timer.async_wait(m_strand.wrap([this, weak](const boost::system::error_code& err)
                    {
                        if (auto self = weak.lock())
                        {
                            boost::system::error_code ec;
                            m_stream.next_layer().close(ec);
                        }
                    }));

                    m_stream.async_shutdown(m_strand.wrap([this, weak, handler](const boost::system::error_code& err)
                    {
                        if (auto self = weak.lock())
                        {
                            boost::system::error_code ec;
                            m_timer.cancel(ec);
                            m_stream.next_layer().close(ec);
                        }
                        handler(err);
                    }));
                    return;
                }

                if (m_status == status::s_half)
                {
                    m_timer.cancel(ec);
                    m_acceptor.close(ec);
                    m_stream.next_layer().close(ec);
                    m_status = status::s_none;
                }
            }

            handler(ec);
        });
    }

    void send(packet_ptr packet, const callback& handler) noexcept(true) override
    {
        boost::asio::post(m_strand, [this, weak = this->weak_from_this(), packet, handler]()
        {
            if (auto self = weak.lock())
            {
                if (m_status == status::s_open)
                {
                    m_wq.emplace(packet, handler);
                    if (m_wq.size() == 1)
                        do_write(packet, handler);
                    return;
                }
            }
            handler(boost::asio::error::not_connected);
        });
    }

    void receive(packet_ptr packet, const callback& handler) noexcept(true) override
    {
        boost::asio::post(m_strand, [this, weak = this->weak_from_this(), packet, handler]()
        {
            if (auto self = weak.lock())
            {
                if (m_status == status::s_open)
                {
                    m_rq.emplace(packet, handler);
                    if (m_rq.size() == 1)
                        do_read(packet, handler);
                    return;
                }
            }
            handler(boost::asio::error::not_connected);
        });
    }
};

std::shared_ptr<tunnel> create_ssl_tunnel(
    boost::asio::io_context& io, const tcp_endpoint& gateway, const tcp_endpoint& faraway, bool accept, bool server, const security::privacy& crypto) noexcept(true)
{
    _dbg_ << "create ssl tunnel: gateway=" << gateway << " faraway=" << faraway << " accept=" << accept << " server=" << server << " crypto=" << static_cast<bool>(crypto.cert.c_str());

    if (accept)
    {
        return server 
            ? std::shared_ptr<tunnel>(new ssl_tunnel<true, true>(io, gateway, faraway, crypto))
            : std::shared_ptr<tunnel>(new ssl_tunnel<true, false>(io, gateway, faraway, crypto));
    }
    return server 
            ? std::shared_ptr<tunnel>(new ssl_tunnel<false, true>(io, gateway, faraway, crypto))
            : std::shared_ptr<tunnel>(new ssl_tunnel<false, false>(io, gateway, faraway, crypto));
}

}
