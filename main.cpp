#include "reactor.h"
#include "tubus.h"
#include <regex>
#include <iostream>
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace novemus { namespace wormhole {

class client : public std::enable_shared_from_this<client>
{
    typedef std::function<void(const boost::system::error_code&)> callback;
    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

    std::shared_ptr<novemus::reactor> m_reactor;
    boost::asio::ip::tcp::socket m_socket;
    std::list<std::pair<mutable_buffer, io_callback>> m_rq;
    std::list<std::pair<const_buffer, io_callback>> m_wq;
    std::mutex m_mutex;

    void error(const boost::system::error_code& error)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        std::for_each(m_wq.begin(), m_wq.end(), [this, error](const auto& item)
        {
            m_reactor->io().post(std::bind(item.second, error, 0));
        });
        m_wq.clear();

        std::for_each(m_rq.begin(), m_rq.end(), [this, error](const auto& item)
        {
            m_reactor->io().post(std::bind(item.second, error, 0));
        });
        m_rq.clear();
    }

    void read()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_socket.is_open() && m_rq.size() > 0)
        {
            auto buffer = m_rq.front().first;
            auto handler = m_rq.front().second;

            std::weak_ptr<client> weak = shared_from_this();
            m_socket.async_read_some(buffer, [weak, handler](const boost::system::error_code& error, size_t size)
            {
                if (auto ptr = weak.lock())
                    ptr->read();

                handler(error, size);
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

            std::weak_ptr<client> weak = shared_from_this();
            boost::asio::async_write(m_socket, buffer, [weak, handler](const boost::system::error_code& error, size_t size)
            {
                if (auto ptr = weak.lock())
                    ptr->write();

                handler(error, size);
            });

            m_wq.pop_front();
        }
    }

public:

    client() : m_reactor(novemus::reactor::shared_reactor()), m_socket(m_reactor->io())
    {
    }

    ~client()
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

    void async_read(const mutable_buffer& buffer, const io_callback& handler)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_rq.emplace_back(buffer, handler);
        if (m_socket.is_open() && m_rq.size() == 1)
        {
            std::weak_ptr<client> weak = shared_from_this();
            m_socket.async_wait(boost::asio::ip::tcp::socket::wait_read, [weak](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (ptr)
                        ptr->error(error);

                    std::cout << "async_read: " << error.message() << std::endl;
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
            std::weak_ptr<client> weak = shared_from_this();
            m_socket.async_wait(boost::asio::ip::tcp::socket::wait_write, [weak](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (ptr)
                        ptr->error(error);

                    std::cout << "async_write: " << error.message() << std::endl;
                }
                else if (ptr)
                    ptr->write();
            });
        }
    }
};

class router : public std::enable_shared_from_this<router>
{
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

    std::shared_ptr<novemus::reactor> m_reactor;
    std::shared_ptr<novemus::tubus::channel> m_tunnel;
    std::map<uint32_t, std::shared_ptr<client>> m_bunch;
    std::mutex m_mutex;

    friend class importer;
    friend class exporter;

    void listen()
    {
        packet pack;
        std::weak_ptr<router> weak = shared_from_this();
        m_tunnel->read(pack, [weak, pack](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (ptr)
                    ptr->error(error);

                std::cout << "listen_tunnel: " << error.message() << std::endl;
            }
            else if (size < pack.size())
            {
                if (ptr)
                    ptr->error(boost::asio::error::message_size);
                    
                std::cout << "listen_tunnel: can't read tunnel" << std::endl;
            }
            else if (ptr)
            {
                auto id = pack.id();
                if (id != std::numeric_limits<uint32_t>::max())
                {
                    if (pack.length() == 0)
                        ptr->route(id);
                    else
                        ptr->read_tunnel(id, pack.length());
                }

                ptr->listen();
            }
        });
    }

    void read_client(uint32_t id)
    {
        auto client = fetch_client(id);
        if (!client)
        {
            std::cout << "read_client " << id << ": connection not found" << std::endl;
            return;
        }

        novemus::mutable_buffer data(1024 * 1024);
        std::weak_ptr<router> weak = shared_from_this();
        client->async_read(data, [weak, id, data](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (ptr)
                    ptr->error(id, error);

                std::cout << "read_client " << id << ": " << error.message() << std::endl;
            }
            else if (ptr)
            {
                if (size > 0)
                    ptr->write_tunnel(id, data.slice(0, size));

                ptr->read_client(id);
            }
        });
    }

    void write_client(uint32_t id, const novemus::const_buffer& data)
    {
        auto client = fetch_client(id);
        if (!client)
        {
            std::cout << "write_client " << id << ": connection not found" << std::endl;
            return;
        }

        std::weak_ptr<router> weak = shared_from_this();
        client->async_write(data, [weak, id, data](const boost::system::error_code& error, size_t size)
        {
            if (error && error != boost::asio::error::operation_aborted)
            {
                auto ptr = weak.lock();
                if (ptr)
                    ptr->error(id, error);

                std::cout << "write_client " << id << ": " << error.message() << std::endl;
            }
        });
    }

    void write_tunnel(uint32_t id, const novemus::const_buffer& data)
    {
        packet pack(id, data);
        std::weak_ptr<router> weak = shared_from_this();
        m_tunnel->write(pack, [weak, pack](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                {
                    if (ptr)
                        ptr->error(error);

                    std::cout << "write_tunnel " << pack.id() << ": " << error.message() << std::endl;
                }
            }
            else if (size < pack.size())
            {
                if (ptr)
                    ptr->error(pack.id(), boost::asio::error::message_size);

                std::cout << "write_tunnel " << pack.id() << ": can't write packet" << std::endl;
            }
        });
    }

    void read_tunnel(uint32_t id, uint32_t size)
    {
        novemus::mutable_buffer data(size);
        std::weak_ptr<router> weak = shared_from_this();
        m_tunnel->read(data, [weak, id, data](const boost::system::error_code& error, size_t size)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (error != boost::asio::error::operation_aborted)
                {
                    if (ptr)
                        ptr->error(error);

                    std::cout << "read_tunnel " << id << ": " << error.message() << std::endl;
                }
            }
            else if (size < data.size())
            {
                if (ptr)
                    ptr->error(id, boost::asio::error::message_size);

                std::cout << "read_tunnel " << id << ": can't read data" << std::endl;
            }
            else if (ptr)
            {
                ptr->write_client(id, data);
            }
        });
    }

    void error(uint32_t id, const boost::system::error_code& error)
    {
        write_tunnel(id, novemus::const_buffer());

        std::unique_lock<std::mutex> lock(m_mutex);
        m_bunch.erase(id);
    }

    void error(const boost::system::error_code& error)
    {
        m_tunnel->close();

        std::unique_lock<std::mutex> lock(m_mutex);
        m_bunch.clear();
    }

    std::shared_ptr<client> fetch_client(uint32_t id)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_bunch.find(id);
        return iter != m_bunch.end() ? iter->second : std::shared_ptr<client>();
    }

    virtual void route(uint32_t id = std::numeric_limits<uint32_t>::max()) = 0;

