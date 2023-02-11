/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "wormhole.h"
#include "buffer.h"
#include "reactor.h"
#include "logger.h"
#include "tubus.h"
#include <list>
#include <boost/asio.hpp>

namespace novemus::wormhole {

class tcp : public std::enable_shared_from_this<tcp>
{
    typedef std::function<void(const boost::system::error_code&)> callback;
    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

    reactor_ptr m_reactor;
    boost::asio::ip::tcp::socket m_socket;
    std::list<std::pair<mutable_buffer, io_callback>> m_rq;
    std::list<std::pair<const_buffer, io_callback>> m_wq;
    std::mutex m_mutex;

    void error(const boost::system::error_code& error)
    {
        std::list<std::pair<mutable_buffer, io_callback>> rq;
        std::list<std::pair<const_buffer, io_callback>> wq;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            boost::system::error_code ec;
            m_socket.close(ec);

            std::swap(rq, m_rq);
            std::swap(wq, m_wq);
        }

        std::for_each(wq.begin(), wq.end(), [this, error](const auto& item)
        {
            item.second(error, 0);
        });

        std::for_each(rq.begin(), rq.end(), [this, error](const auto& item)
        {
            item.second(error, 0);
        });
    }

    void read()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_socket.is_open() && m_rq.size() > 0)
        {
            auto buffer = m_rq.front().first;
            auto handler = m_rq.front().second;

            std::weak_ptr<tcp> weak = shared_from_this();
            m_socket.async_read_some(buffer, [weak, handler](const boost::system::error_code& error, size_t size)
            {
                handler(error, size);
                
                if (auto ptr = weak.lock())
                    ptr->read();
            });

            m_rq.pop_front();
        }
    }

    void write()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_socket.is_open() && m_wq.size() > 0)
        {
            auto buffer = m_wq.front().first;
            auto handler = m_wq.front().second;

            std::weak_ptr<tcp> weak = shared_from_this();
            boost::asio::async_write(m_socket, buffer, [weak, handler](const boost::system::error_code& error, size_t size)
            {
                handler(error, size);
                
                if (auto ptr = weak.lock())
                    ptr->write();
            });

            m_wq.pop_front();
        }
    }

public:

    tcp(reactor_ptr reactor) : m_reactor(reactor), m_socket(m_reactor->io())
    {
    }

    ~tcp()
    {
        error(boost::asio::error::operation_aborted);
        boost::system::error_code ec;
        m_socket.close(ec);
    }

    void async_connect(const tcp_endpoint& ep, const callback& handler)
    {
        m_socket.async_connect(ep, handler);
    }

    boost::asio::ip::tcp::socket& socket()
    {
        return m_socket;
    }

    void async_read(const mutable_buffer& buffer, const io_callback& handler)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_rq.emplace_back(buffer, handler);
        if (m_socket.is_open() && m_rq.size() == 1)
        {
            std::weak_ptr<tcp> weak = shared_from_this();
            m_socket.async_wait(boost::asio::ip::tcp::socket::wait_read, [weak](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (ptr)
                        ptr->error(error);

                    if (error != boost::asio::error::operation_aborted)
                        _err_ << error.message();
                }
                else if (ptr)
                    ptr->read();
            });
        }
    }

    void async_write(const mutable_buffer& buffer, const io_callback& handler)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_wq.emplace_back(buffer, handler);
        if (m_wq.size() == 1)
        {
            std::weak_ptr<tcp> weak = shared_from_this();
            m_socket.async_wait(boost::asio::ip::tcp::socket::wait_write, [weak](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (ptr)
                        ptr->error(error);

                    if (error != boost::asio::error::operation_aborted)
                        _err_ << error.message();
                }
                else if (ptr)
                    ptr->write();
            });
        }
    }
};

typedef std::shared_ptr<tcp> tcp_ptr;

struct packet : public novemus::mutable_buffer
{
    static constexpr size_t header_size = sizeof(uint32_t) + sizeof(uint32_t);

    packet() : novemus::mutable_buffer(header_size)
    {
        std::memset(data(), 0, header_size);
    }

    packet(uint32_t id)
        : novemus::mutable_buffer(header_size)
    {
        set<uint32_t>(0, htonl(id));
        set<uint32_t>(sizeof(uint32_t), 0);
    }

