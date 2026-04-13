/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <wormhole/wormhole.h>
#include <wormhole/logger.h>
#include <wormhole/tunnel.h>
#include <tubus/buffer.h>
#include <list>
#include <map>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>

namespace wormhole {

std::string criteria::to_string(const criteria& qos) noexcept(false)
{
    std::string str;
    switch(qos.proto)
    {
        case protocol::udp:
            str = "udp";
            break;
        case protocol::tcp:
            str = "tcp";
            break;
        case protocol::ssl:
            str = "ssl";
            break;
        case protocol::any:
            str = "any";
            break;
        default:
            throw std::runtime_error("unknown value of protocol");
    }

    str += ":";

    switch(qos.role)
    {
        case schema::server:
            str += "server";
            break;
        case schema::client:
            str += "client";
            break;
        case schema::mutual:
            str += "mutual";
            break;
        case schema::either:
            str += "either";
            break;
        default:
            throw std::runtime_error("unknown value of schema");
    }

    return str;
}

criteria criteria::from_string(const std::string& str) noexcept(false)
{
    criteria qos;

    size_t delim = str.find_last_of(':');
    auto proto = str.substr(0, delim);
    auto role = delim != std::string_view::npos ? str.substr(delim + 1) : "";

    if (proto == "ssl")
        qos.proto = protocol::ssl;
    else if (proto == "tcp")
        qos.proto = protocol::tcp;
    else if (proto == "udp")
        qos.proto = protocol::udp;
    else if (proto == "any")
        qos.proto = protocol::any;
    else
        throw std::runtime_error("can't parse protocol");

    if (role == "server")
        qos.role = schema::server;
    else if (role == "client")
        qos.role = schema::client;
    else if (role == "either")
        qos.role = schema::either;
    else if (role == "mutual")
        qos.role = schema::mutual;
    else
        throw std::runtime_error("can't parse schema");

    return qos;
}

std::string endpoint::to_string(const endpoint& ep) noexcept(false)
{
    if (ep.address.is_v6())
        return "[" + ep.address.to_string() + "]:" + std::to_string(ep.port);
    return ep.address.to_string() + ":" + std::to_string(ep.port);
}

endpoint endpoint::from_string(const std::string& str) noexcept(false)
{
    size_t delim = str.find_last_of(':');

    if (delim == std::string_view::npos)
        throw std::runtime_error("can't parse endpoint");

    auto addr = str.substr(0, delim);
    if (addr.size() > 0 && addr.front() == '[')
        addr = addr.substr(1);
    if (addr.size() > 0 && addr.back() == ']')
        addr.pop_back();

    return endpoint {
        boost::asio::ip::make_address(addr),
        boost::lexical_cast<uint16_t>(str.substr(delim + 1))
    };
}

std::istream& operator>>(std::istream& is, endpoint& ep) noexcept(false)
{
    std::string str;
    is >> str;
    ep = endpoint::from_string(str);
    return is;
}

std::ostream& operator<<(std::ostream& os, const endpoint& ep) noexcept(false)
{
    return os << endpoint::to_string(ep);
}

bool operator==(const endpoint& lhs, const endpoint& rhs) noexcept(true)
{
    return lhs.address == rhs.address && lhs.port == rhs.port;
}

bool operator!=(const endpoint& lhs, const endpoint& rhs) noexcept(true)
{
    return !(lhs == rhs);
}

std::istream& operator>>(std::istream& is, criteria& qos) noexcept(false)
{
    std::string str;
    is >> str;
    qos = criteria::from_string(str);
    return is;
}

std::ostream& operator<<(std::ostream& os, const criteria& qos) noexcept(false)
{
    return os << criteria::to_string(qos);
}

bool operator==(const criteria& lhs, const criteria& rhs) noexcept(true)
{
    return lhs.proto == rhs.proto && lhs.role == rhs.role;
}

bool operator!=(const criteria& lhs, const criteria& rhs) noexcept(true)
{
    return !(lhs == rhs);
}

class tcp_client : public std::enable_shared_from_this<tcp_client>
{
    typedef std::function<void(const boost::system::error_code&)> callback;
    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

