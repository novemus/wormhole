#define BOOST_TEST_MODULE tubus_tests

#include "tubus.h"
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(buffer)
{
    const char* text = "hello, tubus";

    tubus::mutable_buffer mb(std::strlen(text));

    std::memcpy(mb.data(), text, mb.size());

    BOOST_CHECK(mb.unique());
    BOOST_CHECK_EQUAL(mb.size(), std::strlen(text));
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), text, mb.size()), 0);

    {
        tubus::const_buffer cb = mb.slice(7, 5);

        BOOST_CHECK(!mb.unique());
        BOOST_CHECK(!cb.unique());
        BOOST_CHECK_EQUAL(cb.size(), 5);
        BOOST_CHECK_EQUAL(std::memcmp(cb.data(), "tubus", cb.size()), 0);

        std::memcpy(mb.data(), "tubus, hello", mb.size());

        BOOST_CHECK_EQUAL(std::memcmp(cb.data(), "hello", cb.size()), 0);
    }

    mb.shrink(5);
    
    BOOST_CHECK_EQUAL(mb.size(), 5);
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), "tubus", mb.size()), 0);

    mb.prune(1);

    BOOST_CHECK_EQUAL(mb.size(), 4);
    BOOST_REQUIRE_THROW(mb.prune(5), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.shrink(5), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.slice(5, 5), std::runtime_error);

    mb.shrink(0);

    BOOST_CHECK_EQUAL(mb.size(), 0);
    BOOST_CHECK(mb.unique());
}

BOOST_AUTO_TEST_CASE(channel)
{
    boost::asio::ip::udp::endpoint s(boost::asio::ip::address::from_string("127.0.0.1"), 3000);
    boost::asio::ip::udp::endpoint c1(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint c2(boost::asio::ip::address::from_string("127.0.0.1"), 3002);
    boost::asio::ip::udp::endpoint c3(boost::asio::ip::address::from_string("127.0.0.1"), 3003);
};