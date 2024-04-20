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

class executor : public boost::asio::io_context
{
    std::vector<std::thread> m_pool;
    std::unique_ptr<boost::asio::io_context::work> m_work;

    void start(size_t size, bool produce)
    {
        if(!m_pool.empty()) 
            throw std::runtime_error("executor is already started");

        if (!produce)
            m_work = std::make_unique<boost::asio::io_context::work>(*this);

        auto job = [this]()
        {
            boost::system::error_code code;
            boost::asio::io_context::run(code);
            _err_ << code.message();
        };

        for(size_t i = 0; i < (produce ? size - 1 : size); ++i)
            m_pool.emplace_back(job);

        if (produce)
            job();
    }

public:

    executor()
    {
    }

    ~executor()
    {
        boost::asio::io_context::stop();
        m_work.reset();
        for(auto& thread : m_pool)
        {
            if (thread.get_id() != std::this_thread::get_id())
                thread.join();
        }
    }

    void operate(size_t size = std::thread::hardware_concurrency())
    {
        start(size, false);
    }

    void produce(size_t size = std::thread::hardware_concurrency())
    {
        start(size, true);
    }
};

}