public:

    router(std::shared_ptr<novemus::tubus::channel> tunnel)
        : m_reactor(novemus::reactor::shared_reactor())
        , m_tunnel(tunnel)
    {
    }

    virtual void start() = 0;
};

class importer : public router
{
    boost::asio::ip::tcp::acceptor m_server;

public:

    static std::shared_ptr<novemus::wormhole::router> create(std::shared_ptr<novemus::tubus::channel> tunnel, const boost::asio::ip::tcp::endpoint& server)
    {
        return std::make_shared<novemus::wormhole::importer>(tunnel, server);
    }

    importer(std::shared_ptr<novemus::tubus::channel> tunnel, const boost::asio::ip::tcp::endpoint& server)
        : router(tunnel)
        , m_server(m_reactor->io(), server)
    {
        m_server.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    }

    void start() override
    {
        std::weak_ptr<router> weak = shared_from_this();
        m_tunnel->accept([weak](const boost::system::error_code& error)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (ptr)
                    ptr->error(error);

                std::cout << "start: " << error.message() << std::endl;
            }
            else if (ptr)
            {
                ptr->route();
                ptr->listen();
            }
        });

        m_reactor->io().run();
    }

private:

    void route(uint32_t id)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (id == std::numeric_limits<uint32_t>::max())
        {
            auto iter = m_bunch.rbegin();
            id = iter != m_bunch.rend() ? (iter->first + 1) : 0;

            auto client = std::make_shared<novemus::wormhole::client>();
            m_bunch.emplace(id, client);

            std::weak_ptr<router> weak = shared_from_this();
            m_server.async_accept(client->socket(), [weak, id](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (ptr)
                        ptr->error(id, error);

                    std::cout << "accept_facade " << id << ": " << error.message() << std::endl;
                }
                else if (ptr)
                {
                    ptr->write_tunnel(id, novemus::const_buffer());
                    ptr->read_client(id);
                    ptr->route();
                }
            });
        }
        else
            m_bunch.erase(id);
    }
};

