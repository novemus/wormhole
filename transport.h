#pragma once

#include "buffer.h"
#include <set>
#include <memory>
#include <mutex>
#include <iostream>
#include <functional>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace salt {

struct pipe
{
    virtual ~pipe() {}
    virtual void push(const mutable_buffer& buffer) noexcept(true) = 0;
    virtual bool pull(mutable_buffer& buffer) noexcept(true) = 0;
    virtual void error(const boost::system::error_code& err) noexcept(true) = 0;
};

class udp_binding
{
    struct buffer_factory
    {
        buffer_factory()
        {
        }

        mutable_buffer make_buffer()
        {
            auto it = m_cache.begin();
            while (it != m_cache.end())
            {
                if (it->first.unique())
                {
                    mutable_buffer buff = it->first;
                    std::memset(buff.data(), 0, buff.size());

                    it->second = std::time(0);

                    compress_cache();

                    return buff;
                }
                ++it;
            }

            static constexpr size_t max_udp_data_size = 9992;

            m_cache.emplace_back(mutable_buffer(max_udp_data_size), std::time(0));

            return m_cache.back().first;
        }

    private:

        void compress_cache()
        {
            static const time_t TTL = 30;
            time_t now = std::time(0);

            auto it = m_cache.begin();
            while (it != m_cache.end())
            {
                if (it->first.unique() && it->second + TTL > now)
                    it = m_cache.erase(it);
                else
                    ++it;
            }
        }

        size_t m_buffer_size;
        std::list<std::pair<mutable_buffer, time_t>> m_cache;
    };

    boost::asio::io_context m_io;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::deadline_timer m_timer;
    boost::asio::thread_pool m_worker;
    std::map<boost::asio::ip::udp::endpoint, std::weak_ptr<pipe>> m_pool;
    buffer_factory m_stock;
    std::mutex m_mutex;

    void do_send()
    {
        if (m_socket.is_open())
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            bool sleep = true;

            auto iter = m_pool.begin();
            while (iter != m_pool.end())
            {
                auto pipe = iter->second.lock();
                if (pipe)
                {
                    auto buffer = m_stock.make_buffer();
                    if (pipe->pull(buffer))
                    {
                        sleep = false;
                        m_socket.async_send_to(boost::asio::buffer(buffer.data(), buffer.size()), iter->first, [pipe](const boost::system::error_code& e, size_t s)
                        {
                            if (e)
                                pipe->error(e);
                        });
                    }
                    ++iter;
                }
                else
                {
                    iter = m_pool.erase(iter);
                }
            }

            if (sleep)
            {
                m_timer.expires_from_now(boost::posix_time::milliseconds(100));
                m_timer.async_wait([this](const boost::system::error_code&)
                {
                    std::unique_lock<std::mutex> lock(m_mutex);

                    if (m_socket.is_open())
                        m_io.post(boost::bind(&udp_binding::do_send, this));
                });
            }
            else
            {
                m_io.post(boost::bind(&udp_binding::do_send, this));
            }
        }
    }

    void do_recv()
    {
        if (m_socket.is_open())
        {
            auto buffer = m_stock.make_buffer();

            std::shared_ptr<boost::asio::ip::udp::endpoint> peer = std::make_shared<boost::asio::ip::udp::endpoint>();
            m_socket.async_receive_from(boost::asio::buffer(buffer.data(), buffer.size()), *peer, [=](const boost::system::error_code& err, size_t size)
            {
                std::unique_lock<std::mutex> lock(m_mutex);

                if (err)
                {
                    auto iter = m_pool.begin();
                    while (iter != m_pool.end())
                    {
                        auto pipe = iter->second.lock();
                        if (pipe)
                        {
                            pipe->error(err);
                            ++iter;
                        }
                        else
                        {
                            iter = m_pool.erase(iter);
                        }
                    }

                    return;
                }

                auto iter = m_pool.find(*peer);
                if (iter != m_pool.end())
                {
                    auto pipe = iter->second.lock();
                    if (pipe)
                    {
                        pipe->push(buffer.slice(0, size));
                    }
                    else
                    {
                        m_pool.erase(iter);
                    }
                }

                m_io.post(boost::bind(&udp_binding::do_recv, this));
            });
        }
    }

public:

    udp_binding(const boost::asio::ip::udp::endpoint& bind)
        : m_socket(m_io)
        , m_timer(m_io)
    {
        m_socket.open(bind.protocol());
        m_socket.non_blocking(true);
        m_socket.bind(bind);

        auto task = [&]()
        {
            try
            {
                m_io.run();
            }
            catch(const std::exception& e)
            {
                std::cerr << e.what() << std::endl;
            }
        };

        boost::asio::post(m_worker, task);
        boost::asio::post(m_worker, task);

        m_io.post(boost::bind(&udp_binding::do_send, this));
        m_io.post(boost::bind(&udp_binding::do_recv, this));
    }

    ~udp_binding()
    {
        boost::system::error_code ec;

        m_socket.close(ec);
        m_timer.cancel(ec);

        m_io.stop();
        m_worker.join();
    }

    void connect(const boost::asio::ip::udp::endpoint& peer, std::shared_ptr<pipe> pipe)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto res = m_pool.emplace(peer, pipe);
        if (!res.second)
            throw boost::system::system_error(boost::asio::error::address_in_use);
    }

    void evoke()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        boost::system::error_code ec;
        m_timer.cancel(ec);
    }
};

}
