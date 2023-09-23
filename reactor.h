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
#include <mutex>
#include <memory>
#include <vector>
#include <boost/asio.hpp>

namespace wormhole {

class reactor
{
    class context : public std::enable_shared_from_this<context>
    {
        class worker
        {
            std::promise<std::thread::id> m_id;
            std::shared_future<void> m_job;

        public:

            worker(std::shared_ptr<boost::asio::io_context> io, std::launch method) noexcept(true)
            {
                m_job = std::async(method, [this, io]()
                {
                    m_id.set_value(std::this_thread::get_id());

                    boost::system::error_code code;
                    io->run(code);

                    if (code)
                        _err_ << code.message();
                });
            }

            void wait() noexcept(true)
            {
                return m_job.wait();
            }

            std::thread::id get_id() noexcept(true)
            {
                return m_id.get_future().get();
            }
        };

        std::shared_ptr<boost::asio::io_context> m_io;
        std::unique_ptr<boost::asio::io_context::work> m_work;
        std::vector<std::shared_ptr<worker>> m_pool;
        std::mutex m_mutex;

    public:

        context(std::shared_ptr<boost::asio::io_context> io) noexcept(true)
            : m_io(io)
            , m_work(new boost::asio::io_context::work(*io))
        {
        }

        void activate(size_t size, bool attach) noexcept(false)
        {
            if (size == 0)
                throw std::runtime_error("zero pool size");

            std::unique_lock<std::mutex> lock(m_mutex);

            m_io->restart();

            for (size_t i = 0; i < (attach ? size - 1 : size); ++i)
            {
                m_pool.push_back(std::make_shared<worker>(m_io, std::launch::async));
            }

            if (attach)
            {
                auto task = std::make_shared<worker>(m_io, std::launch::deferred);
                m_pool.push_back(task); 

                lock.unlock();
                task->wait();
            }
        }

        void shutdown(bool hard, bool wait) noexcept(true)
        {
            if (wait == false)
            {
                return std::thread([self = shared_from_this(), hard]() { self->shutdown(hard, true); }).detach();
            }

            try
            {
                std::vector<std::shared_ptr<worker>> pool;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);

                    m_work.reset();
                
                    if (hard)
                        m_io->stop();

                    std::swap(pool, m_pool);
                }

                auto id = std::this_thread::get_id();
                for(auto& work : pool)
                {
                    if (!(work->get_id() == id))
                        work->wait();
                }
            }
            catch (const std::exception& e)
            {
                _err_ << e.what();
            }
        }
    };

    size_t m_size;
    std::shared_ptr<boost::asio::io_context> m_io;
    std::shared_ptr<context> m_context;
    std::mutex m_mutex;

    std::shared_ptr<context> free_context() noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto ptr = m_context;
        m_context.reset();

        return ptr;
    }

    std::shared_ptr<context> make_context() noexcept(false)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_context)
            throw std::runtime_error("context already exists");

        m_context = std::make_shared<context>(m_io);

        return m_context;
    }

public:

    reactor(size_t size = std::thread::hardware_concurrency()) noexcept(true)
        : m_size(size)
        , m_io(std::make_shared<boost::asio::io_context>())
    {
    }

    ~reactor() noexcept(true)
    {
        terminate();
    }

    void execute() noexcept(false)
    {
        make_context()->activate(m_size, true);
    }

    void activate() noexcept(false)
    {
        make_context()->activate(m_size, false);
    }

    void terminate(bool wait = false) noexcept(true)
    {
        auto context = free_context();
        if (context)
            context->shutdown(true, wait);
    }

    void complete(bool wait = false) noexcept(true)
    {
        auto context = free_context();
        if (context)
            context->shutdown(false, wait);
    }

    boost::asio::io_context& io() noexcept(true)
    {
        return *m_io;
    }
};

typedef std::shared_ptr<reactor> reactor_ptr;

reactor_ptr shared_reactor() noexcept(true);

}