class exporter : public router
{
    boost::asio::ip::tcp::endpoint m_server;

public:

    static std::shared_ptr<novemus::wormhole::router> create(std::shared_ptr<novemus::tubus::channel> tunnel, const boost::asio::ip::tcp::endpoint& server)
    {
        return std::make_shared<novemus::wormhole::exporter>(tunnel, server);
    }

    exporter(std::shared_ptr<novemus::tubus::channel> tunnel, const boost::asio::ip::tcp::endpoint& server)
        : router(tunnel)
        , m_server(server)
    {
    }

    void start() override
    {
        std::weak_ptr<router> weak = shared_from_this();
        m_tunnel->connect([weak](const boost::system::error_code& error)
        {
            auto ptr = weak.lock();
            if (error)
            {
                if (ptr)
                    ptr->error(error);

                std::cout << "start: " << error.message() << std::endl;
            }
            else if (ptr)
            {
                ptr->listen();
            }
        });

        m_reactor->io().run();
    }

private:

    void route(uint32_t id) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_bunch.find(id);
        if (iter == m_bunch.end())
        {
            auto client = std::make_shared<novemus::wormhole::client>();
            m_bunch.emplace(id, client);
            
            std::weak_ptr<router> weak = shared_from_this();
            client->async_connect(m_server, [weak, id](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (ptr)
                        ptr->error(id, error);

                    std::cout << "warn_client " << id << ": " << error.message() << std::endl;
                }
                else if (ptr)
                {
                    ptr->read_client(id);
                }
            });
        }
        else
            m_bunch.erase(iter);
    }
};

}}

template<class address> address parse_address(const std::string& str)
{
    std::smatch match;
    if (std::regex_search(str, match, std::regex("(\\w+://)?(.+):(.*)")))
    {
        return address(boost::asio::ip::make_address(match[2].str()), boost::lexical_cast<uint16_t>(match[3].str()));
    }
    return address();
}

int main(int argc, char *argv[])
{
    boost::program_options::options_description desc("wormhole options");
    desc.add_options()
        ("help", "produce help message")
        ("purpose", boost::program_options::value<std::string>()->required(), "wormhole tunnel usage: <expose|obtain>")
        ("service", boost::program_options::value<std::string>()->required(), "endpoint of exposed/obtained service: <ip:port>")
        ("gateway", boost::program_options::value<std::string>()->required(), "local endpoint of wormhole tunnel: <ip:port>")
        ("faraway", boost::program_options::value<std::string>()->required(), "remote endpoint of wormhole tunnel: <ip:port>")
        ("obscure", boost::program_options::value<uint64_t>()->default_value(0), "pre-shared key to obscure wormhole tunnel: <number>");

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    try
    {
        boost::program_options::notify(vm);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        std::cout << desc << std::endl;
        return 1;
    }

    boost::program_options::notify(vm);

    try
    {
        auto service = parse_address<boost::asio::ip::tcp::endpoint>(vm["service"].as<std::string>());
        auto gateway = parse_address<boost::asio::ip::udp::endpoint>(vm["gateway"].as<std::string>());
        auto faraway = parse_address<boost::asio::ip::udp::endpoint>(vm["faraway"].as<std::string>());
        auto tubus = novemus::tubus::create_channel(gateway, faraway, vm["obscure"].as<uint64_t>());

        auto router = vm["purpose"].as<std::string>() == "obtain"
                    ? novemus::wormhole::importer::create(tubus, service)
                    : novemus::wormhole::exporter::create(tubus, service);

        router->start();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}
