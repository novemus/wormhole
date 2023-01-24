#include "../multibuffer.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(multi_buffer)
{
    std::cout << "multi_buffer" << std::endl;

    novemus::const_buffer hello("hello");
    novemus::const_buffer multibuffer("multibuffer");
    
    novemus::multibuffer<novemus::const_buffer> mb1;

    mb1.push_back(hello);
    mb1.push_back(multibuffer);

    BOOST_CHECK_EQUAL(mb1.count(), 2);
    BOOST_CHECK_EQUAL(mb1.size(), 16);

    BOOST_CHECK_EQUAL(mb1.at(0).data(), hello.data());
    BOOST_CHECK_EQUAL(mb1.at(1).data(), multibuffer.data());

    auto merge = mb1.unite();
    BOOST_CHECK_EQUAL(std::memcmp(merge.data(), "hellomultibuffer", merge.size()), 0);

    mb1.count(1);
    mb1.push_front(multibuffer);

    BOOST_CHECK_EQUAL(mb1.count(), 2);
    BOOST_CHECK_EQUAL(mb1.size(), 16);

    BOOST_CHECK_EQUAL(mb1.at(0).data(), multibuffer.data());
    BOOST_CHECK_EQUAL(mb1.at(1).data(), hello.data());

    novemus::multibuffer<novemus::const_buffer> mb2(mb1);

    mb2.push_front(hello);
    mb2.push_back(multibuffer);
    mb2.push_back(mb1);

    BOOST_CHECK_EQUAL(mb2.count(), 6);
    BOOST_CHECK_EQUAL(mb2.size(), 48);

    BOOST_CHECK_EQUAL(mb2.at(0).data(), hello.data());
    BOOST_CHECK_EQUAL(mb2.at(1).data(), multibuffer.data());
    BOOST_CHECK_EQUAL(mb2.at(2).data(), hello.data());
    BOOST_CHECK_EQUAL(mb2.at(3).data(), multibuffer.data());
    BOOST_CHECK_EQUAL(mb2.at(4).data(), multibuffer.data());
    BOOST_CHECK_EQUAL(mb2.at(5).data(), hello.data());

    mb2.count(4);
    mb2.pop_front();
    mb2.pop_back();

    BOOST_CHECK_EQUAL(mb2.count(), 2);
    BOOST_CHECK_EQUAL(mb2.size(), 16);

    BOOST_CHECK_EQUAL(mb2.at(0).data(), multibuffer.data());
    BOOST_CHECK_EQUAL(mb2.at(1).data(), hello.data());
}

BOOST_AUTO_TEST_CASE(multi_cursor)
{
    std::cout << "multi_cursor" << std::endl;

    novemus::cursor handle(novemus::const_buffer("123"));
    BOOST_CHECK(!handle.valid());

    novemus::mutable_buffer mb(8);
    std::memset(mb.data(), 0, mb.size());

    handle = novemus::cursor(mb);

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 0);

    handle = novemus::cursor(1234567890);

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1234567890);

    handle = novemus::cursor(handle.unite());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1234567890);
}

BOOST_AUTO_TEST_CASE(multi_snippet)
{
    std::cout << "multi_snippet" << std::endl;

    novemus::snippet snip(novemus::const_buffer("123"));
    BOOST_CHECK(!snip.valid());

    novemus::const_buffer cb("hello, tubus");
    snip = novemus::snippet(1024, cb);

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.at(1).size(), cb.size());
    BOOST_CHECK_EQUAL(snip.fragment().data(), cb.data());
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());

    snip = novemus::snippet(snip.unite());

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
    novemus::snippet snip(1024, cb);

    novemus::section sect(snip);

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::section::header_size);

    snip = novemus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(snip.fragment().data(), cb.data());

    sect = novemus::section(sect.unite());

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::section::header_size);

    snip = novemus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    novemus::cursor handle(1024);
    sect = novemus::section(handle);

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data | novemus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::cursor::cursor_size + novemus::section::header_size);

    handle = novemus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = novemus::section(sect.unite());

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data | novemus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::cursor::cursor_size + novemus::section::header_size);

    handle = novemus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = novemus::section(novemus::section::link);

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);

    sect = novemus::section(sect.unite());

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
}

BOOST_AUTO_TEST_CASE(multi_payload)
{
    std::cout << "multi_payload" << std::endl;

    novemus::payload data;
    novemus::const_buffer cb("hello, tubus");

    data.push_back(novemus::section(novemus::snippet(1024, cb)));
    data.push_back(novemus::section(novemus::cursor(1024)));
    data.push_back(novemus::section(novemus::section::link));

    auto sect = data.advance();
    
    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), cb.size() + novemus::snippet::header_size);
    BOOST_CHECK_EQUAL(sect.size(), cb.size() + novemus::snippet::header_size + novemus::section::header_size);

    auto snip = novemus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(snip.fragment().data(), cb.data());

    sect = data.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data | novemus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::cursor::cursor_size + novemus::section::header_size);

    auto handle = novemus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = data.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), novemus::section::header_size);

    sect = data.advance();

    BOOST_CHECK(!sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 0);
    BOOST_CHECK_EQUAL(sect.size(), 0);
}

