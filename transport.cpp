#include "udp.h"
#include <map>
#include <mutex>
#include <iostream>
#include <functional>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace tubus { namespace udp {

class connector : public transport, public std::enable_shared_from_this<connector>
{
    std::shared_ptr<reactor> m_reactor;
    boost::asio::ip::udp::socket m_socket;
    std::map<boost::asio::ip::udp::endpoint, std::weak_ptr<receiver>> m_peers;
    buffer_factory m_stock;
    std::mutex m_mutex;

    void on_error(const boost::system::error_code& error)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_peers.begin();
        while (iter != m_peers.end())
        {
            auto ptr = iter->second.lock();
            if (ptr)
            {
                m_reactor->get_io().post(boost::bind(&receiver::mistake, ptr, iter->first, error));
                ++iter;
            }
            else
            {
                iter = m_peers.erase(iter);
            }
        }
    }

    void on_error(const boost::asio::ip::udp::endpoint& peer, const boost::system::error_code& error)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_peers.find(peer);
        if (iter != m_peers.end())
        {
            auto ptr = iter->second.lock();
            if (ptr)
            {
                m_reactor->get_io().post(boost::bind(&receiver::mistake, ptr, peer, error));
            }
            else
            {
                m_peers.erase(iter);
            }
        }
    }

    void on_receive(const boost::asio::ip::udp::endpoint& peer, const mutable_buffer& buffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_peers.find(peer);
        if (iter != m_peers.end())
        {
            auto ptr = iter->second.lock();
            if (ptr)
            {
                m_reactor->get_io().post(boost::bind(&receiver::receive, ptr, peer, buffer));
            }
            else
            {
                m_peers.erase(iter);
            }
        }
    }

    void schedule_receive()
    {
        if (m_socket.is_open())
        {
            auto buffer = m_stock.make_buffer();
            auto peer = std::make_shared<boost::asio::ip::udp::endpoint>();
            std::weak_ptr<connector> weak = shared_from_this();

            m_socket.async_receive_from(boost::asio::buffer(buffer.data(), buffer.size()), *peer, [weak, peer, buffer](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error)
                    {
                        ptr->on_error(*peer, error);
                    }
                    else
                    {
                        ptr->on_receive(*peer, buffer.slice(0, size));
                    }

                    ptr->schedule_receive();
                }
            });
        }
        else
        {
            on_error(boost::asio::error::broken_pipe);
        }
    }

public:

    connector(std::shared_ptr<reactor> reactor)
        : m_reactor(reactor)
        , m_socket(reactor->get_io())
        , m_stock(9992)
    {
    }

    void bind(const boost::asio::ip::udp::endpoint& bind)
    {
        m_socket.open(bind.protocol());
        m_socket.non_blocking(true);
        m_socket.bind(bind);

        schedule_receive();
    }

    ~connector()
    {
        boost::system::error_code ec;
        m_socket.close(ec);
    }

    void subscribe(const boost::asio::ip::udp::endpoint& peer, std::shared_ptr<receiver> receiver) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto res = m_peers.emplace(peer, receiver);
        if (res.second == false)
        {
            auto ptr = res.first->second.lock();

            if (ptr)
            {
                m_reactor->get_io().post(boost::bind(&receiver::mistake, receiver, peer, boost::asio::error::address_in_use));
                return;
            }

            m_peers[peer] = receiver;
        }
    }
        
    void dispatch(const boost::asio::ip::udp::endpoint& peer, const const_buffer& buffer) noexcept(true) override
    {
        if (m_socket.is_open())
        {
            std::weak_ptr<connector> weak = shared_from_this();
            m_socket.async_send_to(boost::asio::buffer(buffer.data(), buffer.size()), peer, [peer, weak](const boost::system::error_code& error, size_t size)
            {
                if (error)
                {
                    auto ptr = weak.lock();
                    if (ptr)
                    {
                        ptr->on_error(peer, error);
                    }
                }
            });
        }
        else
        {
            on_error(peer, boost::asio::error::broken_pipe);
        }
    }
};

std::shared_ptr<transport> create_transport(std::shared_ptr<reactor> reactor, const boost::asio::ip::udp::endpoint& bind) noexcept(false)
{
    static std::mutex s_mutex;
    static std::map<boost::asio::ip::udp::endpoint, std::weak_ptr<connector>> s_pool;

    std::unique_lock<std::mutex> lock(s_mutex);
    std::shared_ptr<connector> transport;

    auto iter = s_pool.find(bind);
    if (iter != s_pool.end())
    {
        transport = iter->second.lock();
        if (!transport)
        {
            s_pool.erase(iter);
        }
    }

    if (!transport)
    {
        transport = std::make_shared<connector>(reactor);
        transport->bind(bind);

        s_pool.emplace(bind, transport);
    }

    return transport;
}

}}
