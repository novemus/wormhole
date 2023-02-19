/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../packet.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(tubus_cursor)
{
    wormhole::mutable_buffer mb(8);
    std::memset(mb.data(), 0, mb.size());

    auto curs = wormhole::tubus::cursor(mb);

    BOOST_CHECK_EQUAL(curs.size(), 8);
    BOOST_CHECK_EQUAL(curs.handle(), 0);
}

BOOST_AUTO_TEST_CASE(snippet)
{
    wormhole::mutable_buffer mb(16);
    std::memset(mb.data(), 0, mb.size());

    wormhole::tubus::snippet snip(mb);
    BOOST_CHECK_EQUAL(snip.size(), 16);
    BOOST_CHECK_EQUAL(snip.handle(), 0);
    BOOST_CHECK_EQUAL(snip.fragment().size(), 8);
    BOOST_CHECK_EQUAL(snip.fragment().data(), (uint8_t*)mb.data() + 8);
}

BOOST_AUTO_TEST_CASE(tubus_section)
{
    wormhole::mutable_buffer mb(1024);
    wormhole::tubus::section sect(mb);

    BOOST_CHECK_EQUAL(sect.size(), 1024);

    sect.cursor(12345);

    BOOST_CHECK_EQUAL(sect.type(), wormhole::tubus::section::flag::move | wormhole::tubus::section::echo);
    BOOST_CHECK_EQUAL(sect.length(), wormhole::tubus::cursor::handle_size);
    BOOST_CHECK_EQUAL(sect.value().size(), wormhole::tubus::cursor::handle_size);

    wormhole::tubus::cursor curs(sect.value());

    BOOST_CHECK_EQUAL(curs.size(), wormhole::tubus::cursor::handle_size);
    BOOST_CHECK_EQUAL(curs.handle(), 12345);

    sect.advance();

    wormhole::const_buffer cb("hello, tubus");
    sect.snippet(9, cb);

    BOOST_CHECK_EQUAL(sect.type(), wormhole::tubus::section::move);
    BOOST_CHECK_EQUAL(sect.length(), wormhole::tubus::snippet::handle_size + cb.size());
    
    wormhole::tubus::snippet snip(sect.value());

    BOOST_CHECK_EQUAL(snip.size(), wormhole::tubus::snippet::handle_size + cb.size());
    BOOST_CHECK_EQUAL(snip.handle(), 9);
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect.advance();
    sect.simple(wormhole::tubus::section::link);

    BOOST_CHECK_EQUAL(sect.type(), wormhole::tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.value().size(), 0);

    sect.advance();
    sect.stub();

    BOOST_CHECK_EQUAL(sect.type(), 0);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.value().size(), 0);

    BOOST_CHECK_EQUAL(sect.size(), 1024 - wormhole::tubus::section::header_size * 3 - curs.size() - snip.size());
}

BOOST_AUTO_TEST_CASE(tubus_packet)
{
    wormhole::mutable_buffer mb(1024);
    wormhole::tubus::packet pack(mb);

    pack.set<uint64_t>(0, 0);
    pack.set<uint16_t>(sizeof(uint64_t), htons(wormhole::tubus::packet::packet_sign));
    pack.set<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t), htons(wormhole::tubus::packet::packet_version));
    pack.set<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2, htonl(12345));
    pack.set<uint32_t>(wormhole::tubus::packet::header_size, 0);

    BOOST_CHECK_EQUAL(pack.size(), mb.size());
    BOOST_CHECK_EQUAL(pack.salt(), 0);
    BOOST_CHECK_EQUAL(pack.sign(), wormhole::tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(pack.version(), wormhole::tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(pack.pin(), 12345);

    auto sect = pack.body();

    wormhole::const_buffer cb("hello, tubus");
    sect.snippet(12345, cb);

    sect.advance();
    sect.cursor(12345);

    sect.advance();
    sect.simple(wormhole::tubus::section::link);

    sect.advance();
    sect.stub();

    pack.trim();

    BOOST_CHECK_EQUAL(pack.size(), wormhole::tubus::packet::header_size + wormhole::tubus::section::header_size * 3 + wormhole::tubus::cursor::handle_size + wormhole::tubus::snippet::handle_size + cb.size());

    sect = pack.body();
    wormhole::tubus::snippet snip(sect.value());

    BOOST_CHECK_EQUAL(snip.size(), wormhole::tubus::snippet::handle_size + cb.size());
    BOOST_CHECK_EQUAL(snip.handle(), 12345);
    BOOST_CHECK_EQUAL(std::memcmp(snip.fragment().data(), cb.data(), cb.size()), 0);

    sect.advance();
    wormhole::tubus::cursor curs(sect.value());

    BOOST_CHECK_EQUAL(curs.size(), wormhole::tubus::cursor::handle_size);
    BOOST_CHECK_EQUAL(curs.handle(), 12345);

    sect.advance();

    BOOST_CHECK_EQUAL(sect.type(), wormhole::tubus::section::link);
    BOOST_CHECK_EQUAL(sect.length(), 0);

    sect.advance();
    sect.stub();

    BOOST_CHECK_EQUAL(sect.type(), 0);
    BOOST_CHECK_EQUAL(sect.length(), 0);

    auto stub = pack.stub();

    BOOST_CHECK_EQUAL(stub.data(), sect.data());

    wormhole::mutable_buffer copy(pack.size());
    copy.fill(0, copy.size(), pack.data());

    copy = wormhole::tubus::dimmer::invert(1234567890, copy);
    copy = wormhole::tubus::dimmer::invert(1234567890, copy);

    BOOST_CHECK_EQUAL(pack.size(), copy.size());
    BOOST_CHECK_EQUAL(std::memcmp(pack.data(), copy.data(), copy.size()), 0);
}
