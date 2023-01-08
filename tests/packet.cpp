#include "../packet.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(handle)
{
    novemus::tubus::handle handle(0, 0);

    BOOST_CHECK_EQUAL(handle.offset, 0);
    BOOST_CHECK_EQUAL(handle.length, 0);
    BOOST_CHECK_EQUAL(novemus::tubus::handle::size, sizeof(uint64_t) + sizeof(uint16_t));

    novemus::mutable_buffer mb(1024);
    mb.set<uint64_t>(0, htole64(123));
    mb.set<uint16_t>(sizeof(uint64_t), htons(456));

    handle = novemus::tubus::handle(mb);

    BOOST_CHECK_EQUAL(handle.offset, 123);
    BOOST_CHECK_EQUAL(handle.length, 456);
}

BOOST_AUTO_TEST_CASE(snippet)
{
    novemus::mutable_buffer mb(1024);
    std::memset(mb.data(), 0, mb.size());

    novemus::tubus::snippet snip(mb);

    BOOST_CHECK_EQUAL(snip.offset, 0);
    BOOST_CHECK_EQUAL(snip.piece.size(), 1024 - novemus::tubus::snippet::header_size);

    novemus::const_buffer cb("hello, tubus");
    snip = novemus::tubus::snippet(1024, cb);

    BOOST_CHECK_EQUAL(snip.offset, 1024);
    BOOST_CHECK_EQUAL(snip.piece.size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.piece.data(), cb.size()), 0);
}

BOOST_AUTO_TEST_CASE(section)
{
    novemus::mutable_buffer mb(1024);
    std::memset(mb.data(), 0, mb.size());

    novemus::tubus::section sect(mb);
    auto vb = sect.value();

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::list_stub);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), 1024);
    BOOST_CHECK_EQUAL(vb.size(), 1024 - novemus::tubus::section::header_size);

    sect.set(novemus::tubus::section::link_init);
    vb = sect.value();

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::link_init);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::section::header_size);
    BOOST_CHECK_EQUAL(vb.size(), 0);

    auto more = sect.next();
    vb = more.value();

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::section::list_stub);
    BOOST_CHECK_EQUAL(more.length(), 0);
    BOOST_CHECK_EQUAL(more.size(), 1024 - novemus::tubus::section::header_size);
    BOOST_CHECK_EQUAL(vb.size(), 1024 - novemus::tubus::section::header_size * 2);

    novemus::tubus::snippet snip(3, novemus::const_buffer("hello, tubus"));
    more.set(snip);

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::section::data_move);
    BOOST_CHECK_EQUAL(more.length(), snip.piece.size() + novemus::tubus::snippet::header_size);
    BOOST_CHECK_EQUAL(more.size(), snip.piece.size() + novemus::tubus::snippet::header_size + novemus::tubus::section::header_size);

    more = more.next();

    novemus::tubus::handle handle(123, 456);
    more.set(handle);

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::section::data_ackn);
    BOOST_CHECK_EQUAL(more.length(), novemus::tubus::handle::size);
    BOOST_CHECK_EQUAL(more.size(), novemus::tubus::section::header_size + novemus::tubus::handle::size);
    BOOST_CHECK_EQUAL(handle.offset, 123);
    BOOST_CHECK_EQUAL(handle.length, 456);

    more = more.next();
    more.set(novemus::tubus::section::link_ackn);

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::section::link_ackn);
    BOOST_CHECK_EQUAL(more.length(), 0);
    BOOST_CHECK_EQUAL(more.value().size(), 0);
    BOOST_CHECK_EQUAL(more.size(), novemus::tubus::section::header_size);

    more = more.next();

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::link_init);

    auto tail = sect.tail();

    BOOST_CHECK_EQUAL(more.type(), tail.type());
    BOOST_CHECK_EQUAL(more.length(), tail.length());
    BOOST_CHECK_EQUAL(more.size(), tail.size());
    BOOST_CHECK_EQUAL(more.data(), tail.data());

    auto head = more.head();

    BOOST_CHECK_EQUAL(sect.type(), head.type());
    BOOST_CHECK_EQUAL(sect.length(), head.length());
    BOOST_CHECK_EQUAL(sect.size(), head.size());
    BOOST_CHECK_EQUAL(sect.data(), head.data());

    BOOST_CHECK_EQUAL(head.type(), novemus::tubus::section::link_init);

    auto second = head.next();
    BOOST_CHECK_EQUAL(second.type(), novemus::tubus::section::data_move);
    BOOST_CHECK_EQUAL(head.data() + head.size(), second.data());

    auto third = second.next();
    BOOST_CHECK_EQUAL(third.type(), novemus::tubus::section::data_ackn);
    BOOST_CHECK_EQUAL(second.data() + second.size(), third.data());

    auto fourth = third.next();
    BOOST_CHECK_EQUAL(fourth.type(), novemus::tubus::section::link_ackn);
    BOOST_CHECK_EQUAL(third.data() + third.size(), fourth.data());

    auto fifth = fourth.next();
    BOOST_CHECK_EQUAL(fifth.type(), novemus::tubus::section::list_stub);

    BOOST_CHECK_EQUAL(fifth.data() + fifth.size(), mb.data() + mb.size());
}

