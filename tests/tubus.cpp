/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../buffer.h"
#include "../reactor.h"
#include "../tubus.h"
#include <future>
#include <functional>
#include <boost/asio/ip/tcp.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#define ASYNC_IO(object, method, buffer, filter) \
return std::async([obj = object, buffer]() \
{ \
    std::promise<void> promise; \
    std::future<void> future = promise.get_future(); \
    obj->method(buffer, [&promise](const boost::system::error_code& error, size_t size) \
    { \
        if (filter) \
            promise.set_exception(std::make_exception_ptr(boost::system::system_error(error))); \
        else \
            promise.set_value(); \
    }); \
    future.get(); \
}) \

#define ASYNC(object, method, filter) \
return std::async([obj = object]() \
{ \
    std::promise<void> promise; \
    std::future<void> future = promise.get_future(); \
    obj->method([&promise](const boost::system::error_code& error) \
    { \
        if (filter) \
            promise.set_exception(std::make_exception_ptr(boost::system::system_error(error))); \
        else \
            promise.set_value(); \
    }); \
    future.get(); \
}) \

class tubus_channel
{
    boost::asio::ip::udp::endpoint m_bind;
    boost::asio::ip::udp::endpoint m_peer;
    uint64_t m_secret;
    std::shared_ptr<wormhole::tubus::channel> m_channel;

public:

    tubus_channel(const boost::asio::ip::udp::endpoint& b, const boost::asio::ip::udp::endpoint& p, uint64_t s)
        : m_bind(b)
        , m_peer(p)
        , m_secret(s)
    {
    }

    ~tubus_channel()
    {
        m_channel->close();
    }

    void open()
    {
        m_channel = wormhole::tubus::create_channel(wormhole::shared_reactor(), m_bind, m_peer, m_secret);
        m_channel->open();
    }

    void close()
    {
        m_channel->close();
    }

    std::future<void> async_accept()
    {
        ASYNC(m_channel, accept, error);
    }

    std::future<void> async_connect()
    {
        ASYNC(m_channel, connect, error);
    }

    std::future<void> async_shutdown()
    {
        ASYNC(m_channel, shutdown, error && error != boost::asio::error::interrupted && error != boost::asio::error::connection_refused);
    }

    std::future<void> async_write(const wormhole::const_buffer& buffer)
    {
        ASYNC_IO(m_channel, write, buffer, error);
    }

    std::future<void> async_read(const wormhole::mutable_buffer& buffer)
    {
        ASYNC_IO(m_channel, read, buffer, error);
    }
};

class mediator
{
    wormhole::reactor_ptr m_reactor;
    boost::asio::ip::udp::endpoint m_le;
    boost::asio::ip::udp::endpoint m_re;
    boost::asio::ip::udp::socket m_bs;
    boost::asio::ip::udp::endpoint m_ep;
    wormhole::mutable_buffer m_rb;

    void on_sent(const boost::system::error_code& e, size_t s)
    {
        if (e)
        {
            if (e != boost::asio::error::operation_aborted)
                BOOST_TEST_MESSAGE("mediator: " << e.message());
            return;
        }

        receive();
    }

    void on_received(const boost::system::error_code& e, size_t s)
    {
        if(e)
        {
            if (e != boost::asio::error::operation_aborted)
                BOOST_TEST_MESSAGE("mediator: " << e.message());
            return;
        }

        if (std::rand() % 3 == 0)
        {
            receive();
            return;
        }

        boost::asio::ip::udp::endpoint to = m_ep == m_le ? m_re : m_le;
        m_bs.async_send_to(m_rb.slice(0, s), to, std::bind(&mediator::on_sent, this, std::placeholders::_1, std::placeholders::_2));
    }

    void receive()
    {
        m_bs.async_receive_from(m_rb, m_ep, std::bind(&mediator::on_received, this, std::placeholders::_1, std::placeholders::_2));
    }

public:

    mediator(const boost::asio::ip::udp::endpoint& b, const boost::asio::ip::udp::endpoint& l, const boost::asio::ip::udp::endpoint& r)
        : m_reactor(wormhole::shared_reactor())
        , m_le(l)
        , m_re(r)
        , m_bs(m_reactor->io(), b.protocol())
        , m_rb(65507)
    {
        m_bs.set_option(boost::asio::socket_base::reuse_address(true));
        m_bs.bind(b);

        receive();
    }
};

