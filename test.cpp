#define BOOST_TEST_MODULE tubus_tests

#include "udp.h"
#include "tubus.h"
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

BOOST_AUTO_TEST_CASE(udp_connect)
{
    boost::asio::ip::udp::endpoint e1(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint e2(boost::asio::ip::address::from_string("127.0.0.1"), 3002);
    boost::asio::ip::udp::endpoint e3(boost::asio::ip::address::from_string("127.0.0.1"), 3003);

    auto c12 = novemus::udp::connect(e1, e2);
    auto c21 = novemus::udp::connect(e2, e1);

    auto c13 = novemus::udp::connect(e1, e3);
    auto c31 = novemus::udp::connect(e3, e1);

    char buf[1024];

    BOOST_CHECK_EQUAL(c12->send(boost::asio::buffer(e1.data(), e1.size())), e1.size());
    BOOST_CHECK_EQUAL(c21->send(boost::asio::buffer(e2.data(), e2.size())), e2.size());
    BOOST_CHECK_EQUAL(c13->send(boost::asio::buffer(e1.data(), e1.size())), e1.size());
    BOOST_CHECK_EQUAL(c31->send(boost::asio::buffer(e3.data(), e3.size())), e3.size());

    BOOST_CHECK_EQUAL(c12->receive(boost::asio::buffer(buf, sizeof(buf))), e2.size());
    BOOST_CHECK_EQUAL(std::memcmp(buf, e2.data(), e2.size()), 0);

    BOOST_CHECK_EQUAL(c21->receive(boost::asio::buffer(buf, sizeof(buf))), e1.size());
    BOOST_CHECK_EQUAL(std::memcmp(buf, e1.data(), e1.size()), 0);

    BOOST_CHECK_EQUAL(c13->receive(boost::asio::buffer(buf, sizeof(buf))), e3.size());
    BOOST_CHECK_EQUAL(std::memcmp(buf, e3.data(), e3.size()), 0);

    BOOST_CHECK_EQUAL(c31->receive(boost::asio::buffer(buf, sizeof(buf))), e1.size());
    BOOST_CHECK_EQUAL(std::memcmp(buf,e1.data(), e1.size()), 0);

    auto a1 = novemus::udp::accept(e1);
    auto c1 = novemus::udp::connect(e1);

    BOOST_CHECK_EQUAL(a1->send(boost::asio::buffer(a1->local_endpoint().data(), a1->local_endpoint().size())), a1->local_endpoint().size());
    BOOST_CHECK_EQUAL(c1->send(boost::asio::buffer(c1->local_endpoint().data(), c1->local_endpoint().size())), c1->local_endpoint().size());

    BOOST_CHECK_EQUAL(c1->receive(boost::asio::buffer(buf, sizeof(buf))), a1->local_endpoint().size());
    BOOST_CHECK_EQUAL(std::memcmp(buf, a1->local_endpoint().data(), a1->local_endpoint().size()), 0);
    
    BOOST_CHECK_EQUAL(a1->receive(boost::asio::buffer(buf, sizeof(buf))), c1->local_endpoint().size());
    BOOST_CHECK_EQUAL(std::memcmp(buf, c1->local_endpoint().data(), c1->local_endpoint().size()), 0);
}