    packet(uint32_t id, const novemus::const_buffer& payload)
        : novemus::mutable_buffer(header_size + payload.size())
    {
        set<uint32_t>(0, htonl(id));
        set<uint32_t>(sizeof(uint32_t), htonl(payload.size()));
        fill(header_size, payload.size(), payload.data());
    }

    uint32_t id() const
    {
        return ntohl(get<uint32_t>(0));
    }

    uint32_t length() const
    {
        return ntohl(get<uint32_t>(sizeof(uint32_t)));
    }

    const_buffer payload() const
    {
        return slice(header_size, length());
    }
};

class engine : public novemus::wormhole::router, public std::enable_shared_from_this<engine>
{
    friend class importer;
    friend class exporter;

    reactor_ptr m_reactor;
    novemus::tubus::channel_ptr m_tunnel;
    std::map<uint32_t, tcp_ptr> m_bunch;
    uint32_t m_top;
    std::mutex m_mutex;

    void listen_tunnel()
    {
        packet pack;
        std::weak_ptr<engine> weak = shared_from_this();
        m_tunnel->read(pack, [weak, pack](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                {
                    _err_ << error.message();
                    
                    if (ptr)
                        ptr->cancel();
                }
            }
            else if (size < pack.size())
            {
                _err_ << "can't read tunnel";
                
                if (ptr)
                    ptr->cancel();
            }
            else if (ptr)
            {
                auto id = pack.id();
                if (pack.length() == 0)
                {
                    ptr->notify_client(id);
                    ptr->listen_tunnel();
                }
                else
                {
                    ptr->read_tunnel(id, pack.length());
                }
            }
        });
    }

    void read_client(uint32_t id)
    {
        auto client = fetch_client(id);
        if (!client)
        {
            _err_ << "client " << id << " not found";
            return;
        }

        mutable_buffer data(1024 * 1024);
        std::weak_ptr<engine> weak = shared_from_this();
        client->async_read(data, [weak, id, data](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                    _err_ << "client " << id << ": " << error.message();
                
                if (ptr)
                {
                    ptr->notify_tunnel(id);
                    ptr->remove_client(id);
                }
            }
            else if (ptr)
            {
                if (size > 0)
                    ptr->write_tunnel(id, data.slice(0, size));

                ptr->read_client(id);
            }
        });
    }

    void write_client(uint32_t id, const const_buffer& data)
    {
        auto client = fetch_client(id);
        if (!client)
        {
            _err_ << "client " << id << ": not found";
            return;
        }

        std::weak_ptr<engine> weak = shared_from_this();
        client->async_write(data, [weak, id, data](const boost::system::error_code& error, size_t size)
        {
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                    _err_ << "client " << id << ": " << error.message();
                
                auto ptr = weak.lock();
                if (ptr)
                {
                    ptr->notify_tunnel(id);
                    ptr->remove_client(id);
                }
            }
        });
    }

    void write_tunnel(uint32_t id, const novemus::const_buffer& data)
    {
        packet pack(id, data);
        std::weak_ptr<engine> weak = shared_from_this();
        m_tunnel->write(pack, [weak, pack](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                {
                    _err_ << "client " << pack.id() << ": " << error.message();

                    if (ptr)
                        ptr->cancel();
                }
            }
            else if (size < pack.size())
            {
                _err_ << "client " << pack.id() << ": can't write packet";
                
                if (ptr)
                    ptr->cancel();
            }
        });
    }

    void read_tunnel(uint32_t id, uint32_t size)
    {
        mutable_buffer data(size);
        std::weak_ptr<engine> weak = shared_from_this();
        m_tunnel->read(data, [weak, id, data](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                {
                    _err_ << "client " << id << ": " << error.message();
                    
                    if (ptr)
                        ptr->cancel();
                }
            }
            else if (size < data.size())
            {
                _err_ << "client " << id << ": can't read data";
                
                if (ptr)
                    ptr->cancel();
            }
            else if (ptr)
            {
                ptr->write_client(id, data);
                ptr->listen_tunnel();
            }
        });
    }

    tcp_ptr fetch_client(uint32_t id)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_bunch.find(id);
        return iter != m_bunch.end() ? iter->second : tcp_ptr();
    }

    void remove_client(uint32_t id)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        if (m_bunch.erase(id) != 0)
            _inf_ << "client " << id << " is removed";
    }

    void notify_tunnel(uint32_t id)
    {
        write_tunnel(id, const_buffer());
    }

    virtual void notify_client(uint32_t id) = 0;