BOOST_AUTO_TEST_CASE(packet)
{
    novemus::mutable_buffer mb(novemus::tubus::packet::max_packet_size);
    std::memset(mb.data(), 0, mb.size());

    novemus::tubus::packet pack(mb);
    pack.salt(0);
    pack.sign(novemus::tubus::packet::packet_sign);
    pack.version(novemus::tubus::packet::packet_version);
    pack.pin(456);

    BOOST_CHECK_EQUAL(pack.salt(), 0);
    BOOST_CHECK_EQUAL(pack.sign(), novemus::tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(pack.version(), novemus::tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(pack.pin(), 456);
    BOOST_CHECK_EQUAL(pack.size(), novemus::tubus::packet::max_packet_size);

    auto sect = pack.useless();
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::list_stub);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), pack.size() - novemus::tubus::packet::header_size);

    novemus::const_buffer cb("hello, tubus");
    novemus::tubus::snippet snip(7, cb);
    sect.set(snip);

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data_move);
    BOOST_CHECK_EQUAL(sect.length(), snip.piece.size() + novemus::tubus::snippet::header_size);
    BOOST_CHECK_EQUAL(sect.size(), snip.piece.size() + novemus::tubus::snippet::header_size + novemus::tubus::section::header_size);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.piece.data(), cb.size()), 0);

    auto tail = sect.next();
    BOOST_CHECK_EQUAL(tail.type(), novemus::tubus::section::list_stub);
    BOOST_CHECK_EQUAL(tail.length(), 0);
    BOOST_CHECK_EQUAL(tail.size(), pack.size() - novemus::tubus::packet::header_size - sect.size());

    pack.trim();

    BOOST_CHECK_EQUAL(pack.size(), sect.size() + novemus::tubus::packet::header_size);

    pack.make_opaque(1234567890);
    pack.make_opened(1234567890);

    BOOST_CHECK_NE(pack.salt(), 0);
    BOOST_CHECK_EQUAL(pack.sign(), novemus::tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(pack.version(), novemus::tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(pack.pin(), 456);
    BOOST_CHECK_EQUAL(pack.size(), sect.size() + novemus::tubus::packet::header_size);

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::data_move);
    BOOST_CHECK_EQUAL(sect.length(), snip.piece.size() + novemus::tubus::snippet::header_size);
    BOOST_CHECK_EQUAL(sect.size(), snip.piece.size() + novemus::tubus::snippet::header_size + novemus::tubus::section::header_size);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.piece.data(), cb.size()), 0);
}
