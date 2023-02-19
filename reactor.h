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
#include <vector>
#include <atomic>
#include <boost/thread/thread.hpp>
#include <boost/asio.hpp>

namespace wormhole {

class reactor
{
    class worker
    {
        boost::asio::io_context& m_io;
        std::promise<boost::thread::id> m_id;
        std::promise<void> m_job;

    public:

        worker(boost::asio::io_context& io) noexcept(true) : m_io(io)
        {
        }

        void exec() noexcept(true)
        {
            m_id.set_value(boost::this_thread::get_id());

            boost::system::error_code code;
            m_io.run(code);

            if (code)
                _err_ << code.message();

            m_job.set_value();
        }

        void wait() noexcept(true)
        {
            return m_job.get_future().get();
        }

        boost::thread::id thread_id() noexcept(true)
        {
            return m_id.get_future().get();
        }
    };

    size_t m_size;
    boost::asio::io_context m_io;
    boost::thread_group m_pool;
    std::vector<std::shared_ptr<worker>> m_load;
    std::atomic<boost::asio::io_context::work*> m_work;

    void activate(bool attach) noexcept(false)
    {
        if (m_size == 0)
        {
            throw std::runtime_error("zero reactor");
        }

        auto work = m_work.exchange(new boost::asio::io_context::work(m_io));
        if (work)
        {
            delete m_work.exchange(work);
            throw std::runtime_error("active reactor");
        }

        m_io.reset();

        for (size_t i = 0; i < (attach ? m_size - 1 : m_size); ++i)
        {
            auto task = std::make_shared<worker>(m_io);
            m_pool.create_thread(std::bind(&worker::exec, task));
            m_load.push_back(task);
        }

        if (attach)
        {
            auto task = std::make_shared<worker>(m_io);
            m_load.push_back(task);
            task->exec();
        }
    }

    void shutdown(bool hard, bool join) noexcept(true)
    {
        try
        {
            auto work = m_work.exchange(0);
            
            if (!work)
                return;

            delete work;

            if (hard)
                m_io.stop();

            if (join)
            {
                auto id = boost::this_thread::get_id();
                for (auto& task : m_load)
                {
                    if (task->thread_id() != id)
                        task->wait();
                }
            }

            m_load.clear();

            if (!hard)
                m_io.stop();
        }
        catch (const std::exception& e)
        {
            _err_ << e.what();
        }
    }

public:

    reactor(size_t size = std::thread::hardware_concurrency()) noexcept(true)
        : m_size(size)
        , m_work(0)
    {
    }

    ~reactor() noexcept(true)
    {
        shutdown(true, false);
    }

    void execute() noexcept(false)
    {
        activate(true);
    }

    void activate() noexcept(false)
    {
        activate(false);
    }

    void terminate(bool join = false) noexcept(true)
    {
        shutdown(true, join);
    }

    void complete(bool join = false) noexcept(true)
    {
        shutdown(false, join);
    }

    boost::asio::io_context& io() noexcept(true)
    {
        return m_io;
    }
};

typedef std::shared_ptr<reactor> reactor_ptr;

reactor_ptr shared_reactor() noexcept(true);

}
