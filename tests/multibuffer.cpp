#include "../multibuffer.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(multi_buffer)
{
    std::cout << "multi_buffer" << std::endl;

    novemus::const_buffer hello("hello");
    novemus::const_buffer multibuffer("multibuffer");
    
    novemus::multibuffer mb1;

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

    novemus::multibuffer mb2(mb1);

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

    novemus::multibuffer mb3(mb1.begin(), mb1.end());
    mb3.push_front(mb2);

    BOOST_CHECK_EQUAL(mb3.count(), 4);
    BOOST_CHECK_EQUAL(mb3.size(), 32);

    BOOST_CHECK_EQUAL(mb3.at(0).data(), multibuffer.data());
    BOOST_CHECK_EQUAL(mb3.at(1).data(), hello.data());
    BOOST_CHECK_EQUAL(mb3.at(2).data(), multibuffer.data());
    BOOST_CHECK_EQUAL(mb3.at(3).data(), hello.data());
}

BOOST_AUTO_TEST_CASE(multi_cursor)
{
    std::cout << "multi_cursor" << std::endl;

    novemus::mutable_buffer mb(64);
    std::memset(mb.data(), 0, mb.size());

    novemus::cursor handle(mb);

    BOOST_CHECK_EQUAL(handle.count(), 1);
    BOOST_CHECK_EQUAL(handle.value(), 0);
    BOOST_CHECK_EQUAL(handle.size(), novemus::cursor::cursor_size);

    handle = novemus::cursor(1234567890);

    BOOST_CHECK_EQUAL(handle.count(), 1);
    BOOST_CHECK_EQUAL(handle.value(), 1234567890);
    BOOST_CHECK_EQUAL(handle.size(), novemus::cursor::cursor_size);

    novemus::cursor copy(handle);

    BOOST_CHECK_EQUAL(handle.count(), copy.count());
    BOOST_CHECK_EQUAL(handle.value(), copy.value());
    BOOST_CHECK_EQUAL(handle.size(), copy.size());

    copy = novemus::cursor(handle.unite());

    BOOST_CHECK_EQUAL(handle.count(), copy.count());
    BOOST_CHECK_EQUAL(handle.value(), copy.value());
    BOOST_CHECK_EQUAL(handle.size(), copy.size());
}

BOOST_AUTO_TEST_CASE(multi_snippet)
{
    std::cout << "multi_snippet" << std::endl;

    novemus::mutable_buffer mb(1024);
    std::memset(mb.data(), 0, mb.size());

    novemus::snippet snip(mb);

    BOOST_CHECK_EQUAL(snip.handle(), 0);
    BOOST_CHECK_EQUAL(snip.size(), 1024);
    BOOST_CHECK_EQUAL(snip.fragment().size(), snip.size() - novemus::snippet::header_size);

    novemus::const_buffer cb("hello, tubus");

    snip = novemus::snippet(1024, cb);

    BOOST_CHECK_EQUAL(snip.count(), 2);
    BOOST_CHECK_EQUAL(snip.handle(), 1024);
    BOOST_CHECK_EQUAL(snip.size(), cb.size() + novemus::snippet::header_size);
    BOOST_CHECK_EQUAL(std::memcmp(cb.data(), snip.fragment().data(), cb.size()), 0);

    novemus::snippet copy(snip);

    BOOST_CHECK_EQUAL(snip.count(), copy.count());
    BOOST_CHECK_EQUAL(snip.handle(), copy.handle());
    BOOST_CHECK_EQUAL(snip.size(), copy.size());
    BOOST_CHECK_EQUAL(snip.fragment().data(), copy.fragment().data());
    BOOST_CHECK_EQUAL(snip.at(0).data(), copy.at(0).data());
    BOOST_CHECK_EQUAL(snip.at(1).data(), copy.at(1).data());

    novemus::snippet copy(snip.unite());

    BOOST_CHECK_EQUAL(snip.count(), copy.count());
    BOOST_CHECK_EQUAL(snip.handle(), copy.handle());
    BOOST_CHECK_EQUAL(snip.size(), copy.size());
    BOOST_CHECK_EQUAL(std::memcmp(snip.at(0).data(), copy.at(0).data(), copy.at(0).size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(snip.at(1).data(), copy.at(1).data(), copy.at(1).size()), 0);
}

BOOST_AUTO_TEST_CASE(multi_section)
{
    std::cout << "multi_section" << std::endl;

    novemus::const_buffer cb("hello, tubus");
    novemus::snippet snip(1024, cb);

    novemus::section sect(snip);

    BOOST_CHECK_EQUAL(sect.type(), novemus::section::move_data);
    BOOST_CHECK_EQUAL(sect.length(), snip.size());
    BOOST_CHECK_EQUAL(sect.size(), snip.size() + novemus::section::header_size);
    BOOST_CHECK_EQUAL(sect.count(), 3);
    BOOST_CHECK_EQUAL(sect.value().count(), snip.count());
    BOOST_CHECK_EQUAL(sect.value().at(0).data(), snip.at(0).data());
    BOOST_CHECK_EQUAL(sect.value().at(1).data(), snip.at(1).data());

    novemus::cursor handle(1024);
    novemus::section more(handle);

    BOOST_CHECK_EQUAL(handle.value(), 1024);
    BOOST_CHECK_EQUAL(more.type(), novemus::section::move_ackn);
    BOOST_CHECK_EQUAL(more.length(), handle.size());
    BOOST_CHECK_EQUAL(more.size(), handle.size() + novemus::section::header_size);
    BOOST_CHECK_EQUAL(more.count(), 2);
    BOOST_CHECK_EQUAL(more.value().count(), handle.count());
    BOOST_CHECK_EQUAL(more.value().at(0).data(), handle.at(0).data());

    more = novemus::section(novemus::section::link_init);

    BOOST_CHECK_EQUAL(more.type(), novemus::section::link_init);
    BOOST_CHECK_EQUAL(more.length(), 0);
    BOOST_CHECK_EQUAL(more.size(), novemus::section::header_size);
    BOOST_CHECK_EQUAL(more.count(), 1);

    more = novemus::section(sect);

    BOOST_CHECK_EQUAL(more.type(), sect.type());
    BOOST_CHECK_EQUAL(more.length(), sect.length());
    BOOST_CHECK_EQUAL(more.size(), sect.size());
    BOOST_CHECK_EQUAL(more.count(), sect.count());
    BOOST_CHECK_EQUAL(more.at(0).data(), sect.at(0).data());
    BOOST_CHECK_EQUAL(more.at(1).data(), sect.at(1).data());
    BOOST_CHECK_EQUAL(more.at(2).data(), sect.at(2).data());

    more = novemus::section(sect.unite());

    BOOST_CHECK_EQUAL(more.type(), sect.type());
    BOOST_CHECK_EQUAL(more.length(), sect.length());
    BOOST_CHECK_EQUAL(more.size(), sect.size());
    BOOST_CHECK_EQUAL(more.count(), sect.count());
    BOOST_CHECK_EQUAL(std::memcmp(more.at(0).data(), sect.at(0).data(), sect.at(0).size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(more.at(1).data(), sect.at(1).data(), sect.at(1).size()), 0);
    BOOST_CHECK_EQUAL(std::memcmp(more.at(2).data(), sect.at(2).data(), sect.at(2).size()), 0);
}