class stream_source
{
    size_t m_read = 0;

public:

    wormhole::const_buffer read_some()
    {
        wormhole::mutable_buffer chank(1024 * 1024);

        uint8_t* ptr = (uint8_t*)chank.data();
        uint8_t* end = ptr + chank.size();

        while (ptr < end)
        {
            *ptr = (m_read++) % 256;
            ++ptr;
        }

        return chank;
    }

    size_t read() const
    {
        return m_read;
    }
};

class stream_sink
{
    size_t m_written = 0;

public:

    void write_some(const wormhole::const_buffer& chank)
    {
        const uint8_t* ptr = (const uint8_t*)chank.data();
        const uint8_t* end = ptr + chank.size();

        while (ptr < end)
        {
            if (*ptr != (m_written++) % 256)
                throw std::runtime_error("bad stream");
            ++ptr;
        }
    }

    size_t written() const
    {
        return m_written;
    }
};

BOOST_AUTO_TEST_CASE(tubus_core)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_channel left(le, re, 1234567890);
    tubus_channel right(re, le, 1234567890);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    uint8_t data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    wormhole::mutable_buffer lb(sizeof(data));
    wormhole::mutable_buffer rb(sizeof(data));

    std::memcpy(lb.data(), data, lb.size());
    std::memcpy(rb.data(), data, rb.size());

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    for(size_t i = 0; i < sizeof(data); ++i)
    {
        BOOST_REQUIRE_NO_THROW(left.async_write(lb.slice(i, 1)).get());
        BOOST_REQUIRE_NO_THROW(right.async_write(rb.slice(i, 1)).get());
    }

    std::memset(lb.data(), 0, lb.size());
    std::memset(rb.data(), 0, rb.size());

    BOOST_REQUIRE_NO_THROW(left.async_read(lb).get());
    BOOST_CHECK_EQUAL(std::memcmp(lb.data(), data, lb.size()), 0);

    BOOST_REQUIRE_NO_THROW(right.async_read(rb).get());
    BOOST_CHECK_EQUAL(std::memcmp(rb.data(), data, rb.size()), 0);

    auto ls = left.async_shutdown();
    auto rs = right.async_shutdown();

    BOOST_REQUIRE_NO_THROW(ls.get());
    BOOST_REQUIRE_NO_THROW(rs.get());
}

BOOST_AUTO_TEST_CASE(tubus_connectivity)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_channel left(le, re, 1234567890);
    BOOST_REQUIRE_NO_THROW(left.open());

    tubus_channel right(re, le, 1234567890);
    BOOST_REQUIRE_NO_THROW(right.open());

    auto la = left.async_accept();
    BOOST_CHECK_EQUAL((int)la.wait_for(std::chrono::seconds(3)), (int)std::future_status::timeout);
    BOOST_REQUIRE_THROW(left.async_shutdown().get(), boost::system::system_error);
    BOOST_REQUIRE_NO_THROW(left.close());

    auto rc = right.async_connect();
    BOOST_REQUIRE_THROW(rc.get(), boost::system::system_error);
    BOOST_REQUIRE_NO_THROW(right.async_shutdown().get());
    BOOST_REQUIRE_NO_THROW(right.close());

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    auto a = left.async_accept();
    auto c = right.async_connect();

    BOOST_REQUIRE_NO_THROW(a.get());
    BOOST_REQUIRE_NO_THROW(c.get());

    BOOST_REQUIRE_NO_THROW(left.async_shutdown().get());

    BOOST_REQUIRE_THROW(left.async_read(wormhole::mutable_buffer(1)).get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_write(wormhole::mutable_buffer(1)).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(right.async_shutdown().get());
}

