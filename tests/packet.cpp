#include "../packet.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(cursor)
{
    std::cout << "cursor" << std::endl;

    novemus::tubus::cursor handle(novemus::const_buffer("123"));
    BOOST_CHECK(!handle.valid());

    novemus::mutable_buffer mb(8);
    std::memset(mb.data(), 0, mb.size());

    handle = novemus::tubus::cursor(mb);

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 0);

    handle = novemus::tubus::cursor(1234567890);

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1234567890);

    handle = novemus::tubus::cursor(handle.unite());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1234567890);
}

BOOST_AUTO_TEST_CASE(multi_snippet)
{
    std::cout << "multi_snippet" << std::endl;

    novemus::tubus::snippet snip(novemus::const_buffer("123"));
    BOOST_CHECK(!snip.valid());

    novemus::const_buffer cb("hello, tubus");
    snip = novemus::tubus::snippet(1024, cb);

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.at(1).size(), cb.size());
    BOOST_CHECK_EQUAL(snip.fragment().data(), cb.data());
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());

    snip = novemus::tubus::snippet(snip.unite());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);
}

BOOST_AUTO_TEST_CASE(multi_section)
{
    std::cout << "multi_section" << std::endl;

    novemus::const_buffer cb("hello, tubus");
    novemus::tubus::snippet snip(1024, cb);

    novemus::tubus::section sect(snip);

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::tubus::section::header_size);

    snip = novemus::tubus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(snip.fragment().data(), cb.data());

    sect = novemus::tubus::section(sect.unite());

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::tubus::section::header_size);

    snip = novemus::tubus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    novemus::tubus::cursor handle(1024);
    sect = novemus::tubus::section(handle);

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data | novemus::tubus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::tubus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::cursor::cursor_size + novemus::tubus::section::header_size);

    handle = novemus::tubus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = novemus::tubus::section(sect.unite());

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data | novemus::tubus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::tubus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::cursor::cursor_size + novemus::tubus::section::header_size);

    handle = novemus::tubus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = novemus::tubus::section(novemus::tubus::section::link);

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);

    sect = novemus::tubus::section(sect.unite());

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
}

BOOST_AUTO_TEST_CASE(multi_payload)
{
    std::cout << "multi_payload" << std::endl;

    novemus::tubus::payload data;
    novemus::const_buffer cb("hello, tubus");

    data.push_back(novemus::tubus::section(novemus::tubus::snippet(1024, cb)));
    data.push_back(novemus::tubus::section(novemus::tubus::cursor(1024)));
    data.push_back(novemus::tubus::section(novemus::tubus::section::link));

    auto sect = data.advance();
    
    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), cb.size() + novemus::tubus::snippet::header_size);
    BOOST_CHECK_EQUAL(sect.size(), cb.size() + novemus::tubus::snippet::header_size + novemus::tubus::section::header_size);

    auto snip = novemus::tubus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(snip.fragment().data(), cb.data());

    sect = data.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data | novemus::tubus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::tubus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::cursor::cursor_size + novemus::tubus::section::header_size);

    auto handle = novemus::tubus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = data.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::section::header_size);

    sect = data.advance();

    BOOST_CHECK(!sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 0);
    BOOST_CHECK_EQUAL(sect.size(), 0);
}