    boost::asio::io_context& m_io;
    boost::asio::ip::tcp::socket m_socket;
    std::list<std::pair<tubus::mutable_buffer, io_callback>> m_rq;
    std::list<std::pair<tubus::const_buffer, io_callback>> m_wq;
    std::mutex m_mutex;

    void error(const boost::system::error_code& ec)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        std::for_each(m_wq.begin(), m_wq.end(), [this, ec](const auto& item)
        {
            boost::asio::post(m_io, std::bind(item.second, ec, 0));
        });
        m_wq.clear();

        std::for_each(m_rq.begin(), m_rq.end(), [this, ec](const auto& item)
        {
            boost::asio::post(m_io, std::bind(item.second, ec, 0));
        });
        m_rq.clear();
    }

    void next_read(bool pop = true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (pop)
            m_rq.pop_front();

        if (m_socket.is_open() && m_rq.size() > 0)
        {
            auto buffer = m_rq.front().first;
            auto handler = m_rq.front().second;

            m_socket.async_read_some(buffer, [this, weak = weak_from_this(), handler](const boost::system::error_code& ec, size_t size)
            {
                handler(ec, size);
                
                if (auto ptr = weak.lock())
                    next_read();
            });
        }
    }

    void next_write(bool pop = true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (pop)
            m_wq.pop_front();

        if (m_socket.is_open() && m_wq.size() > 0)
        {
            auto buffer = m_wq.front().first;
            auto handler = m_wq.front().second;

            boost::asio::async_write(m_socket, buffer, [this, weak = weak_from_this(), handler](const boost::system::error_code& ec, size_t size)
            {
                handler(ec, size);
                
                if (auto ptr = weak.lock())
                    next_write();
            });
        }
    }

public:

    tcp_client(boost::asio::io_context& io) : m_io(io), m_socket(io)
    {
    }

    ~tcp_client()
    {
        error(boost::asio::error::operation_aborted);
    }

    void async_connect(const boost::asio::ip::tcp::endpoint& ep, const callback& handler)
    {
        m_socket.async_connect(ep, handler);
    }

    boost::asio::ip::tcp::socket& socket()
    {
        return m_socket;
    }

    void async_read(const tubus::mutable_buffer& buffer, const io_callback& handler)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_rq.emplace_back(buffer, handler);
        if (m_socket.is_open() && m_rq.size() == 1)
        {
            m_socket.async_wait(boost::asio::ip::tcp::socket::wait_read, [this, weak = weak_from_this()](const boost::system::error_code& ec)
            {
                auto ptr = weak.lock();
                if (ec)
                {
                    if (ptr)
                        error(ec);

                    if (ec != boost::asio::error::operation_aborted)
                        _err_ << ec.message();
                }
                else if (ptr)
                    next_read(false);
            });
        }
    }

    void async_write(const tubus::const_buffer& buffer, const io_callback& handler)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_wq.emplace_back(buffer, handler);
        if (m_socket.is_open() && m_wq.size() == 1)
        {
            m_socket.async_wait(boost::asio::ip::tcp::socket::wait_write, [this, weak = weak_from_this()](const boost::system::error_code& ec)
            {
                auto ptr = weak.lock();
                if (ec)
                {
                    if (ptr)
                        error(ec);

                    if (ec != boost::asio::error::operation_aborted)
                        _err_ << ec.message();
                }
                else if (ptr)
                    next_write(false);
            });
        }
    }
};

typedef std::shared_ptr<tcp_client> client_ptr;

class router_base : public router, public std::enable_shared_from_this<router_base>
{
    friend class importer;
    friend class exporter;

    boost::asio::io_context& m_io;
    std::shared_ptr<tunnel> m_tunnel;
    std::map<uint32_t, client_ptr> m_bunch;
    uint32_t m_top;
    std::mutex m_mutex;

    void listen_tunnel() noexcept(true)
    {
        packet_ptr packet(std::make_shared<packet>());
        m_tunnel->receive(packet, [this, weak = weak_from_this(), packet](const boost::system::error_code& ec)
        {
            auto ptr = weak.lock();
            if (ec)
            {
                _err_ << ec.message();

                if (ptr)
                    cancel();
            }
            else if (ptr)
            {
                if (packet->capacity() == 0)
                    about_client(packet->id());
                else
                    write_client(packet);

                listen_tunnel();
            }
        });
    }

