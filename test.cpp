#define BOOST_TEST_MODULE tubus_tests

#include "salt.h"
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(buffer)
{
    const char* text = "hello, tubus";

    salt::mutable_buffer mb(std::strlen(text));

    std::memcpy(mb.data(), text, mb.size());

    BOOST_CHECK(mb.unique());
    BOOST_CHECK_EQUAL(mb.size(), std::strlen(text));
    BOOST_CHECK(std::memcmp(mb.data(), text, mb.size()) == 0);

    {
        salt::const_buffer cb = mb.slice(7, 5);

        BOOST_CHECK(!mb.unique());
        BOOST_CHECK(!cb.unique());
        BOOST_CHECK_EQUAL(cb.size(), 5);
        BOOST_CHECK(std::memcmp(cb.data(), "tubus", cb.size()) == 0);

        std::memcpy(mb.data(), "tubus, hello", mb.size());

        BOOST_CHECK(std::memcmp(cb.data(), "hello", cb.size()) == 0);
    }

    mb.shrink(5);
    
    BOOST_CHECK_EQUAL(mb.size(), 5);
    BOOST_CHECK(std::memcmp(mb.data(), "tubus", mb.size()) == 0);

    mb.prune(1);

    BOOST_CHECK_EQUAL(mb.size(), 4);
    BOOST_REQUIRE_THROW(mb.prune(5), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.shrink(5), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.slice(5, 5), std::runtime_error);

    mb.shrink(0);

    BOOST_CHECK_EQUAL(mb.size(), 0);
    BOOST_CHECK(mb.unique());
}