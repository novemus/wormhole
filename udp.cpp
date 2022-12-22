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
    class channel_pool
    {
        typedef std::map<boost::asio::ip::udp::endpoint, socket_ptr> channel_map;
        typedef std::deque<socket_ptr> socket_queue;

        mutable channel_map m_pool;
        mutable socket_queue m_stock;
        mutable std::mutex m_mutex;

    public:

        channel_pool()
        {
        }

        socket_ptr fetch(const boost::asio::ip::udp::endpoint& peer) const
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            auto iter = m_pool.find(peer);
            if (iter == m_pool.end())
                return socket_ptr();

            return iter->second;
        }
        
        bool empty()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_pool.empty();
        }

        bool contains(const boost::asio::ip::udp::endpoint& peer) const
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_pool.find(peer) != m_pool.end();
        }

        void emplace(const boost::asio::ip::udp::endpoint& peer, socket_ptr socket)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            if (m_pool.find(peer) != m_pool.end())
                boost::asio::detail::throw_error(boost::asio::error::address_in_use, "connect");

            m_pool.emplace(peer, socket);
        }

        void stock(socket_ptr socket)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_stock.emplace_back(socket);
        }

        socket_ptr yield(const boost::asio::ip::udp::endpoint& peer)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            if (m_pool.find(peer) != m_pool.end() || m_stock.empty())
                return socket_ptr();

            auto socket = m_stock.front();

            m_stock.pop_front();
            m_pool.emplace(peer, socket);

            return socket;
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

    boost::asio::ip::udp::socket m_socket;
    channel_pool m_pool;

    void transmit_to(const boost::asio::ip::udp::endpoint& peer, mutable_buffer packet)
    {
        if (m_socket.is_open())
        {
            std::weak_ptr<binding> weak = shared_from_this();
            m_socket.async_send_to(packet, peer, [weak, peer, packet](const boost::system::error_code& error, size_t sent)
            {
                auto ptr = weak.lock();

                if (error)
                {
                    std::cout << "async_send_to " << peer << ": " << error.message() << std::endl;
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
                else if (packet.size() < sent)
                {
                    std::cout << "async_send_to " << peer << ": cant send packet " << std::endl;
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
            });
        }
        else
        {
            m_pool.remove(peer);
        }
    }

    void transmit_from(const boost::asio::ip::udp::endpoint& peer, mutable_buffer packet)
    {
        std::weak_ptr<binding> weak = shared_from_this();
        auto forward_to = [&](socket_ptr socket)
        {
            socket->async_send(packet, [weak, peer, packet](const boost::system::error_code& error, size_t sent)
            {
                auto ptr = weak.lock();

                if (error)
                {
                    std::cout << "async_send " << peer << ": " << error.message() << std::endl;
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
                else if (packet.size() < sent)
                {
                    std::cout << "async_send " << peer << ": cant send packet " << std::endl;
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
            });
        };

        auto socket = m_pool.fetch(peer);
        if (socket)
        {
            if (socket->is_open())
            {
                forward_to(socket);
            }
            else
            {
                close(peer);
            }
        }
        else
        {
            auto socket = m_pool.yield(peer);
            if (socket)
            {
                if (socket->is_open())
                {
                    receive_for(peer);
                    forward_to(socket);
                }
                else
                {
                    close(peer);
                }
            }
        }

    }

    void receive_for(const boost::asio::ip::udp::endpoint& peer)
    {
        auto backend = m_pool.fetch(peer);

        if (backend && backend->is_open())
        {
            std::weak_ptr<binding> weak = shared_from_this();
            mutable_buffer packet = mutable_buffer::create(max_udp_packet_size);

            backend->async_receive(packet, [weak, peer, packet](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    std::cout << "async_receive " << peer << ": " << error.message() << std::endl;
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
                else if (ptr)
                {
                    ptr->transmit_to(peer, packet.slice(0, size));
                    ptr->receive_for(peer);
                }
            });
        }
    }

    void receive()
    {
        if (m_socket.is_open())
        {
            std::weak_ptr<binding> weak = shared_from_this();
            endpoint_ptr peer = std::make_shared<boost::asio::ip::udp::endpoint>();
            mutable_buffer packet = mutable_buffer::create(max_udp_packet_size);

            m_socket.async_receive_from(packet, *peer, [weak, peer, packet](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();

                if (error)
                {
                    std::cout << "async_receive_from " << *peer << ": " << error.message() << std::endl;

                    if (ptr)
                    {
                        ptr->close();
                    }
                }
                else
                {
                    if (ptr)
                    {
                        ptr->transmit_from(*peer, packet.slice(0, size));
                        ptr->receive();
                    }
                }
            });
        }
    }

public:

    binding() : m_socket(novemus::reactor::shared_io())
    {
    }

    void close()
    {
        boost::system::error_code ec;
        m_socket.close(ec);

        m_pool.clear();
    }

    void bind(const boost::asio::ip::udp::endpoint& bind)
    {
        m_socket.open(bind.protocol());
        m_socket.bind(bind);

        receive();
    }

    socket_ptr connect(const boost::asio::ip::udp::endpoint& peer) noexcept(false)
    {
        auto self = shared_from_this();

        boost::asio::local::datagram_protocol::socket front(novemus::reactor::shared_io());
        boost::asio::local::datagram_protocol::socket back(novemus::reactor::shared_io());
        boost::asio::local::connect_pair(front, back);

        socket_ptr frontend(new socket(std::move(front)), [self = shared_from_this()](socket* s) { delete s; });
        socket_ptr backend(new socket(std::move(back)));

        m_pool.emplace(peer, backend);

        receive_for(peer);

        return frontend;
    }

    socket_ptr accept() noexcept(false)
    {
        boost::asio::local::datagram_protocol::socket front(novemus::reactor::shared_io());
        boost::asio::local::datagram_protocol::socket back(novemus::reactor::shared_io());
        boost::asio::local::connect_pair(front, back);

        socket_ptr frontend(new socket(std::move(front)), [self = shared_from_this()](socket* s) { delete s; });
        socket_ptr backend(new socket(std::move(back)));

        m_pool.stock(backend);

        return frontend;
    }

    void close(boost::asio::ip::udp::endpoint peer)
    {
        m_pool.remove(peer);
    }
};

socket_ptr connect(const boost::asio::ip::udp::endpoint& peer) noexcept(false)
{
    boost::asio::ip::udp::socket sock(novemus::reactor::shared_io(), peer.protocol());
    
    sock.connect(peer);

    return socket_ptr(new socket(std::move(sock)));
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
        transport = std::make_shared<binding>();
        transport->bind(bind);

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
