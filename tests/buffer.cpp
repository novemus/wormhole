#define BOOST_TEST_MODULE tubus_tests

#include "../buffer.h"
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(mutable_buffer)
{
    std::cout << "mutable_buffer" << std::endl;

    BOOST_REQUIRE_NO_THROW(novemus::mutable_buffer(0));
    BOOST_CHECK_EQUAL(novemus::mutable_buffer(0).size(), 0);

    const char* greet = "hello, tubus";
    const char* hello = "hello";
    const char* tubus = "tubus";

    novemus::mutable_buffer mb(greet);

    BOOST_CHECK(mb.unique());
    BOOST_CHECK_EQUAL(mb.size(), std::strlen(greet));
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), greet, mb.size()), 0);

    // fill, copy, get, set
    novemus::mutable_buffer sb = mb.slice(7, 5);

    BOOST_CHECK(!mb.unique());
    BOOST_CHECK(!sb.unique());
    BOOST_CHECK_EQUAL(sb.size(), 5);
    BOOST_CHECK_EQUAL(std::memcmp(sb.data(), greet + 7, sb.size()), 0);

    mb.fill(0, strlen(tubus), (const uint8_t*)tubus);
    mb.fill(7, strlen(hello), (const uint8_t*)hello);

    BOOST_CHECK_EQUAL(mb.size(), std::strlen(greet));
    BOOST_CHECK_EQUAL(std::memcmp(mb.data() + 7, hello, strlen(hello)), 0);
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), tubus, strlen(tubus)), 0);

    mb.copy(0, 5, sb.data());

    BOOST_CHECK_EQUAL(std::memcmp(sb.data(), mb.data(), 5), 0);

    mb.set<uint8_t>(0, 9);
    mb.set<uint16_t>(1, 99);
    mb.set<uint32_t>(3, 999);

    BOOST_CHECK_EQUAL(mb.get<uint8_t>(0), 9);
    BOOST_CHECK_EQUAL(mb.get<uint16_t>(1), 99);
    BOOST_CHECK_EQUAL(mb.get<uint32_t>(3), 999);
    
    mb.set<uint64_t>(0, 9999);
    BOOST_CHECK_EQUAL(mb.get<uint64_t>(0), 9999);

    BOOST_REQUIRE_THROW(mb.get<uint8_t>(12), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.get<uint32_t>(10), std::runtime_error);

    BOOST_REQUIRE_THROW(mb.set<uint8_t>(12, 9), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.set<uint32_t>(10, 999), std::runtime_error);

    sb = novemus::mutable_buffer(0);

    // slice
    mb = mb.slice(0, 5);

    BOOST_CHECK_EQUAL(mb.size(), 5);

    mb = mb.slice(1, 4);

    BOOST_CHECK_EQUAL(mb.size(), 4);
    BOOST_REQUIRE_THROW(mb.slice(0, 5), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.slice(5, 1), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.slice(5, 5), std::runtime_error);

    mb = mb.slice(0, 0);

    BOOST_CHECK_EQUAL(mb.size(), 0);
    BOOST_CHECK(mb.unique());

    // pop
    mb = novemus::mutable_buffer(greet);
    auto pb = mb.pop_front(5);

    BOOST_CHECK_EQUAL(mb.size(), strlen(greet) - 5);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet, pb.size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), greet + 5, mb.size()), 0);

    BOOST_CHECK_EQUAL(pb.pop_front(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    pb = mb.pop_back(5);

    BOOST_CHECK_EQUAL(mb.size(), strlen(greet) - 10);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_CHECK_EQUAL(std::memcmp(mb.data(), greet + 5, mb.size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet + 7, pb.size()), 0);

    BOOST_CHECK_EQUAL(pb.pop_back(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.pop_front(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_REQUIRE_THROW(pb.pop_front(pb.size() + 1), std::runtime_error);
    BOOST_REQUIRE_THROW(pb.pop_back(pb.size() + 1), std::runtime_error);

    // truncate, crop
    mb = novemus::mutable_buffer(greet);
    
    pb = mb.pop_back(mb.size());
    pb.truncate(5);

    BOOST_CHECK_EQUAL(pb.size(), 5);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet, pb.size()), 0);

    pb.crop(3);

    BOOST_CHECK_EQUAL(pb.size(), 2);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet + 3, pb.size()), 0);

    BOOST_REQUIRE_THROW(mb.truncate(mb.size() + 1), std::runtime_error);
    BOOST_REQUIRE_THROW(mb.crop(mb.size() + 1), std::runtime_error);

    mb.truncate(mb.size());
    pb.crop(pb.size());

    BOOST_CHECK_EQUAL(mb.size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 0);

    BOOST_CHECK(!mb.unique());
    BOOST_CHECK(!pb.unique());
}

