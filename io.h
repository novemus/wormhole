/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#pragma once

#include "logger.h"
#include <thread>
#include <vector>
#include <boost/asio.hpp>

namespace wormhole {

class asio_engine : public boost::asio::io_context
{
    std::vector<std::thread> m_pool;

    void execute()
    {
        boost::system::error_code code;
        boost::asio::io_context::run(code);
        _err_ << code.message();
    }

public:

    asio_engine()
    {
    }

    ~asio_engine()
    {
        boost::asio::io_context::stop();
        for(auto& thread : m_pool)
        {
            if (thread.get_id() != std::this_thread::get_id())
                thread.join();
        }
    }

    void activate(size_t size = std::thread::hardware_concurrency(), bool attach = false)
    {
        assert(m_pool.empty());

        for(size_t i = 0; i < (attach ? size - 1 : size); ++i)
            m_pool.emplace_back(std::bind(&asio_engine::execute, this));

        if (attach)
            execute();
    }
};

}
