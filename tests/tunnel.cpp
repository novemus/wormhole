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
    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address_v4::any(), 0);
    acceptor.open(ep.protocol());
    acceptor.bind(ep);
    acceptor.listen();

    return acceptor.local_endpoint().port();
}

namespace {

std::shared_ptr<wormhole::tunnel> create_tunnel(boost::asio::io_context& io, wormhole::protocol proto, wormhole::relation role, unsigned short gateway, unsigned short faraway)
{
    static const uint64_t s_secret = 1234567890;

    static const wormhole::security::crypto s_client {
        "./certs/client.crt",
        "./certs/client.key",
        "./certs/ca.crt"
    };

    static const wormhole::security::crypto s_server {
        "./certs/server.crt",
        "./certs/server.key",
        "./certs/ca.crt"
    };

    if (proto == wormhole::protocol::ssl)
    {
        return wormhole::create_ssl_tunnel(
            io,
            boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), gateway),
            boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), faraway),
            role,
            role == wormhole::relation::server ? s_server : s_client
        );

    }
    else if (proto == wormhole::protocol::tcp)
    {
        return wormhole::create_tcp_tunnel(
            io,
            boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), gateway),
            boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), faraway),
            role,
            s_secret
        );
    }

    return wormhole::create_udp_tunnel(
        io,
        boost::asio::ip::udp::endpoint(boost::asio::ip::make_address("127.0.0.1"), gateway),
        boost::asio::ip::udp::endpoint(boost::asio::ip::make_address("127.0.0.1"), faraway),
        role,
        s_secret
    );
};

const char HELLO_WORMHOLE[] = "Hello, Wormhole!";

void test_tunnel(wormhole::protocol proto)
{
    boost::asio::io_context io;
    boost::asio::thread_pool pool;

    auto work = boost::asio::make_work_guard(io);
    boost::asio::post(pool, [&io] { io.run(); });
    boost::asio::post(pool, [&io] { io.run(); });
    boost::asio::post(pool, [&io] { io.run(); });

    auto cp = find_free_port();
    auto sp = find_free_port();
    auto client = create_tunnel(io, proto, wormhole::relation::client, cp, sp);
    auto server = create_tunnel(io, proto, wormhole::relation::server, sp, cp);

    auto scp = std::make_shared<std::promise<void>>();
    auto ssp = std::make_shared<std::promise<void>>();

    auto send = [](std::shared_ptr<wormhole::tunnel> tunnel, std::shared_ptr<std::promise<void>> promise)
    {
        auto packet1 = std::make_shared<wormhole::packet>(1, true);
        BOOST_REQUIRE_EQUAL(packet1->id(), 1);
        BOOST_REQUIRE_EQUAL(packet1->size(), wormhole::packet::header_size + wormhole::packet::max_payload_size);
        BOOST_REQUIRE_EQUAL(packet1->capacity(), wormhole::packet::max_payload_size);
        BOOST_REQUIRE_EQUAL(packet1->header().size(), wormhole::packet::header_size);
        BOOST_REQUIRE_EQUAL(packet1->payload().size(), wormhole::packet::max_payload_size);

        tunnel->send(packet1, [packet1](const boost::system::error_code& ec)
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

        tunnel->send(packet2, [packet2](const boost::system::error_code& ec)
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

        tunnel->send(packet3, [packet3, promise](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet3->id(), 3);
            BOOST_CHECK_EQUAL(packet3->size(), wormhole::packet::header_size + std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(packet3->capacity(), std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(packet3->header().size(), wormhole::packet::header_size);
            BOOST_REQUIRE_EQUAL(packet3->payload().size(), std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(std::memcmp(packet3->payload().data(), HELLO_WORMHOLE, std::strlen(HELLO_WORMHOLE)), 0);

            promise->set_value();
        });
    };

    auto rcp = std::make_shared<std::promise<void>>();
    auto rsp = std::make_shared<std::promise<void>>();

    auto receive = [](std::shared_ptr<wormhole::tunnel> tunnel, std::shared_ptr<std::promise<void>> promise)
    {
        auto packet1 = std::make_shared<wormhole::packet>();
        tunnel->receive(packet1, [packet1](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet1->id(), 1);
            BOOST_CHECK_EQUAL(packet1->size(), wormhole::packet::header_size + wormhole::packet::max_payload_size);
            BOOST_CHECK_EQUAL(packet1->capacity(), wormhole::packet::max_payload_size);
            BOOST_CHECK_EQUAL(packet1->header().size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet1->payload().size(), wormhole::packet::max_payload_size);
        });

        auto packet2 = std::make_shared<wormhole::packet>();
        tunnel->receive(packet2, [packet2](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet2->id(), 2);
            BOOST_CHECK_EQUAL(packet2->size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet2->capacity(), 0);
            BOOST_CHECK_EQUAL(packet2->header().size(), wormhole::packet::header_size);
            BOOST_CHECK_EQUAL(packet2->payload().size(), 0);
        });

        auto packet3 = std::make_shared<wormhole::packet>();
        tunnel->receive(packet3, [tunnel, packet3, promise](const boost::system::error_code& ec)
        {
            BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
            BOOST_CHECK_EQUAL(packet3->id(), 3);
            BOOST_CHECK_EQUAL(packet3->size(), wormhole::packet::header_size + std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(packet3->capacity(), std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(packet3->header().size(), wormhole::packet::header_size);
            BOOST_REQUIRE_EQUAL(packet3->payload().size(), std::strlen(HELLO_WORMHOLE));
            BOOST_CHECK_EQUAL(std::memcmp(packet3->payload().data(), HELLO_WORMHOLE, std::strlen(HELLO_WORMHOLE)), 0);

            promise->set_value();
        });
    };

    server->open([&](const boost::system::error_code& ec)
    {
        BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());

        send(server, ssp);
        receive(server, rsp);
    });

    client->open([&](const boost::system::error_code& ec)
    {
        BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());

        send(client, scp);
        receive(client, rcp);
    });

    BOOST_CHECK_NO_THROW(scp->get_future().get());
    BOOST_CHECK_NO_THROW(rcp->get_future().get());
    BOOST_CHECK_NO_THROW(ssp->get_future().get());
    BOOST_CHECK_NO_THROW(rsp->get_future().get());

    auto ccp = std::make_shared<std::promise<void>>();
    client->close([&](const boost::system::error_code& ec)
    {
        BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
        ccp->set_value();
    });

    auto csp = std::make_shared<std::promise<void>>();
    server->close([&](const boost::system::error_code& ec)
    {
        BOOST_REQUIRE_EQUAL(ec, boost::system::error_code());
        csp->set_value();
    });

    BOOST_CHECK_NO_THROW(ccp->get_future().get());
    BOOST_CHECK_NO_THROW(csp->get_future().get());

    work.reset();
    pool.join();
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