    void write_tunnel(packet_ptr packet) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_tunnel->send(packet, [this, weak = weak_from_this(), packet](const boost::system::error_code& ec)
        {
            auto ptr = weak.lock();
            if (ec)
            {
                _err_ << ec.message() << ", " << "client=" << packet->id();

                if (ptr)
                {
                    about_client(packet->id());
                    cancel();
                }
            }
        });

        if (packet->capacity() > 0)
        {
            lock.unlock();
            listen_client(packet->id());
        }
    }

    void listen_client(uint32_t id) noexcept(true)
    {
        auto client = fetch_client(id);
        if (!client)
        {
            _err_ << "client " << id << " is not found";
            return;
        }

        packet_ptr pack(std::make_shared<packet>(id, true));
        client->async_read(pack->payload(), [this, weak = weak_from_this(), pack](const boost::system::error_code& ec, size_t size)
        {
            auto ptr = weak.lock();
            if (ec)
            {
                if (ec != boost::asio::error::operation_aborted)
                    _err_ << ec.message() << ", " << "client=" << pack->id();

                if (ptr)
                {
                    write_tunnel(std::make_shared<packet>(pack->id(), false));
                    remove_client(pack->id());
                }
            }
            else if (ptr)
            {
                if (size > packet::max_payload_size)
                {
                    _err_ << "wrong data size, client=" << pack->id();

                    write_tunnel(std::make_shared<packet>(pack->id(), false));
                    remove_client(pack->id());
                }
                else if (size > 0)
                {
                    pack->shrink(size);
                    write_tunnel(pack);
                }
                else
                    listen_client(pack->id());
            }
        });
    }

    void write_client(packet_ptr pack) noexcept(true)
    {
        auto client = fetch_client(pack->id());
        if (!client)
        {
            _err_ << "client " << pack->id() << " is not found";
            return;
        }

        client->async_write(pack->payload(), [this, weak = weak_from_this(), pack](const boost::system::error_code& ec, size_t size)
        {
            auto ptr = weak.lock();
            if (ec)
            {
                if (ec != boost::asio::error::operation_aborted)
                    _err_ << ec.message() << ", " << "client=" << pack->id();

                if (ptr)
                {
                    write_tunnel(std::make_shared<packet>(pack->id(), false));
                    remove_client(pack->id());
                }
            }
            else if (size < pack->capacity())
            {
                _err_ << "can't write data, client=" << pack->id();
                
                if (ptr)
                {
                    write_tunnel(std::make_shared<packet>(pack->id(), false));
                    remove_client(pack->id());
                }
            }
        });
    }

    client_ptr fetch_client(uint32_t id) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_bunch.find(id);
        return iter != m_bunch.end() ? iter->second : client_ptr();
    }

    void remove_client(uint32_t id) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        if (m_bunch.erase(id) != 0)
            _inf_ << "client " << id << " is removed";
    }

    virtual void about_client(uint32_t id) noexcept(true) = 0;

public:

    router_base(boost::asio::io_context& io, std::shared_ptr<tunnel> tunnel)
        : m_io(io)
        , m_tunnel(tunnel)
        , m_top(std::numeric_limits<uint32_t>::max())
    {
    }

    void cancel() noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_tunnel->close([](const boost::system::error_code& ec)
        {
            if (ec && ec != boost::asio::error::interrupted && ec != boost::asio::error::in_progress)
                _err_ << ec.message();
        });

        boost::system::error_code ec;
        for (auto& item : m_bunch)
            item.second->socket().close(ec);
    }
};

class importer : public router_base
{
    boost::asio::ip::tcp::acceptor m_server;

public:

    importer(boost::asio::io_context& io, const boost::asio::ip::tcp::endpoint& service, std::shared_ptr<tunnel> tunnel)
        : router_base(io, tunnel)
        , m_server(io, service, true)
    {
    }