public:

    engine(const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret)
        : m_reactor(std::make_shared<novemus::reactor>())
        , m_tunnel(novemus::tubus::create_channel(m_reactor, gateway, faraway, secret))
        , m_top(std::numeric_limits<uint32_t>::max())
    {
        m_tunnel->open();
    }

    void employ() noexcept(true) override
    {
        m_reactor->execute();
    }
    
    void launch() noexcept(true) override
    {
        m_reactor->activate();
    }
    
    void cancel() noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_bunch.clear();
        m_tunnel->close();
        m_reactor->terminate();
    }
};

typedef std::shared_ptr<novemus::wormhole::router> router_ptr;

class importer : public engine
{
    boost::asio::ip::tcp::acceptor m_server;

public:

    importer(const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret)
        : engine(gateway, faraway, secret)
        , m_server(m_reactor->io(), server)
    {
        m_server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    }

    void employ() noexcept(true) override
    {
        connect_tunnel();
        engine::employ();
    }

    void launch() noexcept(true) override
    {
        connect_tunnel();
        engine::launch();
    }

    void cancel() noexcept(true) override
    {
        boost::system::error_code ec;
        m_server.cancel(ec);
        engine::cancel();
    }

private:

    void connect_tunnel()
    {
        std::weak_ptr<importer> weak = std::static_pointer_cast<importer>(shared_from_this());
        m_tunnel->connect([weak](const boost::system::error_code& error)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                {
                    _err_ << error.message();
                
                    if (ptr)
                        ptr->cancel();
                }
            }
            else if (ptr)
            {
                _inf_ << "tunnel is connected";

                ptr->accept_client();
                ptr->listen_tunnel();
            }
        });
    }

    void accept_client()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        uint32_t id = ++m_top;

        _inf_ << "accepting client " << id;

        auto client = std::make_shared<tcp>(m_reactor);
        m_bunch.emplace(id, client);

        std::weak_ptr<importer> weak = std::static_pointer_cast<importer>(shared_from_this());
        m_server.async_accept(client->socket(), [weak, id](const boost::system::error_code& error)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                    _err_ << "client " << id << ": " << error.message();
                
                if (ptr)
                    ptr->remove_client(id);
            }
            else if (ptr)
            {
                _inf_ << "client " << id << " is accepted";

                ptr->notify_tunnel(id);
                ptr->read_client(id);
                ptr->accept_client();
            }
        });
    }

    void notify_client(uint32_t id)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_bunch.erase(id) != 0)
            _inf_ << "client " << id << " is disconnected";
    }
};

class exporter : public engine
{
    boost::asio::ip::tcp::endpoint m_server;

public:

    exporter(const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret)
        : engine(gateway, faraway, secret)
        , m_server(server)
    {
    }

    void employ() noexcept(true) override
    {
        accept_tunnel();
        engine::employ();
    }

    void launch() noexcept(true) override
    {
        accept_tunnel();
        engine::launch();
    }

private:

    void accept_tunnel()
    {
        std::weak_ptr<engine> weak = shared_from_this();
        m_tunnel->accept([weak](const boost::system::error_code& error)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                {
                    _err_ << error.message();
                    
                    if (ptr)
                        ptr->cancel();
                }
            }
            else if (ptr)
            {
                _inf_ << "tunnel is accepted";

                ptr->listen_tunnel();
            }
        });
    }

    void notify_client(uint32_t id) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (id == m_top + 1)
        {
            m_top = id;

            _inf_ << "connecting client " << id;

            auto client = std::make_shared<tcp>(m_reactor);
            m_bunch.emplace(id, client);
            
            std::weak_ptr<engine> weak = shared_from_this();
            client->async_connect(m_server, [weak, id](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (error != boost::asio::error::operation_aborted)
                        _err_ << "client " << id << ": " << error.message();
                    
                    if (ptr)
                    {
                        ptr->notify_tunnel(id);
                        ptr->remove_client(id);
                    }
                }
                else if (ptr)
                {
                    _inf_ << "client " << id << " is connected";

                    ptr->read_client(id);
                }
            });
        }
        else
        {
            if (m_bunch.erase(id) != 0)
                _inf_ << "client " << id << " is disconnected";
        }
    }
};

router_ptr create_exporter(const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(true)
{
    return std::make_shared<exporter>(server, gateway, faraway, secret);
}

router_ptr create_importer(const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(true)
{
    return std::make_shared<importer>(server, gateway, faraway, secret);
}

}
