/*
 * Copyright (c) 2022 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../wormhole.h"
#include "../logger.h"
#include "../executor.h"
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>
#include <mutex>
#include <set>

namespace {

enum { max_length = 1024 };

class tcp_echo_session : public std::enable_shared_from_this<tcp_echo_session>
{
    char m_data[max_length];
    boost::asio::ip::tcp::socket m_socket;

public:

    tcp_echo_session(boost::asio::io_service& io)
        : m_socket(io)
    {
    }

    boost::asio::ip::tcp::socket& socket()
    {
        return m_socket;
    }

    void start()
    {
        m_socket.async_read_some(
            boost::asio::buffer(m_data, max_length),
            boost::bind(&tcp_echo_session::handle_read, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
            );
    }

protected:

    void handle_read(const boost::system::error_code &error, size_t transferred)
    {
        if (!error)
        {
            m_socket.async_write_some(
                boost::asio::buffer(m_data, transferred),
                boost::bind(&tcp_echo_session::handle_write, shared_from_this(), boost::asio::placeholders::error)
                );
        }
    }

    void handle_write(const boost::system::error_code &error)
    {
        if (!error)
        {
            m_socket.async_read_some(
                boost::asio::buffer(m_data, max_length),
                boost::bind(&tcp_echo_session::handle_read, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
                );
        }
    }
};

class tcp_echo_server : public std::enable_shared_from_this<tcp_echo_server>
{
    boost::asio::io_service& m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::set<std::shared_ptr<tcp_echo_session>> m_sessions;
    std::mutex m_mutex;

public:

    tcp_echo_server(boost::asio::io_service& io, const boost::asio::ip::tcp::endpoint& ep)
        : m_io(io)
        , m_acceptor(io, ep)
    {
    }

    ~tcp_echo_server()
    {
        stop();
    }

    void start()
    {
        accept();
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        boost::system::error_code ec;
        m_acceptor.cancel(ec);
        m_acceptor.close(ec);

        for(auto& session : m_sessions)
            session->socket().cancel(ec);
    }

protected:

    void accept()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_acceptor.is_open())
        {
            auto session = std::make_shared<tcp_echo_session>(m_io);
            m_sessions.emplace(session);
            m_acceptor.async_accept(session->socket(), [self = shared_from_this(), session](const boost::system::error_code &error)
            {
                if (!error)
                    session->start();

                self->accept();
            });
        }
    }
};

std::shared_ptr<tcp_echo_server> create_tcp_server(boost::asio::io_service& io, const boost::asio::ip::tcp::endpoint& ep)
{
    return std::make_shared<tcp_echo_server>(io, ep);
}

const char HELLO_WORMHOLE[] = "Hello, Wormhold!";
const char WORMHOLE_HELLO[] = "Wormhold, Hello!";

const boost::asio::ip::tcp::endpoint SERVER(boost::asio::ip::address::from_string("127.0.0.1"), 18765);
const boost::asio::ip::tcp::endpoint PROXY(boost::asio::ip::address::from_string("127.0.0.1"), 15678);
const boost::asio::ip::udp::endpoint SERVER_GATEWAY(boost::asio::ip::address::from_string("127.0.0.1"), 7777);
const boost::asio::ip::udp::endpoint CLIENT_GATEWAY(boost::asio::ip::address::from_string("127.0.0.1"), 8888);

}

BOOST_AUTO_TEST_CASE(hello_wormhole)
{
    wormhole::log::set(wormhole::log::debug);

    wormhole::executor io;

    auto server = create_tcp_server(io, SERVER);
    BOOST_REQUIRE_NO_THROW(server->start());

    io.operate(3);

    auto exporter = wormhole::create_exporter(io, SERVER, SERVER_GATEWAY, CLIENT_GATEWAY, 0);
    BOOST_REQUIRE_NO_THROW(exporter->launch());

    auto importer = wormhole::create_importer(io, PROXY, CLIENT_GATEWAY, SERVER_GATEWAY, 0);
    BOOST_REQUIRE_NO_THROW(importer->launch());

    boost::asio::ip::tcp::socket client1(io, boost::asio::ip::tcp::v4());
    boost::asio::ip::tcp::socket client2(io, boost::asio::ip::tcp::v4());

    BOOST_REQUIRE_NO_THROW(client1.connect(PROXY));
    BOOST_REQUIRE_NO_THROW(client2.connect(PROXY));

    char buffer[max_length];

    BOOST_REQUIRE_NO_THROW(BOOST_REQUIRE_EQUAL(boost::asio::write(client1, boost::asio::buffer(HELLO_WORMHOLE, sizeof(HELLO_WORMHOLE))), sizeof(HELLO_WORMHOLE)));
    BOOST_REQUIRE_NO_THROW(BOOST_REQUIRE_EQUAL(boost::asio::write(client2, boost::asio::buffer(WORMHOLE_HELLO, sizeof(WORMHOLE_HELLO))), sizeof(WORMHOLE_HELLO)));

    BOOST_REQUIRE_NO_THROW(BOOST_REQUIRE_EQUAL(boost::asio::read(client1, boost::asio::buffer(buffer, sizeof(HELLO_WORMHOLE))), sizeof(HELLO_WORMHOLE)));
    BOOST_REQUIRE_EQUAL(std::memcmp(buffer, HELLO_WORMHOLE, sizeof(HELLO_WORMHOLE)), 0);
    
    BOOST_REQUIRE_NO_THROW(BOOST_REQUIRE_EQUAL(boost::asio::read(client2, boost::asio::buffer(buffer, sizeof(WORMHOLE_HELLO))), sizeof(WORMHOLE_HELLO)));
    BOOST_REQUIRE_EQUAL(std::memcmp(buffer, WORMHOLE_HELLO, sizeof(WORMHOLE_HELLO)), 0);

    BOOST_REQUIRE_NO_THROW(BOOST_REQUIRE_EQUAL(boost::asio::write(client1, boost::asio::buffer(HELLO_WORMHOLE, sizeof(HELLO_WORMHOLE))), sizeof(HELLO_WORMHOLE)));
    BOOST_REQUIRE_NO_THROW(BOOST_REQUIRE_EQUAL(boost::asio::write(client2, boost::asio::buffer(WORMHOLE_HELLO, sizeof(WORMHOLE_HELLO))), sizeof(WORMHOLE_HELLO)));

    BOOST_REQUIRE_NO_THROW(BOOST_REQUIRE_EQUAL(boost::asio::read(client1, boost::asio::buffer(buffer, sizeof(HELLO_WORMHOLE))), sizeof(HELLO_WORMHOLE)));
    BOOST_REQUIRE_EQUAL(std::memcmp(buffer, HELLO_WORMHOLE, sizeof(HELLO_WORMHOLE)), 0);
    
    BOOST_REQUIRE_NO_THROW(BOOST_REQUIRE_EQUAL(boost::asio::read(client2, boost::asio::buffer(buffer, sizeof(WORMHOLE_HELLO))), sizeof(WORMHOLE_HELLO)));
    BOOST_REQUIRE_EQUAL(std::memcmp(buffer, WORMHOLE_HELLO, sizeof(WORMHOLE_HELLO)), 0);

    BOOST_REQUIRE_NO_THROW(client1.shutdown(boost::asio::ip::tcp::socket::shutdown_both));
    BOOST_REQUIRE_NO_THROW(client2.shutdown(boost::asio::ip::tcp::socket::shutdown_both));

    BOOST_REQUIRE_NO_THROW(client1.close());
    BOOST_REQUIRE_NO_THROW(client2.close());

    BOOST_REQUIRE_NO_THROW(importer->cancel());
    BOOST_REQUIRE_NO_THROW(exporter->cancel());

    BOOST_REQUIRE_NO_THROW(server->stop());
}
