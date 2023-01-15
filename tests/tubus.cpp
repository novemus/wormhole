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
    size_t total = 0; \
    do { \
        std::promise<size_t> promise; \
        std::future<size_t> future = promise.get_future(); \
        obj->method(buffer.slice(total, buffer.size() - total), [&promise](const boost::system::error_code& error, size_t size) \
        { \
            if (filter) \
                promise.set_exception(std::make_exception_ptr(boost::system::system_error(error))); \
            else \
                promise.set_value(size); \
        }); \
        total += future.get(); \
    } while (total < buffer.size()); \
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
    std::shared_ptr<novemus::tubus::channel> m_channel;

public:

    tubus_channel(const boost::asio::ip::udp::endpoint& b, const boost::asio::ip::udp::endpoint& p, uint64_t s)
        : m_bind(b)
        , m_peer(p)
        , m_secret(s)
    {
    }

    void open()
    {
        m_channel = novemus::tubus::create_channel(m_bind, m_peer, m_secret);
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
        ASYNC(m_channel, shutdown, error && error != boost::asio::error::broken_pipe && error != boost::asio::error::connection_refused);
    }

    std::future<void> async_write(const novemus::const_buffer& buffer)
    {
        ASYNC_IO(m_channel, write, buffer, error);
    }

    std::future<void> async_read(const novemus::mutable_buffer& buffer)
    {
        ASYNC_IO(m_channel, read, buffer, error);
    }
};

class mediator
{
    std::shared_ptr<novemus::reactor> m_reactor;
    boost::asio::ip::udp::endpoint m_le;
    boost::asio::ip::udp::endpoint m_re;
    boost::asio::ip::udp::socket m_bs;
    boost::asio::ip::udp::endpoint m_ep;
    novemus::mutable_buffer m_rb;

    void on_sent(const boost::system::error_code& e, size_t s)
    {
        if (e)
        {
            if (e != boost::asio::error::operation_aborted)
                std::cout << "mediator: " << e.message() << std::endl;
            return;
        }

        receive();
    }