BOOST_AUTO_TEST_CASE(tubus_integrity)
{
    boost::asio::ip::udp::endpoint pe(boost::asio::ip::address::from_string("127.0.0.1"), 3000);
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    mediator proxy(pe, le, re);

    tubus_channel left(le, pe, 0);
    tubus_channel right(re, pe, 0);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    stream_source source;
    stream_sink sink;

    BOOST_REQUIRE_NO_THROW(left.async_write(source.read_some()).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(source.read_some()).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(source.read_some()).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(source.read_some()).get());

    wormhole::mutable_buffer buffer(source.read() / 4);

    BOOST_REQUIRE_NO_THROW(right.async_read(buffer).get());
    BOOST_REQUIRE_NO_THROW(sink.write_some(buffer));
    BOOST_REQUIRE_NO_THROW(right.async_read(buffer).get());
    BOOST_REQUIRE_NO_THROW(sink.write_some(buffer));
    BOOST_REQUIRE_NO_THROW(right.async_read(buffer).get());
    BOOST_REQUIRE_NO_THROW(sink.write_some(buffer));
    BOOST_REQUIRE_NO_THROW(right.async_read(buffer).get());
    BOOST_REQUIRE_NO_THROW(sink.write_some(buffer));

    BOOST_CHECK_EQUAL(source.read(), sink.written());

    auto rr = right.async_read(buffer.slice(0, 1));
    BOOST_CHECK_EQUAL((int)rr.wait_for(std::chrono::seconds(3)), (int)std::future_status::timeout);

    auto ls = left.async_shutdown();
    auto rs = right.async_shutdown();

    BOOST_REQUIRE_NO_THROW(ls.get());
    BOOST_REQUIRE_NO_THROW(rs.get());
}

BOOST_AUTO_TEST_CASE(tubus_fall)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_channel left(le, re, 0);
    tubus_channel right(re, le, 0);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    wormhole::mutable_buffer buffer(1024 * 1024);

    // receive buffer overflow
    BOOST_REQUIRE_NO_THROW(left.async_write(buffer).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(buffer).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(buffer).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(buffer).get());
    BOOST_REQUIRE_NO_THROW(left.async_write(buffer).get());
    
    BOOST_REQUIRE_THROW(left.async_write(buffer).get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_read(buffer).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(left.close());
    BOOST_REQUIRE_NO_THROW(right.close());

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    auto a = left.async_accept();
    auto c = right.async_connect();

    BOOST_REQUIRE_NO_THROW(a.get());
    BOOST_REQUIRE_NO_THROW(c.get());

    // send buffer overflow
    BOOST_REQUIRE_THROW(left.async_write(wormhole::mutable_buffer(1024 * 1024 * 6)).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(left.close());
    BOOST_REQUIRE_NO_THROW(right.close());
}

BOOST_AUTO_TEST_CASE(tubus_speed)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    auto left = wormhole::tubus::create_channel(wormhole::shared_reactor(), le, re, 0);
    auto right = wormhole::tubus::create_channel(wormhole::shared_reactor(), re, le, 0);

    wormhole::mutable_buffer wb(1024 * 1024);
    wormhole::mutable_buffer rb(1024 * 1024);

    const size_t TRAFFIC = 1024 * 1024 * 1024;

    size_t written = 0;

    std::promise<void> wp;
    std::future<void> wf = wp.get_future();

    wormhole::tubus::io_callback on_write = [&](const boost::system::error_code& err, size_t size)
    {
        if (err)
        {
            wp.set_exception(std::make_exception_ptr(boost::system::system_error(err)));
            return;
        }

        written += size;

        if (written < TRAFFIC)
        {
            auto rest = TRAFFIC - written;
            left->write(wb.size() > rest ? wb.slice(0, rest) : wb, on_write);
        }
        else
        {
            wp.set_value();
        }
    };

    wormhole::tubus::callback on_connect = [&](const boost::system::error_code& err)
    {
        if (err)
        {
            wp.set_exception(std::make_exception_ptr(boost::system::system_error(err)));
            return;
        }

        left->write(wb, on_write);
    };

    size_t read = 0;

    std::promise<void> rp;
    std::future<void> rf = rp.get_future();

    wormhole::tubus::io_callback on_read = [&](const boost::system::error_code& err, size_t size)
    {
        if (err)
        {
            rp.set_exception(std::make_exception_ptr(boost::system::system_error(err)));
            return;
        }

        read += size;

        if (read < TRAFFIC)
        {
            auto rest = TRAFFIC - read;
            right->read(rb.size() > rest ? rb.slice(0, rest) : rb, on_read);
        }
        else
        {
            rp.set_value();
        }
    };

    wormhole::tubus::callback on_accept = [&](const boost::system::error_code& err)
    {
        if (err)
        {
            rp.set_exception(std::make_exception_ptr(boost::system::system_error(err)));
            return;
        }

        right->read(rb, on_read);
    };

    right->open();
    left->open();

    right->accept(on_accept);
    left->connect(on_connect);

    BOOST_TEST_MESSAGE(boost::posix_time::microsec_clock::local_time() << ": begin");

    BOOST_REQUIRE_NO_THROW(wf.get());
    BOOST_REQUIRE_NO_THROW(rf.get());

    BOOST_TEST_MESSAGE(boost::posix_time::microsec_clock::local_time() << ": end");

    right->close();
    left->close();
}

