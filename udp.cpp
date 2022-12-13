#include "udp.h"
#include "buffer.h"
#include "reactor.h"
#include <map>
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

        channel_map m_pool;
        mutable std::mutex m_mutex;

    public:

        channel_pool()
        {
        }

        socket_ptr fetch(const boost::asio::ip::udp::endpoint& peer) const
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            auto iter = m_pool.find(peer);
            return iter != m_pool.end() ? iter->second : socket_ptr();
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
            m_pool.emplace(peer, socket);
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

    void transmit_to(boost::asio::ip::udp::endpoint peer, const void* data, size_t size)
    {
        if (m_socket.is_open())
        {
            std::weak_ptr<binding> weak = shared_from_this();
            m_socket.async_send_to(boost::asio::buffer(data, size), peer, [weak, peer, size](const boost::system::error_code& error, size_t sent)
            {
                auto ptr = weak.lock();

                if (error)
                {
                    std::cout << "transmit_to " << peer << ": " << error.message() << std::endl;
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
                else if (size < sent)
                {
                    std::cout << "transmit_to " << peer << ": cant send packet " << std::endl;
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

    void transmit_from(boost::asio::ip::udp::endpoint peer, const void* data, size_t size)
    {
        auto socket = m_pool.fetch(peer);

        if (socket && socket->is_open())
        {
            std::weak_ptr<binding> weak = shared_from_this();
            socket->async_send(boost::asio::buffer(data, size), [weak, peer, size](const boost::system::error_code& error, size_t sent)
            {
                auto ptr = weak.lock();

                if (error)
                {
                    std::cout << "resend_from " << peer << ": " << error.message() << std::endl;
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
                else if (size < sent)
                {
                    std::cout << "resend_from " << peer << ": cant send packet " << std::endl;
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
            });
        }
        else
        {
            close(peer);
        }
    }

    void receive_for(boost::asio::ip::udp::endpoint peer)
    {
        auto backend = m_pool.fetch(peer);

        if (backend && backend->is_open())
        {
            std::weak_ptr<binding> weak = shared_from_this();
            mutable_buffer packet = mutable_buffer::create(max_udp_packet_size);

            backend->async_receive(boost::asio::buffer(packet.data(), packet.size()), [weak, peer, packet](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (error)
                {
                    std::cout << "receive_for " << peer << ": " << error.message();
                    if (ptr)
                    {
                        ptr->close(peer);
                    }
                }
                else if (ptr)
                {
                    ptr->transmit_to(peer, packet.data(), size);
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

            m_socket.async_receive_from(boost::asio::buffer(packet.data(), packet.size()), *peer, [packet, peer, weak](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();

                if (error)
                {
                    std::cout << "receive_for " << peer << ": " << error.message();

                    if (ptr)
                    {
                        ptr->close();
                    }
                }
                else
                {
                    if (ptr)
                    {
                        ptr->transmit_from(*peer, packet.data(), size);
                    }
                }

                if (ptr)
                {
                    ptr->transmit_from(*peer, packet.data(), size);
                }

                ptr->receive();
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
        m_socket.non_blocking(true);
        m_socket.bind(bind);

        receive();
    }

    socket_ptr connect(const boost::asio::ip::udp::endpoint& peer) noexcept(false)
    {
        if (m_pool.contains(peer))
            boost::asio::detail::throw_error(boost::asio::error::address_in_use, "connect");

        boost::asio::local::datagram_protocol::socket frontend(novemus::reactor::shared_io());
        boost::asio::local::datagram_protocol::socket backend(novemus::reactor::shared_io());
        
        boost::asio::local::connect_pair(frontend, backend);

        m_pool.emplace(peer, std::make_shared<socket>(std::move(backend)));

        receive_for(peer);

        return std::make_shared<socket>(std::move(frontend));
    }

    void close(boost::asio::ip::udp::endpoint peer)
    {
        m_pool.remove(peer);
    }
};

socket_ptr connect(const boost::asio::ip::udp::endpoint& peer) noexcept(false)
{
    boost::asio::ip::udp::socket sock(novemus::reactor::shared_io(), peer.protocol());
    
    sock.non_blocking(true);
    sock.connect(peer);

    return std::make_shared<socket>(std::move(sock));
}

socket_ptr connect(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer) noexcept(false)
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

    return transport->connect(peer);
}

}}
