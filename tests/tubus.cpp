#include "../buffer.h"
#include "../reactor.h"
#include "../tubus.h"
#include <future>
#include <functional>
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
        , m_channel(novemus::tubus::create_channel(b, p, s))
    {
    }

    void reset()
    {
        m_channel = novemus::tubus::create_channel(m_bind, m_peer, m_secret);
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
        ASYNC(m_channel, shutdown, error && error != boost::asio::error::timed_out && error != boost::asio::error::broken_pipe);
    }

    std::future<void> async_write(const novemus::const_buffer& buffer)
    {
        return std::async([obj = m_channel, buffer]()
        {
            size_t total = 0;
            do {
                std::promise<size_t> promise;
                std::future<size_t> future = promise.get_future();
                obj->write(buffer.slice(total, buffer.size() - total), [&promise](const boost::system::error_code& error, size_t size)
                {
                    if (error)
                        promise.set_exception(std::make_exception_ptr(boost::system::system_error(error)));
                    else
                        promise.set_value(size);
                });
                total += future.get();
            } while (total < buffer.size());
        });
    }

    std::future<void> async_read(const novemus::mutable_buffer& buffer)
    {
        ASYNC_IO(m_channel, read, buffer, error);
    }
};

BOOST_AUTO_TEST_CASE(tubus_core)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_channel left(le, re, 0);
    tubus_channel right(re, le, 0);

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

BOOST_AUTO_TEST_CASE(tubus_integrity)
{
    boost::asio::ip::udp::endpoint pe(boost::asio::ip::address::from_string("127.0.0.1"), 3000);
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    mediator proxy(pe, le, re);

    tubus_channel left(le, pe, 0);
    tubus_channel right(re, pe, 0);

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

    BOOST_REQUIRE_THROW(right.async_read(buffer.slice(0, 1)).get(), boost::system::system_error);

    auto ls = left.async_shutdown();
    auto rs = right.async_shutdown();

    BOOST_REQUIRE_NO_THROW(ls.get());
    BOOST_REQUIRE_NO_THROW(rs.get());
}

BOOST_AUTO_TEST_CASE(tubus_connectivity)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_channel left(le, re, 123456789);
    tubus_channel right(re, le, 123456789);

    BOOST_REQUIRE_THROW(left.async_accept().get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_connect().get(), boost::system::system_error);

    left.reset();
    right.reset();

    auto la = left.async_accept();
    auto rc = right.async_connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    BOOST_REQUIRE_NO_THROW(left.async_shutdown().get());

    BOOST_REQUIRE_THROW(left.async_read(novemus::mutable_buffer(1)).get(), boost::system::system_error);
    BOOST_REQUIRE_THROW(right.async_write(novemus::mutable_buffer(1)).get(), boost::system::system_error);

    BOOST_REQUIRE_NO_THROW(right.async_shutdown().get());
}

BOOST_AUTO_TEST_CASE(tubus_speed)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    auto left = novemus::tubus::create_channel(le, re, 0);
    auto right = novemus::tubus::create_channel(re, le, 0);

    right->accept([=](const boost::system::error_code& error)
    {
        if (error)
        {
            std::cout << boost::posix_time::microsec_clock::local_time() << ": " << error.message() << std::endl;
            return;
        }

        std::cout << boost::posix_time::microsec_clock::local_time() << ": right accept" << std::endl;
        right->read(novemus::mutable_buffer(1024 * 1024 * 100), [=](const boost::system::error_code& error, size_t size)
        {
            if (error)
            {
                std::cout << boost::posix_time::microsec_clock::local_time() << ": " << error.message() << std::endl;
                return;
            }

            std::cout << boost::posix_time::microsec_clock::local_time() << ": right read" << std::endl;
            right->read(novemus::mutable_buffer(1024 * 1024 * 100), [=](const boost::system::error_code& error, size_t size)
            {
                if (error)
                {
                    std::cout << boost::posix_time::microsec_clock::local_time() << ": " << error.message() << std::endl;
                    return;
                }

                std::cout << boost::posix_time::microsec_clock::local_time() << ": right shutdown" << std::endl;
            });
        });
    });

    left->connect([=](const boost::system::error_code& error)
    {
        if (error)
        {
            std::cout << boost::posix_time::microsec_clock::local_time() << ": " << error.message() << std::endl;
            return;
        }

        std::cout << boost::posix_time::microsec_clock::local_time() << ": left connect" << std::endl;
        left->write(novemus::mutable_buffer(1024 * 1024 * 100), [=](const boost::system::error_code& error, size_t size)
        {
            if (error)
            {
                std::cout << boost::posix_time::microsec_clock::local_time() << ": " << error.message() << std::endl;
                return;
            }

            std::cout << boost::posix_time::microsec_clock::local_time() << ": left write" << std::endl;
            left->write(novemus::mutable_buffer(1024 * 1024 * 100), [=](const boost::system::error_code& error, size_t size)
            {
                if (error)
                {
                    std::cout << boost::posix_time::microsec_clock::local_time() << ": " << error.message() << std::endl;
                    return;
                }

                std::cout << boost::posix_time::microsec_clock::local_time() << ": left shutdown" << std::endl;
            });
        });
    });
}

BOOST_AUTO_TEST_CASE(udp_speed)
{
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
    
    std::cout << boost::posix_time::microsec_clock::local_time() << ": start" << std::endl;

    size_t recv = 0;
    size_t sent = 0;

    novemus::mutable_buffer data(9992);
    for (size_t j = 0; j < 1000; ++j)
    {
        for (size_t i = 0; i < 10; ++i)
        {
            sent += left->send(data);
        }

        for (size_t i = 0; i < 10; ++i)
        {
            recv += right->receive(data);
        }
    }

    std::cout << boost::posix_time::microsec_clock::local_time() << ": sent " << sent << std::endl;
    std::cout << boost::posix_time::microsec_clock::local_time() << ": receive " << recv << std::endl;
}
