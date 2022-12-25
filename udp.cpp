#include "udp.h"
#include "buffer.h"
#include "reactor.h"
#include <map>
#include <deque>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/local/datagram_protocol.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace novemus { namespace udp {

const size_t max_udp_packet_size = 9992;

class binding : public std::enable_shared_from_this<binding>
{
    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

    class channel : public std::enable_shared_from_this<channel>
    {
        std::shared_ptr<novemus::reactor> m_reactor;
        boost::asio::local::datagram_protocol::socket m_socket;
        boost::asio::io_context::strand m_strand;

        void do_send(const const_buffer& packet, const io_callback& callback)
        {
            if (m_socket.is_open())
            {
                try
                {
                    m_socket.cancel();
                    callback(boost::system::error_code(), m_socket.send(packet));
                }
                catch(const boost::system::system_error& ex)
                {
                    callback(ex.code(), 0);
                }
            }
            else
            {
                callback(boost::asio::error::broken_pipe, 0);
            }
        }

        void do_receive(const mutable_buffer& packet, const io_callback& callback)
        {
            if (m_socket.is_open())
            {
                std::weak_ptr<channel> weak = shared_from_this();
                m_socket.async_receive(packet, [weak, packet, callback](const boost::system::error_code& error, size_t size)
                {
                    if (error == boost::asio::error::operation_aborted)
                    {
                        auto ptr = weak.lock();
                        if (ptr)
                        {
                            ptr->async_receive(packet, callback);
                            return;
                        }
                    }

                    callback(error, size);
                });
            }
            else
            {
                callback(boost::asio::error::broken_pipe, 0);
            }
        }

    public:

        channel() 
            : m_reactor(novemus::reactor::shared_reactor())
            , m_socket(m_reactor->io())
            , m_strand(m_reactor->io())
        {}

        void schedule_send(const const_buffer& packet, const io_callback& callback)
        {
            std::weak_ptr<channel> weak = shared_from_this();
            m_strand.post([weak, packet, callback]()
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    ptr->do_send(packet, callback);
                }
            });
        }

        void async_receive(const mutable_buffer& packet, const io_callback& callback)
        {
            std::weak_ptr<channel> weak = shared_from_this();
            m_strand.post([weak, packet, callback]()
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    ptr->do_receive(packet, callback);
                }
            });
        }

        void connect(boost::asio::local::datagram_protocol::socket& socket)
        {
            boost::asio::local::connect_pair(m_socket, socket);
        }
    };
    
    typedef std::shared_ptr<channel> channel_ptr;

    class channel_pool
    {
        typedef std::map<boost::asio::ip::udp::endpoint, channel_ptr> channel_map;
        typedef std::deque<channel_ptr> channel_stock;

        channel_map m_pool;
        channel_stock m_stock;
        std::mutex m_mutex;

    public:

        channel_pool()
        {
        }

        channel_ptr fetch(const boost::asio::ip::udp::endpoint& peer)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            auto iter = m_pool.find(peer);

            if (iter == m_pool.end())
                return channel_ptr();

            return iter->second;
        }
        
        channel_ptr yield(const boost::asio::ip::udp::endpoint& peer)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            if (m_pool.find(peer) != m_pool.end() || m_stock.empty())
                return channel_ptr();

            auto ptr = m_stock.front();

            m_stock.pop_front();
            m_pool.emplace(peer, ptr);

            return ptr;
        }

        channel_ptr emplace(const boost::asio::ip::udp::endpoint& peer)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            if (m_pool.find(peer) != m_pool.end())
                boost::asio::detail::throw_error(boost::asio::error::address_in_use, "connect");

            auto ptr = std::make_shared<channel>();
            m_pool.emplace(peer, ptr);

            return ptr;
        }

        channel_ptr stock()
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            auto ptr = std::make_shared<channel>();
            m_stock.emplace_back(ptr);

            return ptr;
        }

        void remove(const boost::asio::ip::udp::endpoint& peer)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_pool.erase(peer);
        }
        
        void clear()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_pool.clear();
        }
    };

    typedef std::shared_ptr<boost::asio::ip::udp::endpoint> endpoint_ptr;

    void do_receive_from_remote()
    {
        if (m_socket.is_open())
        {
            std::weak_ptr<binding> weak = shared_from_this();
            endpoint_ptr peer = std::make_shared<boost::asio::ip::udp::endpoint>();
            mutable_buffer packet = m_store->obtain(max_udp_packet_size);

            m_socket.async_receive_from(packet, *peer, [weak, peer, packet](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    if (error != boost::asio::error::operation_aborted)
                        std::cout << "do_receive_from_remote " << *peer << ": " << error.message() << std::endl;

                    if (ptr)
                    {
                        if (error == boost::asio::error::operation_aborted)
                            ptr->async_receive_from_remote();
                        else
                            ptr->close();
                    }
                }
                else
                {
                    if (ptr && size > 0)
                    {
                        ptr->schedule_send_to_local(*peer, packet.slice(0, size));
                        ptr->async_receive_from_remote();
                    }
                }
            });
        }
    }

    void do_send_to_remote(const boost::asio::ip::udp::endpoint& peer, const const_buffer& packet)
    {
        if (m_socket.is_open())
        {
            try
            {
                m_socket.cancel();

                size_t size = m_socket.send_to(packet, peer);
                if (size < packet.size())
                {
                    close(peer);
                    std::cout << "do_send_to_remote " << peer << ": can't send packet"  << std::endl;
                }
                else
                {
                    async_receive_from_local(peer);
                }
            }
            catch(const boost::system::system_error& ex)
            {
                std::cout << "do_send_to_remote " << peer << ": " << ex.what() << std::endl;
                close(peer);
            }
        }
        else
        {
            close(peer);
        }
    }

    void schedule_send_to_remote(const boost::asio::ip::udp::endpoint& peer, const const_buffer& packet)
    {
        std::weak_ptr<binding> weak = shared_from_this();
        m_strand.post([weak, peer, packet]()
        {
            auto ptr = weak.lock();
            if (ptr)
            {
                ptr->do_send_to_remote(peer, packet);
            }
        });
    }

    void schedule_send_to_local(const boost::asio::ip::udp::endpoint& peer, const const_buffer& packet)
    {
        auto channel = m_pool.fetch(peer);
        if (!channel)
        {
            channel = m_pool.yield(peer);
            async_receive_from_local(peer);
        }

        if (channel)
        {
            std::weak_ptr<binding> weak = shared_from_this();
            channel->schedule_send(packet, [weak, peer, packet](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error)
                    {
                        std::cout << "schedule_send_to_local " << peer << ": " << error.message() << std::endl;
                        ptr->close(peer);
                    }
                    else if (size < packet.size())
                    {
                        std::cout << "schedule_send_to_local " << peer << ": can't send packet " << std::endl;
                        ptr->close(peer);
                    }
                }
            });
        }
    }

    void async_receive_from_local(const boost::asio::ip::udp::endpoint& peer)
    {
        auto channel = m_pool.fetch(peer);
        if (channel)
        {
            std::weak_ptr<binding> weak = shared_from_this();
            mutable_buffer packet = m_store->obtain(max_udp_packet_size);
            channel->async_receive(packet, [weak, peer, packet](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error)
                    {
                        std::cout << "async_receive_from_local " << peer << ": " << error << std::endl;
                        ptr->close(peer);
                    }
                    else if (size > 0)
                    {
                        ptr->schedule_send_to_remote(peer, packet.slice(0, size));
                    }
                }
            });
        }
    }

    void async_receive_from_remote()
    {
        std::weak_ptr<binding> weak = shared_from_this();
        m_strand.post([weak]()
        {
            auto ptr = weak.lock();
            if (ptr)
            {
                ptr->do_receive_from_remote();
            }
        });
    }

