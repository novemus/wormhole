#include "../packet.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(cursor)
{
    std::cout << "cursor" << std::endl;

    novemus::mutable_buffer mb(64);
    std::memset(mb.data(), 0, mb.size());

    novemus::tubus::cursor handle(mb);

    BOOST_CHECK_EQUAL(handle.value(), 0);
    BOOST_CHECK_EQUAL(handle.size(), novemus::tubus::cursor::cursor_size);

    handle.value(1234567890);

    BOOST_CHECK_EQUAL(handle.value(), 1234567890);
    BOOST_CHECK_EQUAL(handle.size(), novemus::tubus::cursor::cursor_size);
}

BOOST_AUTO_TEST_CASE(snippet)
{
    std::cout << "snippet" << std::endl;

    novemus::mutable_buffer mb(1024);
    std::memset(mb.data(), 0, mb.size());

    novemus::tubus::snippet snip(mb);

    BOOST_CHECK_EQUAL(snip.handle(), 0);
    BOOST_CHECK_EQUAL(snip.size(), 1024);

    novemus::const_buffer cb("hello, tubus");
    snip.set(1024, cb);

    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.size(), cb.size() + novemus::tubus::snippet::handle_size);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.fragment().data(), cb.size()), 0);
}

BOOST_AUTO_TEST_CASE(section)
{
    std::cout << "section" << std::endl;

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

    novemus::tubus::snippet snip(more.value());
    snip.set(3, novemus::const_buffer("hello, tubus"));
    more.type(novemus::tubus::section::move_data);
    more.length(snip.size());

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::section::move_data);
    BOOST_CHECK_EQUAL(more.length(), snip.size());
    BOOST_CHECK_EQUAL(more.size(), snip.size() + novemus::tubus::section::header_size);

    more = more.next();

    novemus::tubus::cursor handle(novemus::mutable_buffer(8));
    handle.value(123);

    more.set(novemus::tubus::section::move_ackn, handle);

    BOOST_CHECK_EQUAL(more.type(), novemus::tubus::section::move_ackn);
    BOOST_CHECK_EQUAL(more.length(), sizeof(uint64_t));
    BOOST_CHECK_EQUAL(more.size(), sizeof(uint64_t) + novemus::tubus::section::header_size);
    BOOST_CHECK_EQUAL(more.value().get<uint64_t>(0), 123);

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
    BOOST_CHECK_EQUAL(second.type(), novemus::tubus::section::move_data);
    BOOST_CHECK_EQUAL(head.data() + head.size(), second.data());

    auto third = second.next();
    BOOST_CHECK_EQUAL(third.type(), novemus::tubus::section::move_ackn);
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
    std::cout << "packet" << std::endl;

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
    novemus::tubus::snippet snip(sect.value());
    snip.set(7, cb);
    sect.type(novemus::tubus::section::move_data);
    sect.length(snip.size());

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::move_data);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::tubus::section::header_size);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.fragment().data(), cb.size()), 0);

    auto tail = sect.next();
    BOOST_CHECK_EQUAL(tail.type(), novemus::tubus::section::list_stub);
    BOOST_CHECK_EQUAL(tail.length(), 0);
    BOOST_CHECK_EQUAL(tail.size(), pack.size() - novemus::tubus::packet::header_size - sect.size());

    pack.trim();

    BOOST_CHECK_EQUAL(pack.size(), sect.size() + novemus::tubus::packet::header_size);

    pack.make_opaque(1234567890);
    pack.make_opened(1234567890);

    BOOST_CHECK_EQUAL(pack.salt(), 0);
    BOOST_CHECK_EQUAL(pack.sign(), novemus::tubus::packet::packet_sign);
    BOOST_CHECK_EQUAL(pack.version(), novemus::tubus::packet::packet_version);
    BOOST_CHECK_EQUAL(pack.pin(), 456);
    BOOST_CHECK_EQUAL(pack.size(), sect.size() + novemus::tubus::packet::header_size);

    BOOST_CHECK_EQUAL(sect.type(), novemus::tubus::section::move_data);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::tubus::section::header_size);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.fragment().data(), cb.size()), 0);
}
