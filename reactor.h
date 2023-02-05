#pragma once

#include "logger.h"
#include <memory>
#include <thread>
#include <boost/asio.hpp>

namespace novemus {

class reactor
{
    struct context
    {
        boost::asio::io_context io;
        boost::asio::thread_pool pool;
        std::unique_ptr<boost::asio::io_context::work> work;

        void activate(size_t concurrency) noexcept(true)
        {
            work.reset(new boost::asio::io_context::work(io));

            for (std::size_t i = 0; i < concurrency; ++i)
            {
                boost::asio::post(pool, [this]()
                {
                    boost::system::error_code code;
                    io.run(code);
                    if (code)
                        _err_ << code.message();
                });
            }
        }

        void terminate() noexcept(true)
        {
            if (work)
            {
                try
                {
                    work.reset();
                    io.stop();
                    pool.join();
                }
                catch (const std::exception& e)
                {
                    _err_ << e.what();
                }
            }
        }

        void attach() noexcept(true)
        {
            boost::system::error_code code;
            io.run(code);
            if (code)
                _err_ << code.message();
        }

        bool active() const noexcept(true)
        {
            return work.get() != 0;
        }
    };

    size_t m_threads;
    std::shared_ptr<context> m_context;

public:

    reactor(size_t threads = std::thread::hardware_concurrency()) noexcept(true)
        : m_threads(threads)
        , m_context(std::make_shared<context>())
    {
    }

    ~reactor() noexcept(true)
    {
        terminate();
    }

    void execute() noexcept(false)
    {
        if (m_context->active())
            boost::asio::detail::throw_error(boost::asio::error::already_started, "execute");

        m_context->activate(m_threads - 1);
        m_context->attach();
    }

    void activate() noexcept(false)
    {
        if (m_context->active())
            boost::asio::detail::throw_error(boost::asio::error::already_started, "activate");

        m_context->activate(m_threads);
    }

    void terminate() noexcept(true)
    {
        if (m_context->active())
        {
            std::thread([context = m_context]() { context->terminate(); }).detach();
        }
    }

    boost::asio::io_context& io() noexcept(true)
    {
        return m_context->io;
    }
};

typedef std::shared_ptr<reactor> reactor_ptr;

reactor_ptr shared_reactor() noexcept(true);

}
