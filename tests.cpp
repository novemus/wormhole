#define BOOST_TEST_MODULE tubus_tests

#include "buffer.h"
#include "udp.h"
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

BOOST_AUTO_TEST_CASE(udp)
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

BOOST_AUTO_TEST_CASE(tubus)
{
    boost::asio::ip::udp::endpoint le(boost::asio::ip::address::from_string("127.0.0.1"), 3001);
    boost::asio::ip::udp::endpoint re(boost::asio::ip::address::from_string("127.0.0.1"), 3002);

    auto left = novemus::tubus::create_channel(le, re);
    auto right = novemus::tubus::create_channel(re, le);

    std::promise<boost::system::error_code> lap;
    left->accept([&lap](const boost::system::error_code& error)
    {
        lap.set_value(error);
    });

    std::promise<boost::system::error_code> rcp;
    right->connect([&rcp](const boost::system::error_code& error)
    {
        rcp.set_value(error);
    });

    BOOST_CHECK_EQUAL(lap.get_future().get(), boost::system::error_code());
    BOOST_CHECK_EQUAL(rcp.get_future().get(), boost::system::error_code());

    novemus::mutable_buffer lb(10);
    novemus::mutable_buffer rb(10);

    std::memcpy(lb.data(), "1234567890", lb.size());
    std::memcpy(rb.data(), "1234567890", rb.size());

    std::vector<std::promise<boost::system::error_code>> lwps(lb.size());
    for(size_t i = 0; i < lb.size(); ++i)
    {
        left->write(lb.slice(i, 1), [i, &lwps](const boost::system::error_code& error, size_t)
        {
            lwps[i].set_value(error);
        });
    }

    std::vector<std::promise<boost::system::error_code>> rwps(rb.size());
    for(size_t i = 0; i < rb.size(); ++i)
    {
        right->write(rb.slice(i, 1), [i, &rwps](const boost::system::error_code& error, size_t)
        {
            rwps[i].set_value(error);
        });
    }

    for(auto& p : lwps)
    {
        BOOST_CHECK_EQUAL(p.get_future().get(), boost::system::error_code());
    }

    for(auto& p : rwps)
    {
        BOOST_CHECK_EQUAL(p.get_future().get(), boost::system::error_code());
    }

    std::promise<boost::system::error_code> lsp;
    left->shutdown([&lsp](const boost::system::error_code& error)
    {
        lsp.set_value(error);
    });

    std::promise<boost::system::error_code> rsp;
    right->shutdown([&rsp](const boost::system::error_code& error)
    {
        rsp.set_value(error);
    });

    BOOST_CHECK_EQUAL(lsp.get_future().get(), boost::system::error_code());
    BOOST_CHECK_EQUAL(rsp.get_future().get(), boost::system::error_code());
}