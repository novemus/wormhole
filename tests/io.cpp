/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../io.h"
#include <boost/test/unit_test.hpp>
#include <future>

BOOST_AUTO_TEST_CASE(io_attached)
{
    wormhole::asio_engine io;

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();
    io.post([&p1]()
    {
        p1.set_value();
    });

    std::promise<void> p2;
    std::future<void> f2 = p2.get_future();
    io.post([&p2]()
    {
        p2.set_value();
    });

    BOOST_CHECK(f1.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    BOOST_CHECK(f2.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);

    BOOST_REQUIRE_NO_THROW(io.activate(1, true));

    BOOST_CHECK(f1.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    BOOST_CHECK(f2.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);

    std::promise<void> p3;
    std::future<void> f3 = p3.get_future();
    io.post([&p3]()
    {
        p3.set_value();
    });

    BOOST_CHECK(f3.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
}

BOOST_AUTO_TEST_CASE(io_detached)
{
    wormhole::asio_engine io;

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();
    io.post([&p1]()
    {
        p1.set_value();
    });

    std::promise<void> p2;
    std::future<void> f2 = p2.get_future();
    io.post([&p2]()
    {
        p2.set_value();
    });

    BOOST_CHECK(f1.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    BOOST_CHECK(f2.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);

    BOOST_REQUIRE_NO_THROW(io.activate(1, false));

    BOOST_REQUIRE_NO_THROW(f1.get());
    BOOST_REQUIRE_NO_THROW(f2.get());

    std::promise<void> p3;
    std::future<void> f3 = p3.get_future();
    io.post([&p3]()
    {
        p3.set_value();
    });

    BOOST_CHECK(f3.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
}
