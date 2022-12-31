#define BOOST_TEST_MODULE tubus_tests

#include "buffer.h"
#include "tubus.h"
#include <future>
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(buffer)
{
    const char* text = "hello, tubus";

    novemus::mutable_buffer mb(std::strlen(text));

    std::memcpy(mb.data(), text, mb.size());

    BOOST_CHECK(mb.unique());
    BOOST_CHECK_EQUAL(mb.size(), std::strlen(text));
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), text, mb.size()), 0);

    {
        novemus::const_buffer cb = mb.slice(7, 5);

        BOOST_CHECK(!mb.unique());
        BOOST_CHECK(!cb.unique());
        BOOST_CHECK_EQUAL(cb.size(), 5);
        BOOST_CHECK_EQUAL(std::memcmp(cb.data(), "tubus", cb.size()), 0);

        std::memcpy(mb.data(), "tubus, hello", mb.size());

        BOOST_CHECK_EQUAL(std::memcmp(cb.data(), "hello", cb.size()), 0);
    }

    mb = mb.slice(0, 5);
    
    BOOST_CHECK_EQUAL(mb.size(), 5);
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), "tubus", mb.size()), 0);

    mb = mb.slice(1, 4);

    BOOST_CHECK_EQUAL(mb.size(), 4);
    BOOST_REQUIRE_THROW(mb.slice(0, 5), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.slice(5, 1), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.slice(5, 5), std::runtime_error);

    mb = mb.slice(0, 0);

    BOOST_CHECK_EQUAL(mb.size(), 0);
    BOOST_CHECK(mb.unique());
}

class tubus_channel
{
#define HANDLER(FILTER) [&promise](const boost::system::error_code& error) \
{ \
    if (FILTER) \
        promise.set_exception(std::make_exception_ptr(boost::system::system_error(error))); \
    else \
        promise.set_value(); \
} \

#define ACCEPT m_channel->accept(HANDLER(error))
#define CONNECT m_channel->connect(HANDLER(error))
#define SHUTDOWN m_channel->shutdown(HANDLER(error && error != boost::asio::error::timed_out))
#define READ m_channel->read(buffer, HANDLER(error))
#define WRITE m_channel->write(buffer, HANDLER(error))

#define EXECUTE(TASK) return std::async([=]() { \
    std::promise<void> promise; \
    std::future<void> future = promise.get_future(); \
    TASK; \
    if (future.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) \
        throw boost::system::system_error(boost::asio::error::timed_out); \
    return future.get(); \
}) \

    std::shared_ptr<novemus::tubus::channel> m_channel;

public:

    tubus_channel(const boost::asio::ip::udp::endpoint& b, const boost::asio::ip::udp::endpoint& p, uint64_t s)
        : m_channel(novemus::tubus::create_channel(b, p, s))
    {
    }

    std::future<void> accept()
    {
        EXECUTE(ACCEPT);
    }

    std::future<void> connect()
    {
        EXECUTE(CONNECT);
    }

    std::future<void> shutdown()
    {
        EXECUTE(SHUTDOWN);
    }

    std::future<void> write(const novemus::const_buffer& buffer)
    {
        EXECUTE(WRITE);
    }

    std::future<void> read(const novemus::mutable_buffer& buffer)
    {
        EXECUTE(READ);
    }
};

BOOST_AUTO_TEST_CASE(tubus_core)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    tubus_channel left(le, re, 123456789);
    tubus_channel right(re, le, 123456789);

    uint8_t data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    novemus::mutable_buffer lb(sizeof(data));
    novemus::mutable_buffer rb(sizeof(data));

    std::memcpy(lb.data(), data, lb.size());
    std::memcpy(rb.data(), data, rb.size());

    auto la = left.accept();
    auto rc = right.connect();

    BOOST_REQUIRE_NO_THROW(la.get());
    BOOST_REQUIRE_NO_THROW(rc.get());

    for(size_t i = 0; i < sizeof(data); ++i)
    {
        BOOST_REQUIRE_NO_THROW(left.write(lb.slice(i, 1)).get());
        BOOST_REQUIRE_NO_THROW(right.write(rb.slice(i, 1)).get());
    }

    BOOST_REQUIRE_NO_THROW(left.read(lb).get());
    BOOST_CHECK_EQUAL(std::memcmp(lb.data(), data, lb.size()), 0);

    BOOST_REQUIRE_NO_THROW(right.read(lb).get());
    BOOST_CHECK_EQUAL(std::memcmp(rb.data(), data, rb.size()), 0);

    auto ls = left.shutdown();
    auto rs = right.shutdown();

    BOOST_REQUIRE_NO_THROW(ls.get());
    BOOST_REQUIRE_NO_THROW(rs.get());
}

BOOST_AUTO_TEST_CASE(tubus_mask)
{

}