    void on_received(const boost::system::error_code& e, size_t s)
    {
        if(e)
        {
            if (e != boost::asio::error::operation_aborted)
                std::cout << "mediator: " << e.message() << std::endl;
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
        : m_reactor(novemus::reactor::shared_reactor())
        , m_le(l)
        , m_re(r)
        , m_bs(m_reactor->io(), b.protocol())
        , m_rb(10000)
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

    novemus::const_buffer read_some()
    {
        novemus::mutable_buffer chank(1024 * 1024);

        uint8_t* ptr = chank.data();
        uint8_t* end = chank.data() + chank.size();

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

    void write_some(const novemus::const_buffer& chank)
    {
        const uint8_t* ptr = chank.data();
        const uint8_t* end = chank.data() + chank.size();

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
    std::cout << "tubus_core" << std::endl;

    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_channel left(le, re, 1234567890);
    tubus_channel right(re, le, 1234567890);

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    uint8_t data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    novemus::mutable_buffer lb(sizeof(data));
    novemus::mutable_buffer rb(sizeof(data));

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
    std::cout << "tubus_connectivity" << std::endl;

    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_channel left(le, re, 1234567890);
    BOOST_REQUIRE_NO_THROW(left.open());

    tubus_channel right(re, le, 1234567890);
    BOOST_REQUIRE_NO_THROW(right.open());

    auto la = left.async_accept();
    BOOST_CHECK_EQUAL((int)la.wait_for(std::chrono::seconds(3)), (int)std::future_status::timeout);
    BOOST_REQUIRE_NO_THROW(left.async_shutdown().get());

    auto rc = right.async_connect();
    BOOST_REQUIRE_THROW(rc.get(), boost::system::system_error);
    BOOST_REQUIRE_NO_THROW(right.async_shutdown().get());
    
    BOOST_REQUIRE_NO_THROW(left.close());
    BOOST_REQUIRE_NO_THROW(right.close());

    BOOST_REQUIRE_NO_THROW(left.open());
    BOOST_REQUIRE_NO_THROW(right.open());

    auto a = left.async_accept();
    auto c = right.async_connect();

    BOOST_REQUIRE_NO_THROW(a.get());
    BOOST_REQUIRE_NO_THROW(c.get());

    BOOST_REQUIRE_NO_THROW(left.async_shutdown().get());

    BOOST_REQUIRE_THROW(left.async_read(novemus::mutable_buffer(1)).get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_write(novemus::mutable_buffer(1)).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(right.async_shutdown().get());
}

BOOST_AUTO_TEST_CASE(tubus_integrity)
{
    std::cout << "tubus_integrity" << std::endl;

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

    novemus::mutable_buffer buffer(source.read() / 4);

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
    std::cout << "tubus_fall" << std::endl;

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

    novemus::mutable_buffer buffer(1024 * 1024);

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
    BOOST_REQUIRE_THROW(left.async_write(novemus::mutable_buffer(1024 * 1024 * 6)).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(left.close());
    BOOST_REQUIRE_NO_THROW(right.close());
}

BOOST_AUTO_TEST_CASE(tubus_speed)
{
    std::cout << "tubus_speed" << std::endl;

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

    novemus::mutable_buffer wb(1024 * 1024);
    novemus::mutable_buffer rb(1024 * 1024);

    std::cout << boost::posix_time::microsec_clock::local_time() << ": begin" << std::endl;

    for (size_t i = 0; i < 1024; ++i)
    {
        auto lw = left.async_write(wb);
        auto rr = right.async_read(rb);
        BOOST_REQUIRE_NO_THROW(lw.get());
        BOOST_REQUIRE_NO_THROW(rr.get());
    }

    std::cout << boost::posix_time::microsec_clock::local_time() << ": end" << std::endl;

    auto ls = left.async_shutdown();
    auto rs = right.async_shutdown();

    BOOST_REQUIRE_NO_THROW(ls.get());
    BOOST_REQUIRE_NO_THROW(rs.get());
}

BOOST_AUTO_TEST_CASE(udp_speed)
{
    std::cout << "udp_speed" << std::endl;

    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    auto reactor = novemus::reactor::shared_reactor();

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
    
    std::cout << boost::posix_time::microsec_clock::local_time() << ": begin" << std::endl;

    size_t recv = 0;
    size_t sent = 0;

    novemus::mutable_buffer data(65507);
    novemus::mutable_buffer ackn(20);
    for (size_t j = 0; j < 3278; ++j)
    {
        for (size_t i = 0; i < 5; ++i)
        {
            sent += left->send(data);
        }

        for (size_t i = 0; i < 5; ++i)
        {
            recv += right->receive(data);
            right->send(ackn);
        }

        for (size_t i = 0; i < 5; ++i)
        {
            left->receive(ackn);
        }
    }

    std::cout << boost::posix_time::microsec_clock::local_time() << ": sent " << sent << std::endl;
    std::cout << boost::posix_time::microsec_clock::local_time() << ": receive " << recv << std::endl;
}

BOOST_AUTO_TEST_CASE(tcp_speed)
{
    std::cout << "tcp_speed" << std::endl;

    boost::asio::ip::tcp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::tcp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    auto reactor = novemus::reactor::shared_reactor();

    boost::asio::ip::tcp::socket right(reactor->io());
    boost::asio::ip::tcp::socket left(reactor->io());

    boost::asio::ip::tcp::acceptor acceptor(reactor->io(), le);
    acceptor.async_accept(left, [&left](const boost::system::error_code& error)
    {
        std::cout << error.message() << std::endl;

        if (error)
            return;

        size_t read = 0;
        novemus::mutable_buffer rb(1024 * 1024);

        std::cout << boost::posix_time::microsec_clock::local_time() << ": begin read" << std::endl;
        while (read < 1024 * 1024 * 1024)
        {
            boost::system::error_code error;
            read += left.read_some(rb, error);
        }

        std::cout << boost::posix_time::microsec_clock::local_time() << ": read " << read << std::endl;
    });

    right.open(re.protocol());
    right.bind(re);
    right.connect(le);

    size_t sent = 0;
    novemus::mutable_buffer wb(1024 * 1024);

    std::cout << boost::posix_time::microsec_clock::local_time() << ": begin send" << std::endl;
    while (sent < 1024 * 1024 * 1024)
    {
        boost::system::error_code error;
        sent += right.write_some(wb, error);
    }

    std::cout << boost::posix_time::microsec_clock::local_time() << ": sent " << sent << std::endl;
    std::cin.get();
}