    void launch() noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        std::weak_ptr<importer> weak = std::static_pointer_cast<importer>(shared_from_this());
        m_tunnel->open([this, weak](const boost::system::error_code& ec)
        {
            auto ptr = weak.lock();
            if (ec)
            {
                _err_ << ec.message();

                if (ptr)
                    cancel();
            }
            else if (ptr)
            {
                _inf_ << "tunnel is connected";

                accept_client();
                listen_tunnel();
            }
        });
    }

    void cancel() noexcept(true) override
    {
        router_base::cancel();

        std::unique_lock<std::mutex> lock(m_mutex);
        boost::system::error_code ec;
        m_server.cancel(ec);
        m_server.close(ec);
    }

private:

    void accept_client() noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_server.is_open())
            return;

        uint32_t id = ++m_top;

        _inf_ << "accepting client " << id;

        auto client = std::make_shared<tcp_client>(m_io);
        m_bunch.emplace(id, client);

        std::weak_ptr<importer> weak = std::static_pointer_cast<importer>(shared_from_this());
        m_server.async_accept(client->socket(), [this, weak, client, id](const boost::system::error_code& ec)
        {
            auto ptr = weak.lock();
            if (ec)
            {
                if (ec != boost::asio::error::operation_aborted)
                    _err_ << ec.message() << ", " << "client=" << id;

                if (ptr)
                    remove_client(id);
            }
            else if (ptr)
            {
                _inf_ << "client " << id << " is accepted, endpoint=" << client->socket().remote_endpoint();

                write_tunnel(std::make_shared<packet>(id, false));
                listen_client(id);
                accept_client();
            }
        });
    }

    void about_client(uint32_t id) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_bunch.erase(id) != 0)
            _inf_ << "client " << id << " is disconnected";
    }
};

class exporter : public router_base
{
    boost::asio::ip::tcp::endpoint m_service;

public:

    exporter(boost::asio::io_context& io, const boost::asio::ip::tcp::endpoint& service, std::shared_ptr<tunnel> tunnel)
        : router_base(io, tunnel)
        , m_service(service)
    {
    }

    void launch() noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_tunnel->open([this, weak = weak_from_this()](const boost::system::error_code& ec)
        {
            auto ptr = weak.lock();
            if (ec)
            {
                _err_ << ec.message();

                if (ptr)
                    cancel();
            }
            else if (ptr)
            {
                _inf_ << "tunnel is connected";

                listen_tunnel();
            }
        });
    }

private:

    void about_client(uint32_t id) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (id == m_top + 1)
        {
            m_top = id;

            _inf_ << "connecting client " << id;

            auto client = std::make_shared<tcp_client>(m_io);
            m_bunch.emplace(id, client);

            client->async_connect(m_service, [this, weak = weak_from_this(), client, id](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (error != boost::asio::error::operation_aborted)
                        _err_ << "client " << id << ": " << error.message();

                    if (ptr)
                    {
                        write_tunnel(std::make_shared<packet>(id, false));
                        remove_client(id);
                    }
                }
                else if (ptr)
                {
                    _inf_ << "client " << id << " is connected, endpoint=" << client->socket().local_endpoint();

                    listen_client(id);
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

std::shared_ptr<tunnel> create_tunnel(bool exporter, boost::asio::io_context& io, const endpoint& gateway, const endpoint& faraway, const criteria& qos, const security& defense) noexcept(false)
{
    bool accept = qos.role == schema::server || exporter && qos.role == schema::either;
    bool server = qos.role == schema::server || exporter && qos.role != schema::client;

    if (qos.proto == protocol::ssl)
        return create_ssl_tunnel(io, gateway, faraway, accept, server, defense.crypto);
    if (qos.proto == protocol::tcp)
        return create_tcp_tunnel(io, gateway, faraway, accept, defense.secret);
    return create_udp_tunnel(io, gateway, faraway, accept, defense.secret);
}

std::shared_ptr<router> create_exporter(
    boost::asio::io_context& io, const endpoint& service, const endpoint& gateway, const endpoint& faraway, const criteria& qos, const security& defense) noexcept(false)
{   
    return std::make_shared<exporter>(io, service, create_tunnel(true, io, gateway, faraway,qos, defense));
}

std::shared_ptr<router> create_importer(
    boost::asio::io_context& io, const endpoint& service, const endpoint& gateway, const endpoint& faraway, const criteria& qos, const security& defense) noexcept(false)
{
    return std::make_shared<importer>(io, service, create_tunnel(false, io, gateway, faraway, qos, defense));
}

}
