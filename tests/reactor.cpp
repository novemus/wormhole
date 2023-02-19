/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../reactor.h"
#include <boost/test/unit_test.hpp>
#include <future>

BOOST_AUTO_TEST_CASE(reactor_execute)
{
    auto reactor = std::make_shared<wormhole::reactor>();

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();
    reactor->io().post([&p1]()
    {
        p1.set_value();
    });

    std::promise<void> p2;
    std::future<void> f2 = p2.get_future();
    reactor->io().post([&p2]()
    {
        p2.set_value();
    });

    std::promise<void> p3;
    std::future<void> f3 = p3.get_future();
    reactor->io().post([&p3, reactor]()
    {
        p3.set_value();
        BOOST_REQUIRE_NO_THROW(reactor->complete());
    });

    BOOST_REQUIRE_NO_THROW(reactor->execute());

    BOOST_REQUIRE_NO_THROW(f1.get());
    BOOST_REQUIRE_NO_THROW(f2.get());
    BOOST_REQUIRE_NO_THROW(f3.get());
}

BOOST_AUTO_TEST_CASE(reactor_activate)
{
    auto reactor = std::make_shared<wormhole::reactor>();

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();
    reactor->io().post([&p1]()
    {
        p1.set_value();
    });

    std::promise<void> p2;
    std::future<void> f2 = p2.get_future();
    reactor->io().post([&p2]()
    {
        p2.set_value();
    });

    BOOST_REQUIRE_NO_THROW(reactor->activate());
    BOOST_REQUIRE_NO_THROW(reactor->complete(true));

    std::promise<void> p3;
    std::future<void> f3 = p3.get_future();
    reactor->io().post([&p3]()
    {
        p3.set_value();
    });

    BOOST_REQUIRE_NO_THROW(f1.get());
    BOOST_REQUIRE_NO_THROW(f2.get());
    BOOST_CHECK(f3.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
}

BOOST_AUTO_TEST_CASE(reactor_terminate)
{
    auto reactor = std::make_shared<wormhole::reactor>(1);

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();
    reactor->io().post([&p1, reactor]()
    {
        BOOST_REQUIRE_NO_THROW(reactor->terminate(true));
        p1.set_value();
    });

    std::promise<void> p2;
    std::future<void> f2 = p2.get_future();
    reactor->io().post([&p2]()
    {
        p2.set_value();
    });

    std::promise<void> p3;
    std::future<void> f3 = p3.get_future();
    reactor->io().post([&p3]()
    {
        p3.set_value();
    });

    BOOST_REQUIRE_NO_THROW(reactor->execute());

    BOOST_REQUIRE_NO_THROW(f1.get());
    BOOST_CHECK(f2.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
    BOOST_CHECK(f3.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
}

BOOST_AUTO_TEST_CASE(reactor_reuse)
{
    auto reactor = std::make_shared<wormhole::reactor>(1);

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();
    reactor->io().post([&p1]()
    {
        p1.set_value();
    });

    std::promise<void> p2;
    std::future<void> f2 = p2.get_future();
    reactor->io().post([&p2]()
    {
        p2.set_value();
    });

    BOOST_REQUIRE_NO_THROW(reactor->activate());
    BOOST_REQUIRE_THROW(reactor->activate(), std::runtime_error);

    BOOST_REQUIRE_NO_THROW(reactor->complete(true));

    BOOST_REQUIRE_NO_THROW(f1.get());
    BOOST_REQUIRE_NO_THROW(f2.get());

    std::promise<void> p3;
    std::future<void> f3 = p3.get_future();
    reactor->io().post([&p3]()
    {
        p3.set_value();
    });

    BOOST_REQUIRE_NO_THROW(reactor->activate());
    BOOST_REQUIRE_NO_THROW(reactor->complete(true));

    BOOST_REQUIRE_NO_THROW(f3.get());
}
