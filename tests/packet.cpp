#include "../packet.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(snippet)
{
    novemus::mutable_buffer mb(1024);
    std::memset(mb.data(), 0, mb.size());

    novemus::tubus::snippet snip(mb);

    BOOST_CHECK_EQUAL(snip.pos(), 0);
    BOOST_CHECK_EQUAL(snip.length(), 0);
    BOOST_CHECK_EQUAL(snip.size(), 1024);

    auto db = snip.data();
    BOOST_CHECK_EQUAL(db.size(), 1024 - novemus::tubus::snippet::header_size);
    BOOST_CHECK_EQUAL(db.get<uint64_t>(0), 0);

    novemus::const_buffer cb("hello, tubus");
    snip.set(1024, cb);

    BOOST_CHECK_EQUAL(snip.pos(), 1024);
    BOOST_CHECK_EQUAL(snip.length(), cb.size());
    BOOST_CHECK_EQUAL(snip.size(), cb.size() + novemus::tubus::snippet::header_size);

    db = snip.data();
    BOOST_CHECK_EQUAL(snip.length(), db.size());
    BOOST_CHECK_EQUAL(db.size(), cb.size());
    BOOST_CHECK_EQUAL(std::memcmp(db.data(), cb.data(), cb.size()), 0);

    auto head = snip.header();
    BOOST_CHECK_EQUAL(head.pos(), 1024);
    BOOST_CHECK_EQUAL(head.length(), db.size());
    BOOST_CHECK_EQUAL(head.size(), novemus::tubus::snippet::header_size);

    head = novemus::tubus::snippet::info(mb);
    head.set(256, 2048);

    BOOST_CHECK_EQUAL(head.pos(), 256);
    BOOST_CHECK_EQUAL(head.length(), 2048);
    BOOST_CHECK_EQUAL(head.size(), novemus::tubus::snippet::header_size);
}

BOOST_AUTO_TEST_CASE(section)
{
    novemus::mutable_buffer mb(1024);
    std::memset(mb.data(), 0, mb.size());

    novemus::tubus::packet::section sect(mb);
    auto vb = sect.value();

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::packet::section::list_end);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), 1024);
    BOOST_CHECK_EQUAL(vb.size(), 1024 - novemus::tubus::packet::section::header_size);

    sect.set(novemus::tubus::packet::section::link_req);
    vb = sect.value();

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::packet::section::link_req);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), novemus::tubus::packet::section::header_size);
    BOOST_CHECK_EQUAL(vb.size(), 0);

    auto more = sect.next();
    vb = more.value();

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::packet::section::list_end);
    BOOST_CHECK_EQUAL(more.length(), 0);
    BOOST_CHECK_EQUAL(more.size(), 1024 - novemus::tubus::packet::section::header_size);
    BOOST_CHECK_EQUAL(vb.size(), 1024 - novemus::tubus::packet::section::header_size * 2);

    novemus::const_buffer cb("hello, tubus");
    novemus::tubus::snippet snip(vb);
    snip.set(3, cb);
    more.set(novemus::tubus::packet::section::push_req, snip.buffer());

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::packet::section::push_req);
    BOOST_CHECK_EQUAL(more.length(), snip.size());
    BOOST_CHECK_EQUAL(more.value().size(), snip.size());
    BOOST_CHECK_EQUAL(more.size(), snip.size() + novemus::tubus::packet::section::header_size);

    more = more.next();
    auto info = snip.header();
    more.set(novemus::tubus::packet::section::push_ack, info.buffer());

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::packet::section::push_ack);
    BOOST_CHECK_EQUAL(more.length(), info.size());
    BOOST_CHECK_EQUAL(more.size(), novemus::tubus::packet::section::header_size + info.size());
    BOOST_CHECK_EQUAL(info.pos(), 3);
    BOOST_CHECK_EQUAL(info.size(), novemus::tubus::snippet::header_size);
    BOOST_CHECK_EQUAL(info.length(), snip.length());

    more = more.next();
    more.set(novemus::tubus::packet::section::link_ack);

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::packet::section::link_ack);
    BOOST_CHECK_EQUAL(more.length(), 0);
    BOOST_CHECK_EQUAL(more.value().size(), 0);
    BOOST_CHECK_EQUAL(more.size(), novemus::tubus::packet::section::header_size);

    more = more.next();

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::packet::section::link_req);

    auto tail = sect.tail();

    BOOST_CHECK_EQUAL(more.type(), tail.type());
    BOOST_CHECK_EQUAL(more.length(), tail.length());
    BOOST_CHECK_EQUAL(more.size(), tail.size());
    BOOST_CHECK_EQUAL(more.buffer().data(), tail.buffer().data());

    auto head = more.head();

    BOOST_CHECK_EQUAL(sect.type(), head.type());
    BOOST_CHECK_EQUAL(sect.length(), head.length());
    BOOST_CHECK_EQUAL(sect.size(), head.size());
    BOOST_CHECK_EQUAL(sect.buffer().data(), head.buffer().data());

    BOOST_CHECK_EQUAL(head.type(), novemus::tubus::packet::section::link_req);

    auto second = head.next();
    BOOST_CHECK_EQUAL(second.type(), novemus::tubus::packet::section::push_req);
    BOOST_CHECK_EQUAL(head.buffer().data() + head.size(), second.buffer().data());

    auto third = second.next();
    BOOST_CHECK_EQUAL(third.type(), novemus::tubus::packet::section::push_ack);
    BOOST_CHECK_EQUAL(second.buffer().data() + second.size(), third.buffer().data());

    auto fourth = third.next();
    BOOST_CHECK_EQUAL(fourth.type(), novemus::tubus::packet::section::link_ack);
    BOOST_CHECK_EQUAL(third.buffer().data() + third.size(), fourth.buffer().data());

    auto fifth = fourth.next();
    BOOST_CHECK_EQUAL(fifth.type(), novemus::tubus::packet::section::list_end);
    BOOST_CHECK_EQUAL(fourth.buffer().data() + fourth.size(), fifth.buffer().data());

    BOOST_CHECK_EQUAL(fifth.buffer().data() + fifth.size(), mb.data() + mb.size());
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
    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::packet::section::list_end);
    BOOST_CHECK_EQUAL(sect.length(), 0);
    BOOST_CHECK_EQUAL(sect.size(), pack.size() - novemus::tubus::packet::header_size);

    novemus::const_buffer cb("hello, tubus");
    novemus::tubus::snippet snip(sect.value());
    snip.set(7, cb);

    sect.type(novemus::tubus::packet::section::push_req);
    sect.length(snip.size());

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::packet::section::push_req);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::tubus::packet::section::header_size);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.data().data(), cb.size()), 0);

    auto tail = sect.next();
    BOOST_CHECK_EQUAL(tail.type(), novemus::tubus::packet::section::list_end);
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
    BOOST_CHECK_EQUAL(pack.size(), pack.buffer().size());

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::packet::section::push_req);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::tubus::packet::section::header_size);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.data().data(), cb.size()), 0);
}