public:

    binding(const boost::asio::ip::udp::endpoint& bind)
        : m_reactor(novemus::reactor::shared_reactor())
        , m_socket(m_reactor->io())
        , m_strand(m_reactor->io())
        , m_store(novemus::buffer_factory::shared_factory())
    {
        m_socket.open(bind.protocol());
        m_socket.bind(bind);
    }

    void close() noexcept(true)
    {
        boost::system::error_code ec;
        m_socket.close(ec);

        m_pool.clear();
    }

    void open() noexcept(true)
    {
        async_receive_from_remote();
    }

    socket_ptr connect(const boost::asio::ip::udp::endpoint& peer) noexcept(false)
    {
        boost::asio::local::datagram_protocol::socket frontend(m_reactor->io());

        channel_ptr channel = m_pool.emplace(peer);
        channel->connect(frontend);

        async_receive_from_local(peer);

        return socket_ptr(new socket(std::move(frontend)), [keep = shared_from_this()](socket* s) { delete s; });
    }

    socket_ptr accept() noexcept(false)
    {
        boost::asio::local::datagram_protocol::socket frontend(m_reactor->io());
        
        channel_ptr channel = m_pool.stock();
        channel->connect(frontend);

        return socket_ptr(new socket(std::move(frontend)), [keep = shared_from_this()](socket* s) { delete s; });
    }

    void close(const boost::asio::ip::udp::endpoint& peer)
    {
        m_pool.remove(peer);
    }

private:

    std::shared_ptr<novemus::reactor> m_reactor;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::io_context::strand m_strand;
    std::shared_ptr<buffer_factory> m_store;
    channel_pool m_pool;
};

socket_ptr connect(const boost::asio::ip::udp::endpoint& peer) noexcept(false)
{
    auto reactor = novemus::reactor::shared_reactor();
    boost::asio::ip::udp::socket frontend(reactor->io(), peer.protocol());
    
    frontend.connect(peer);

    return socket_ptr(new socket(std::move(frontend)), [reactor](socket* s) { delete s; });
}

std::shared_ptr<binding> fetch_binding(const boost::asio::ip::udp::endpoint& bind)
{
    static std::mutex s_mutex;
    static std::map<boost::asio::ip::udp::endpoint, std::weak_ptr<binding>> s_pool;

    std::unique_lock<std::mutex> lock(s_mutex);
    std::shared_ptr<binding> transport;

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
        transport = std::make_shared<binding>(bind);
        transport->open();

        s_pool.emplace(bind, transport);
    }

    return transport;
}

socket_ptr connect(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer) noexcept(false)
{
    return fetch_binding(bind)->connect(peer);
}

socket_ptr accept(const boost::asio::ip::udp::endpoint& bind) noexcept(false)
{
    return fetch_binding(bind)->accept();
}

}}