BOOST_AUTO_TEST_CASE(const_buffer)
{
    std::cout << "const_buffer" << std::endl;

    BOOST_REQUIRE_NO_THROW(novemus::mutable_buffer(""));
    BOOST_CHECK_EQUAL(novemus::mutable_buffer("").size(), 0);

    const char* greet = "hello, tubus";

    novemus::const_buffer cb(greet);

    BOOST_CHECK(cb.unique());
    BOOST_CHECK_EQUAL(cb.size(), std::strlen(greet));
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), greet, cb.size()), 0);

    // copy, get
    novemus::const_buffer sb = cb.slice(4, 8);

    BOOST_CHECK(!cb.unique());
    BOOST_CHECK(!sb.unique());
    BOOST_CHECK_EQUAL(sb.size(), 8);

    BOOST_CHECK_EQUAL(cb.get<uint8_t>(4), sb.get<uint8_t>(0));
    BOOST_CHECK_EQUAL(cb.get<uint16_t>(4), sb.get<uint16_t>(0));
    BOOST_CHECK_EQUAL(cb.get<uint32_t>(4), sb.get<uint32_t>(0));
    BOOST_CHECK_EQUAL(cb.get<uint64_t>(4), sb.get<uint64_t>(0));

    uint8_t copy[5];
    cb.copy(0, 5, copy);

    BOOST_CHECK_EQUAL(std::memcmp(copy, cb.data(), 5), 0);

    BOOST_REQUIRE_THROW(cb.get<uint8_t>(12), std::runtime_error);
    BOOST_REQUIRE_THROW(cb.get<uint32_t>(10), std::runtime_error);

    sb = novemus::const_buffer("");

    // slice
    cb = cb.slice(0, 5);

    BOOST_CHECK_EQUAL(cb.size(), 5);

    cb = cb.slice(1, 4);

    BOOST_CHECK_EQUAL(cb.size(), 4);
    BOOST_REQUIRE_THROW(cb.slice(0, 5), std::runtime_error);
    BOOST_REQUIRE_THROW(cb.slice(5, 1), std::runtime_error);
    BOOST_REQUIRE_THROW(cb.slice(5, 5), std::runtime_error);

    cb = cb.slice(0, 0);

    BOOST_CHECK_EQUAL(cb.size(), 0);
    BOOST_CHECK(cb.unique());

    // pop
    cb = novemus::const_buffer(greet);
    auto pb = cb.pop_front(5);

    BOOST_CHECK_EQUAL(cb.size(), strlen(greet) - 5);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet, pb.size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), greet + 5, cb.size()), 0);

    BOOST_CHECK_EQUAL(pb.pop_front(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    pb = cb.pop_back(5);

    BOOST_CHECK_EQUAL(cb.size(), strlen(greet) - 10);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), greet + 5, cb.size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet + 7, pb.size()), 0);

    BOOST_CHECK_EQUAL(pb.pop_back(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.pop_front(0).size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 5);

    BOOST_REQUIRE_THROW(pb.pop_front(pb.size() + 1), std::runtime_error);
    BOOST_REQUIRE_THROW(pb.pop_back(pb.size() + 1), std::runtime_error);

    // truncate, crop
    cb = novemus::const_buffer(greet);
    
    pb = cb.pop_back(cb.size());
    pb.truncate(5);

    BOOST_CHECK_EQUAL(pb.size(), 5);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet, pb.size()), 0);

    pb.crop(3);

    BOOST_CHECK_EQUAL(pb.size(), 2);
    BOOST_CHECK_EQUAL(std::memcmp(pb.data(), greet + 3, pb.size()), 0);

    BOOST_REQUIRE_THROW(cb.truncate(cb.size() + 1), std::runtime_error);
    BOOST_REQUIRE_THROW(cb.crop(cb.size() + 1), std::runtime_error);

    cb.truncate(cb.size());
    pb.crop(pb.size());

    BOOST_CHECK_EQUAL(cb.size(), 0);
    BOOST_CHECK_EQUAL(pb.size(), 0);

    BOOST_CHECK(!cb.unique());
    BOOST_CHECK(!pb.unique());
}

BOOST_AUTO_TEST_CASE(buffer_factory)
{
    std::cout << "buffer_factory" << std::endl;

    auto factory = novemus::buffer_factory::shared_factory();

    {
        auto buf1 = factory->obtain(16384);
        auto buf2 = factory->obtain(8192);
        auto buf3 = factory->obtain(4096);

        BOOST_CHECK(buf1.data() != buf2.data() && buf2.data() != buf3.data() && buf1.data() != buf3.data());
    }

    auto buf1 = factory->obtain(512);
    auto buf2 = factory->obtain(1024);
    auto buf3 = factory->obtain(2048);

    BOOST_CHECK(buf1.data() != buf2.data() && buf2.data() != buf3.data() && buf1.data() != buf3.data());
}