BOOST_AUTO_TEST_CASE(multi_packet)
{
    novemus::tubus::packet packet(12345);

    BOOST_CHECK(packet.valid());
    BOOST_CHECK_EQUAL(packet.count(), 1);
    BOOST_CHECK_EQUAL(packet.size(), novemus::tubus::packet::header_size);
    BOOST_CHECK_EQUAL(packet.salt(), 0);
    BOOST_CHECK_EQUAL(packet.sign(), novemus::tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(packet.version(), novemus::tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(packet.pin(), 12345);
    BOOST_CHECK_EQUAL(packet.data().count(), 0);

    novemus::const_buffer cb("hello, tubus");

    packet.push_back(novemus::tubus::section(novemus::tubus::snippet(1024, cb)));
    packet.push_back(novemus::tubus::section(novemus::tubus::cursor(1024)));
    packet.push_back(novemus::tubus::section(novemus::tubus::section::link));

    BOOST_CHECK_EQUAL(packet.count(), 7);

    auto buffer = packet.unite();

    packet = novemus::tubus::packet(buffer);

    BOOST_CHECK(packet.valid());
    BOOST_CHECK_EQUAL(packet.count(), 7);
    BOOST_CHECK_EQUAL(packet.size(), buffer.size());
    BOOST_CHECK_EQUAL(packet.salt(), 0);
    BOOST_CHECK_EQUAL(packet.sign(), novemus::tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(packet.version(), novemus::tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(packet.pin(), 12345);

    auto payload = packet.data();
    auto sect = payload.advance();
    
    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), cb.size() + novemus::tubus::snippet::header_size);
    BOOST_CHECK_EQUAL(sect.size(), cb.size() + novemus::tubus::snippet::header_size + novemus::tubus::section::header_size);

    auto snip = novemus::tubus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect = payload.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data | novemus::tubus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::tubus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::cursor::cursor_size + novemus::tubus::section::header_size);

    auto handle = novemus::tubus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = payload.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::section::header_size);

    sect = payload.advance();

    BOOST_CHECK(!sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 0);
    BOOST_CHECK_EQUAL(sect.size(), 0);
}

BOOST_AUTO_TEST_CASE(dimmer)
{
    novemus::tubus::packet packet(12345);

    BOOST_CHECK(packet.valid());
    BOOST_CHECK_EQUAL(packet.count(), 1);
    BOOST_CHECK_EQUAL(packet.size(), novemus::tubus::packet::header_size);
    BOOST_CHECK_EQUAL(packet.salt(), 0);
    BOOST_CHECK_EQUAL(packet.sign(), novemus::tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(packet.version(), novemus::tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(packet.pin(), 12345);
    BOOST_CHECK_EQUAL(packet.data().count(), 0);

    novemus::const_buffer cb("hello, tubus");

    packet.push_back(novemus::tubus::section(novemus::tubus::snippet(1024, cb)));
    packet.push_back(novemus::tubus::section(novemus::tubus::cursor(1024)));
    packet.push_back(novemus::tubus::section(novemus::tubus::section::link));

    BOOST_CHECK_EQUAL(packet.count(), 7);

    auto buffer = novemus::tubus::dimmer::invert(1234567890, packet.unite());
    BOOST_CHECK_EQUAL(buffer.size(), packet.size());

    buffer = novemus::tubus::dimmer::invert(1234567890, buffer);
    BOOST_CHECK_EQUAL(buffer.size(), packet.size());

    packet = novemus::tubus::packet(buffer);

    BOOST_CHECK(packet.valid());
    BOOST_CHECK_EQUAL(packet.count(), 7);
    BOOST_CHECK_EQUAL(packet.size(), buffer.size());
    BOOST_CHECK_EQUAL(packet.salt(), 0);
    BOOST_CHECK_EQUAL(packet.sign(), novemus::tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(packet.version(), novemus::tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(packet.pin(), 12345);

    auto payload = packet.data();
    auto sect = payload.advance();
    
    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), cb.size() + novemus::tubus::snippet::header_size);
    BOOST_CHECK_EQUAL(sect.size(), cb.size() + novemus::tubus::snippet::header_size + novemus::tubus::section::header_size);

    auto snip = novemus::tubus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect = payload.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data | novemus::tubus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::tubus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::cursor::cursor_size + novemus::tubus::section::header_size);

    auto handle = novemus::tubus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = payload.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::section::header_size);

    sect = payload.advance();

    BOOST_CHECK(!sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 0);
    BOOST_CHECK_EQUAL(sect.size(), 0);
}