BOOST_AUTO_TEST_CASE(multi_packet)
{
    novemus::packet packet(12345);

    BOOST_CHECK(packet.valid());
    BOOST_CHECK_EQUAL(packet.count(), 1);
    BOOST_CHECK_EQUAL(packet.size(), novemus::packet::header_size);
    BOOST_CHECK_EQUAL(packet.salt(), 0);
    BOOST_CHECK_EQUAL(packet.sign(), novemus::packet::packet_sign);
    BOOST_CHECK_EQUAL(packet.version(), novemus::packet::packet_version);
    BOOST_CHECK_EQUAL(packet.pin(), 12345);
    BOOST_CHECK_EQUAL(packet.data().count(), 0);

    novemus::const_buffer cb("hello, tubus");

    packet.push_back(novemus::section(novemus::snippet(1024, cb)));
    packet.push_back(novemus::section(novemus::cursor(1024)));
    packet.push_back(novemus::section(novemus::section::link));

    BOOST_CHECK_EQUAL(packet.count(), 7);

    auto buffer = packet.unite();

    packet = novemus::packet(buffer);

    BOOST_CHECK(packet.valid());
    BOOST_CHECK_EQUAL(packet.count(), 7);
    BOOST_CHECK_EQUAL(packet.size(), buffer.size());
    BOOST_CHECK_EQUAL(packet.salt(), 0);
    BOOST_CHECK_EQUAL(packet.sign(), novemus::packet::packet_sign);
    BOOST_CHECK_EQUAL(packet.version(), novemus::packet::packet_version);
    BOOST_CHECK_EQUAL(packet.pin(), 12345);

    auto payload = packet.data();
    auto sect = payload.advance();
    
    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), cb.size() + novemus::snippet::header_size);
    BOOST_CHECK_EQUAL(sect.size(), cb.size() + novemus::snippet::header_size + novemus::section::header_size);

    auto snip = novemus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect = payload.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data | novemus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::cursor::cursor_size + novemus::section::header_size);

    auto handle = novemus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = payload.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), novemus::section::header_size);

    sect = payload.advance();

    BOOST_CHECK(!sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 0);
    BOOST_CHECK_EQUAL(sect.size(), 0);
}

BOOST_AUTO_TEST_CASE(dimmer)
{
    novemus::packet packet(12345);

    BOOST_CHECK(packet.valid());
    BOOST_CHECK_EQUAL(packet.count(), 1);
    BOOST_CHECK_EQUAL(packet.size(), novemus::packet::header_size);
    BOOST_CHECK_EQUAL(packet.salt(), 0);
    BOOST_CHECK_EQUAL(packet.sign(), novemus::packet::packet_sign);
    BOOST_CHECK_EQUAL(packet.version(), novemus::packet::packet_version);
    BOOST_CHECK_EQUAL(packet.pin(), 12345);
    BOOST_CHECK_EQUAL(packet.data().count(), 0);

    novemus::const_buffer cb("hello, tubus");

    packet.push_back(novemus::section(novemus::snippet(1024, cb)));
    packet.push_back(novemus::section(novemus::cursor(1024)));
    packet.push_back(novemus::section(novemus::section::link));

    BOOST_CHECK_EQUAL(packet.count(), 7);

    auto buffer = novemus::dimmer::invert(1234567890, packet.unite());
    BOOST_CHECK_EQUAL(buffer.size(), packet.size());

    buffer = novemus::dimmer::invert(1234567890, buffer);
    BOOST_CHECK_EQUAL(buffer.size(), packet.size());

    packet = novemus::packet(buffer);

    BOOST_CHECK(packet.valid());
    BOOST_CHECK_EQUAL(packet.count(), 7);
    BOOST_CHECK_EQUAL(packet.size(), buffer.size());
    BOOST_CHECK_EQUAL(packet.salt(), 0);
    BOOST_CHECK_EQUAL(packet.sign(), novemus::packet::packet_sign);
    BOOST_CHECK_EQUAL(packet.version(), novemus::packet::packet_version);
    BOOST_CHECK_EQUAL(packet.pin(), 12345);

    auto payload = packet.data();
    auto sect = payload.advance();
    
    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data);
    BOOST_CHECK_EQUAL(sect.length(), cb.size() + novemus::snippet::header_size);
    BOOST_CHECK_EQUAL(sect.size(), cb.size() + novemus::snippet::header_size + novemus::section::header_size);

    auto snip = novemus::snippet(sect.value());

    BOOST_CHECK(snip.valid());
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.fragment().size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect = payload.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 2);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::data | novemus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), novemus::cursor::cursor_size);
    BOOST_CHECK_EQUAL(sect.size(), novemus::cursor::cursor_size + novemus::section::header_size);

    auto handle = novemus::cursor(sect.value());

    BOOST_CHECK(handle.valid());
    BOOST_CHECK_EQUAL(handle.value(), 1024);

    sect = payload.advance();

    BOOST_CHECK(sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 1);
    BOOST_CHECK_EQUAL(sect.type(), novemus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), novemus::section::header_size);

    sect = payload.advance();

    BOOST_CHECK(!sect.valid());
    BOOST_CHECK_EQUAL(sect.count(), 0);
    BOOST_CHECK_EQUAL(sect.size(), 0);
}
