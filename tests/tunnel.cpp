/*
 * Copyright (c) 2022 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <future>
#include <wormhole/tunnel.h>
#include <wormhole/logger.h>
#include <boost/asio/thread_pool.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>

unsigned short find_free_port()
{
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor(io);
    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address_v4::loopback(), 0);
    acceptor.open(ep.protocol());
    acceptor.bind(ep);
    acceptor.listen();

    return acceptor.local_endpoint().port();
}

namespace {

std::shared_ptr<wormhole::tunnel> create_tunnel(boost::asio::io_context& io, wormhole::protocol proto, wormhole::schema role, unsigned short gateway, unsigned short faraway)
{
    static const uint64_t s_secret = 1234567890;

    static const wormhole::security::privacy s_client {
        "./certs/client.crt",
        "./certs/client.key",
        "./certs/ca.crt"
    };

    static const wormhole::security::privacy s_server {
        "./certs/server.crt",
        "./certs/server.key",
        "./certs/ca.crt"
    };

    if (proto == wormhole::protocol::ssl)
    {
        return wormhole::create_ssl_tunnel(
            io,
            boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), gateway),
            boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), faraway),
            role != wormhole::schema::client,
            role != wormhole::schema::client,
            role == wormhole::schema::server ? s_server : s_client
        );
    }
    else if (proto == wormhole::protocol::tcp)
    {
        return wormhole::create_tcp_tunnel(
            io,
            boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), gateway),
            boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), faraway),
            role != wormhole::schema::client,
            s_secret
        );
    }

    return wormhole::create_udp_tunnel(
        io,
        boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::loopback(), gateway),
        boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::loopback(), faraway),
        role != wormhole::schema::client,
        s_secret
    );
};

static constexpr char HELLO_WORMHOLE[] = "Hello, Wormhole!";

void test_tunnel(wormhole::protocol proto)
{
    boost::asio::io_context io;

    auto cp = find_free_port();
    auto sp = find_free_port();
    auto client = create_tunnel(io, proto, wormhole::schema::client, cp, sp);
    auto server = create_tunnel(io, proto, wormhole::schema::server, sp, cp);

    std::shared_ptr<void> closer(nullptr, [client, server](void*)
    {
        client->close([](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
        });

        server->close([](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
        });
    });

    auto send = [](std::shared_ptr<wormhole::tunnel> tunnel, std::shared_ptr<void> closer)
    {
        auto packet1 = std::make_shared<wormhole::packet>(1, true);
        BOOST_REQUIRE_EQUAL(packet1->id(), 1);
        BOOST_REQUIRE_EQUAL(packet1->size(), wormhole::packet::header_size + wormhole::packet::max_payload_size);
        BOOST_REQUIRE_EQUAL(packet1->capacity(), wormhole::packet::max_payload_size);
        BOOST_REQUIRE_EQUAL(packet1->header().size(), wormhole::packet::header_size);
        BOOST_REQUIRE_EQUAL(packet1->payload().size(), wormhole::packet::max_payload_size);

        tunnel->send(packet1, [packet1, closer](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet1->id(), 1);
            BOOST_CHECK_EQUAL(packet1->size(), wormhole::packet::header_size + wormhole::packet::max_payload_size);
            BOOST_CHECK_EQUAL(packet1->capacity(), wormhole::packet::max_payload_size);
            BOOST_CHECK_EQUAL(packet1->header().size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet1->payload().size(), wormhole::packet::max_payload_size);
        });

        auto packet2 = std::make_shared<wormhole::packet>(2, false);
        BOOST_REQUIRE_EQUAL(packet2->id(), 2);
        BOOST_REQUIRE_EQUAL(packet2->size(), wormhole::packet::header_size);
        BOOST_REQUIRE_EQUAL(packet2->capacity(), 0);
        BOOST_REQUIRE_EQUAL(packet2->header().size(), wormhole::packet::header_size);
        BOOST_REQUIRE_EQUAL(packet2->payload().size(), 0);

        tunnel->send(packet2, [packet2, closer](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet2->id(), 2);
            BOOST_CHECK_EQUAL(packet2->size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet2->capacity(), 0);
            BOOST_CHECK_EQUAL(packet2->header().size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet2->payload().size(), 0);
        });

        auto packet3 = std::make_shared<wormhole::packet>(3, true);
        packet3->shrink(std::strlen(HELLO_WORMHOLE));
        packet3->payload().fill(0, std::strlen(HELLO_WORMHOLE), HELLO_WORMHOLE);

        BOOST_REQUIRE_EQUAL(packet3->id(), 3);
        BOOST_REQUIRE_EQUAL(packet3->size(), wormhole::packet::header_size + std::strlen(HELLO_WORMHOLE));
        BOOST_REQUIRE_EQUAL(packet3->capacity(), std::strlen(HELLO_WORMHOLE));
        BOOST_REQUIRE_EQUAL(packet3->header().size(), wormhole::packet::header_size);
        BOOST_REQUIRE_EQUAL(packet3->payload().size(), std::strlen(HELLO_WORMHOLE));

        tunnel->send(packet3, [packet3, closer](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet3->id(), 3);
            BOOST_CHECK_EQUAL(packet3->size(), wormhole::packet::header_size + std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(packet3->capacity(), std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(packet3->header().size(), wormhole::packet::header_size);
            BOOST_REQUIRE_EQUAL(packet3->payload().size(), std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(std::memcmp(packet3->payload().data(), HELLO_WORMHOLE, std::strlen(HELLO_WORMHOLE)), 0);
        });
    };

    auto receive = [](std::shared_ptr<wormhole::tunnel> tunnel, std::shared_ptr<void> closer)
    {
        auto packet1 = std::make_shared<wormhole::packet>();
        tunnel->receive(packet1, [packet1, closer](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet1->id(), 1);
            BOOST_CHECK_EQUAL(packet1->size(), wormhole::packet::header_size + wormhole::packet::max_payload_size);
            BOOST_CHECK_EQUAL(packet1->capacity(), wormhole::packet::max_payload_size);
            BOOST_CHECK_EQUAL(packet1->header().size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet1->payload().size(), wormhole::packet::max_payload_size);
        });

        auto packet2 = std::make_shared<wormhole::packet>();
        tunnel->receive(packet2, [packet2, closer](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet2->id(), 2);
            BOOST_CHECK_EQUAL(packet2->size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet2->capacity(), 0);
            BOOST_CHECK_EQUAL(packet2->header().size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet2->payload().size(), 0);
        });

        auto packet3 = std::make_shared<wormhole::packet>();
        tunnel->receive(packet3, [packet3, closer](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet3->id(), 3);
            BOOST_CHECK_EQUAL(packet3->size(), wormhole::packet::header_size + std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(packet3->capacity(), std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(packet3->header().size(), wormhole::packet::header_size);
            BOOST_REQUIRE_EQUAL(packet3->payload().size(), std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(std::memcmp(packet3->payload().data(), HELLO_WORMHOLE, std::strlen(HELLO_WORMHOLE)), 0);
        });
    };

    BOOST_REQUIRE_NO_THROW(server->open([&, closer](const boost::system::error_code& ec)
    {
        BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());

        BOOST_REQUIRE_NO_THROW(send(server, closer));
        BOOST_REQUIRE_NO_THROW(receive(server, closer));
    }));

    BOOST_REQUIRE_NO_THROW(client->open([&, closer](const boost::system::error_code& ec)
    {
        BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());

        BOOST_REQUIRE_NO_THROW(send(client, closer));
        BOOST_REQUIRE_NO_THROW(receive(client, closer));
    }));

    closer.reset();

    BOOST_CHECK_NO_THROW(io.run());
}

}

BOOST_AUTO_TEST_CASE(ssl_tunnel)
{
    test_tunnel(wormhole::protocol::ssl);
}

BOOST_AUTO_TEST_CASE(tcp_tunnel)
{
    test_tunnel(wormhole::protocol::tcp);
}

BOOST_AUTO_TEST_CASE(udp_tunnel)
{
    test_tunnel(wormhole::protocol::udp);
}