BOOST_AUTO_TEST_CASE(udp_speed)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    auto reactor = wormhole::shared_reactor();

    auto right = std::make_shared<boost::asio::ip::udp::socket>(reactor->io(), re.protocol());

    right->set_option(boost::asio::socket_base::reuse_address(true));
    right->set_option(boost::asio::socket_base::send_buffer_size(1048576));
    right->set_option(boost::asio::socket_base::receive_buffer_size(1048576));
    
    right->bind(re);
    right->connect(le);

    auto left = std::make_shared<boost::asio::ip::udp::socket>(reactor->io(), le.protocol());

    left->set_option(boost::asio::socket_base::send_buffer_size(1048576));
    left->set_option(boost::asio::socket_base::receive_buffer_size(1048576));
    left->set_option(boost::asio::socket_base::reuse_address(true));

    left->bind(le);
    left->connect(re);
    
    BOOST_TEST_MESSAGE(boost::posix_time::microsec_clock::local_time() << ": begin");

    size_t recv = 0;
    size_t sent = 0;

    wormhole::mutable_buffer data(1432);
    wormhole::mutable_buffer ackn(20);
    for (size_t j = 0; j < 15621; ++j)
    {
        for (size_t i = 0; i < 48; ++i)
        {
            sent += left->send(data);
        }

        for (size_t i = 0; i < 48; ++i)
        {
            recv += right->receive(data);
            right->send(ackn);
        }

        for (size_t i = 0; i < 48; ++i)
        {
            left->receive(ackn);
        }
    }

    BOOST_TEST_MESSAGE(boost::posix_time::microsec_clock::local_time() << ": end");
}

BOOST_AUTO_TEST_CASE(tcp_speed)
{
    boost::asio::ip::tcp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::tcp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    auto reactor = wormhole::shared_reactor();

    boost::asio::ip::tcp::socket right(reactor->io());
    boost::asio::ip::tcp::socket left(reactor->io());

    std::promise<void> promise;
    std::future<void> future = promise.get_future();

    boost::asio::ip::tcp::acceptor acceptor(reactor->io(), le);
    acceptor.async_accept(left, [&left, &promise](const boost::system::error_code& error)
    {
        if (error)
        {
            promise.set_exception(std::make_exception_ptr(boost::system::system_error(error)));
            return;
        }

        size_t read = 0;
        wormhole::mutable_buffer rb(1432);

        while (read < 1024 * 1024 * 1024)
        {
            boost::system::error_code error;
            read += left.read_some(rb, error);
        }

        promise.set_value();
    });

    right.open(re.protocol());
    right.bind(re);
    right.connect(le);

    size_t sent = 0;
    wormhole::mutable_buffer wb(1432);

    BOOST_TEST_MESSAGE(boost::posix_time::microsec_clock::local_time() << ": begin");
    while (sent < 1024 * 1024 * 1024)
    {
        boost::system::error_code error;
        sent += right.write_some(wb, error);
    }

    BOOST_REQUIRE_NO_THROW(future.get());
    
    BOOST_TEST_MESSAGE(boost::posix_time::microsec_clock::local_time() << ": end");
}
