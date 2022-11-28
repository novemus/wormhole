#pragma once

#include <memory>
#include <thread>
#include <iostream>
#include <boost/asio.hpp>

namespace tubus {

class reactor
{
    struct context
    {
        boost::asio::io_context io;
        std::unique_ptr<boost::asio::io_context::work> work;
        boost::asio::thread_pool pool;

        void activate(size_t threads) 
        {
            work.reset(new boost::asio::io_context::work(io));

            for (std::size_t i = 0; i < threads; ++i)
            {
                boost::asio::post(pool, [this]() {
                    boost::system::error_code code;
                    io.run(code);
                    if (code)
                        std::cout << code.message() << std::endl;
                });
            }
        }

        void terminate()
        {
            try
            {
                work.reset();
                io.stop();
                pool.join();
            }
            catch (const std::exception& e)
            {
                std::cout << e.what() << std::endl;
            }
        }
    };

    std::shared_ptr<context> m_context;

public:

    reactor(size_t threads = std::thread::hardware_concurrency()) : m_context(std::make_shared<context>())
    {
        m_context->activate(threads);
    }

    ~reactor()
    {
        std::thread([context = m_context]() {
            context->terminate();
        }).detach();
    }

    boost::asio::io_context& get_io()
    {
        return m_context->io;
    }
};

}
