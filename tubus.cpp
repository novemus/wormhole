#include "packet.h"
#include "tubus.h"
#include "reactor.h"
#include <map>
#include <set>
#include <list>
#include <atomic>
#include <iostream>
#include <random>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/icl/interval_set.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace novemus { namespace tubus {

template<class var> var getenv(const std::string& name, const var& def)
{
    try
    {
        const char *env = std::getenv(name.c_str());
        return env ? boost::lexical_cast<var>(env) : def;
    }
    catch (const boost::bad_lexical_cast& ex)
    {
        std::cout << "getenv: " << ex.what() << std::endl;
    }

    return def;
}

class transport : public novemus::tubus::channel, public std::enable_shared_from_this<transport>
{
    enum state { initial, accepting, connecting, linked, shutting, tearing, finished };

    inline static boost::posix_time::time_duration ping_timeout()
    {
        static boost::posix_time::seconds s_timeout(getenv("TUBUS_PING_TIMEOUT", 30));
        return s_timeout;
    }

    inline static boost::posix_time::time_duration resend_timeout()
    {
        static boost::posix_time::milliseconds s_timeout(getenv("TUBUS_RESEND_TIMEOUT", 100));
        return s_timeout;
    }

    inline static boost::posix_time::time_duration shutdown_timeout()
    {
        static boost::posix_time::milliseconds s_timeout(getenv("TUBUS_SHUTDOWN_TIMEOUT", 1000));
        return s_timeout;
    }

    inline static size_t snippet_flight()
    {
        static size_t s_flight(getenv("TUBUS_SNIPPET_FLIGHT", 10));
        return s_flight;
    }

    struct connector
    {
        static uint16_t make_pin()
        {
            static std::atomic<uint16_t> s_pin;
            uint16_t pin = ++s_pin;
            return pin > 0 ? pin : make_pin();
        };

        connector(boost::asio::io_context& io)
            : m_io(io)
            , m_status(state::initial)
            , m_local(0)
            , m_remote(0)
            , m_pass(boost::posix_time::max_date_time)
            , m_dead(boost::posix_time::max_date_time)
        {
        }

        void error(const boost::system::error_code& err)
        {
            if (on_connect)
            {
                m_io.post(boost::bind(on_connect, err));
                on_connect = 0;
            }

            if (on_shutdown)
            {
                m_io.post(boost::bind(on_shutdown, err));
                on_shutdown = 0;
            }

            m_local = 0;
            m_remote = 0;
            m_pass = boost::posix_time::max_date_time;
            m_dead = boost::posix_time::max_date_time;
            m_jobs.clear();
            m_status = state::finished;
        }

        bool valid(const packet& pack)
        {
            return pack.valid() && m_local != 0 && (m_remote == 0 || m_remote == pack.pin());
        }

        void parse(const packet& pack)
        {
            if (!valid(pack))
                return;

            m_pass = boost::posix_time::microsec_clock::universal_time();

            section sect = pack.payload();
            while (sect.type() != section::list_stub)
            {
                switch (sect.type())
                {
                    case section::link_init:
                    {
                        m_jobs.emplace(section::link_ackn, m_pass);

                        if (m_status == state::accepting)
                        {
                            m_remote = pack.pin();
                        }
                        break;
                    }
                    case section::link_ackn:
                    {
                        if (m_status == state::connecting)
                        {
                            m_status = state::linked;
                            m_remote = pack.pin();

                            m_jobs.erase(section::link_init);
                            m_jobs.emplace(section::ping_shot, m_pass + ping_timeout());

                            m_io.post(boost::bind(on_connect, boost::system::error_code()));
                            on_connect = 0;
                        }
                        break;
                    }
                    case section::tear_init:
                    {
                        m_jobs.emplace(section::tear_ackn, m_pass);

                        if (m_status == state::linked)
                        {
                            m_status = state::tearing;
                        }
                        break;
                    }
                    case section::tear_ackn:
                    {
                        if (m_status == state::shutting)
                        {
                            m_status = state::finished;
                            m_jobs.erase(section::tear_init);

                            m_io.post(boost::bind(on_shutdown, boost::system::error_code()));
                            on_shutdown = 0;
                        }
                        break;
                    } 
                    case section::ping_shot:
                    {
                        m_jobs.emplace(section::ping_ackn, m_pass);
                        break;
                    }
                    case section::ping_ackn:
                    {
                        m_jobs.erase(section::ping_shot);
                        m_jobs.emplace(section::ping_shot, m_pass + ping_timeout());
                        break;
                    }
                    default: break;
                }

                sect = sect.next();
            }
        }

        void imbue(packet& pack)
        {
            pack.sign(packet::packet_sign);
            pack.version(packet::packet_version);
            pack.pin(m_local);

            auto now = boost::posix_time::microsec_clock::universal_time();

            section sect = pack.useless();
            while (sect.size() >= section::header_size)
            {
                auto iter = std::find_if(m_jobs.begin(), m_jobs.end(), [now](const auto& item)
                {
                    return item.second < now;
                });

                if (iter == m_jobs.end())
                    return;
 
                sect.set(iter->first);
                
                if (iter->first == section::link_ackn)
                {
                    m_jobs.erase(iter);

                    if (m_status == state::accepting)
                    {
                        m_status = state::linked;
                        m_io.post(boost::bind(on_connect, boost::system::error_code()));
                        on_connect = 0;
                    }
                }
                else if (iter->first == section::ping_ackn || iter->first == section::tear_ackn)
                {
                    m_jobs.erase(iter);
                }
                else
                {
                    iter->second = now + resend_timeout();
                }

                sect = sect.next();
            }
        }

        state status() const
        {
            return m_status;
        }

        bool pingless() const
        {
            auto now = boost::posix_time::microsec_clock::universal_time();
            return now > m_dead || m_pass + ping_timeout() < now - boost::posix_time::seconds(5);
        }

        bool shutdown(const callback& caller)
        {
            if (m_status != state::linked && m_status != state::tearing)
            {
                boost::system::error_code error = m_status == state::shutting ? 
                    boost::asio::error::in_progress : boost::asio::error::broken_pipe;

                m_io.post(boost::bind(caller, error));
                return false;
            }

            if (m_status == state::tearing)
            {
                m_status = state::finished;
                m_remote = 0;

                m_io.post(boost::bind(caller, boost::system::error_code()));
                return true;
            }

            auto now = boost::posix_time::microsec_clock::universal_time();
            on_shutdown = caller;

            m_dead = now + shutdown_timeout();
            m_status = state::shutting;

            m_jobs.emplace(section::ping_shot, now);
            return true;
        }

        bool connect(const callback& caller)
        {
            if (m_status != state::initial)
            {
                boost::system::error_code ec = m_status == state::connecting ? 
                    boost::asio::error::in_progress : m_status == state::linked ?
                        boost::asio::error::already_connected : boost::asio::error::broken_pipe;

                m_io.post(boost::bind(caller, ec));
                return false;
            }

            m_local = make_pin();
            on_connect = caller;

            m_pass = boost::posix_time::microsec_clock::universal_time();
            m_status = state::connecting;

            m_jobs.emplace(section::link_init, m_pass);
            return true;
        }

        bool accept(const callback& caller)
        {
            if (m_status != state::initial)
            {
                boost::system::error_code ec = m_status == state::accepting ? 
                    boost::asio::error::in_progress : m_status == state::linked ?
                        boost::asio::error::already_connected : boost::asio::error::broken_pipe;

                m_io.post(boost::bind(caller, ec));
                return false;
            }

            m_local = make_pin();
            on_connect = caller;

            m_pass = boost::posix_time::microsec_clock::universal_time();
            m_status = state::accepting;

            return true;
        }

        bool deffered() const
        {
            return !m_jobs.empty();
        }

    private:

        boost::asio::io_context& m_io;
        state m_status;
        uint32_t m_local;
        uint32_t m_remote;
        std::map<section::kind, boost::posix_time::ptime> m_jobs;
        boost::posix_time::ptime m_pass;
        boost::posix_time::ptime m_dead;
        callback on_connect;
        callback on_shutdown;
    };

    struct ostreamer
    {
        ostreamer(boost::asio::io_context& io) : m_io(io)
        {
        }

        void error(const boost::system::error_code& ec)
        {
            auto total = m_flight.empty() ? m_buffer.head() : m_flight.begin()->first;

            auto iter = m_callers.begin();
            while (iter != m_callers.end())
            {
                size_t sent = 0;

                if (total > iter->first - iter->second.second && total < iter->first)
                    sent = iter->first - total;
                else if (total >= iter->first) 
                    sent = iter->second.second;

                m_io.post(boost::bind(iter->second.first, ec, sent));
                ++iter;
            }

            m_callers.clear();
            m_flight.clear();
            m_buffer.clear();
        }

        void parse(const packet& pack)
        {
            auto sect = pack.payload();

            while (sect.type() != section::list_stub)
            {
                if (sect.type() == section::data_ackn)
                {
                    handle hand(sect.value());
                    m_flight.erase(hand.value());

                    auto total = m_flight.empty() ? m_buffer.head() : m_flight.begin()->first;
                    auto iter = m_callers.begin();
                    while (iter != m_callers.end() && iter->first <= total)
                    {
                        m_io.post(boost::bind(iter->second.first, boost::system::error_code(), iter->second.second));
                        iter = m_callers.erase(iter);
                    }
                }

                sect = sect.next();
            }
        }

        void imbue(packet& pack)
        {
            auto now = boost::posix_time::microsec_clock::universal_time();

            auto sect = pack.useless();
            while (sect.size() > snippet::handle_size + section::header_size)
            {
                auto iter = std::find_if(m_flight.begin(), m_flight.end(), [&sect, &now](const auto& item)
                {
                    return item.second.second < now && item.second.first.size() + snippet::handle_size <= sect.size() - section::header_size;
                });

                if (iter == m_flight.end())
                    break;

                iter->second.second = now + resend_timeout();

                snippet snip(sect.value());
                snip.set(iter->first, iter->second.first);

                sect.type(section::data_move);
                sect.length(snip.size());

                sect = sect.next();
            }

            while (m_flight.size() < snippet_flight() && m_buffer.available())
            {
                if (sect.size() <= section::header_size + snippet::handle_size)
                    break;

                snippet snip(sect.value());

                auto handle = m_buffer.head();
                auto fragment = m_buffer.pop(sect.size() - section::header_size - snippet::handle_size);
                
                snip.set(handle, fragment);

                sect.type(section::data_move);
                sect.length(snip.size());

                m_flight.emplace(handle, std::make_pair(fragment, now + resend_timeout()));
                sect = sect.next();
            }
        }

        void append(const const_buffer& buffer, const io_callback& caller)
        {
            m_callers.emplace(m_buffer.push(buffer), std::make_pair(caller, buffer.size()));
        }

        bool deffered() const
        {
            return !m_flight.empty() || m_buffer.available();
        }

    private:

        struct streambuf
        {
            const_buffer pop(uint64_t max)
            {
                static const const_buffer zero("");

                if (m_data.empty())
                    return zero;

                auto top = m_data.front();
                if (top.size() <= max)
                {
                    m_head += top.size();
                    m_data.pop_front();
                    return top;
                }

                m_head += max;
                return m_data.front().pop_front(max);
            }

            uint64_t push(const const_buffer& buf)
            {
                m_tail += buf.size();
                m_data.push_back(buf);
                return m_tail;
            }

            void clear()
            {
                m_data.clear();
                m_head = 0;
                m_tail = 0;
            }

            uint64_t head() const
            {
                return m_head;
            }

            uint64_t tail() const
            {
                return m_tail;
            }

            bool available() const
            {
                return !m_data.empty();
            }

        private:

            uint64_t m_head = 0;
            uint64_t m_tail = 0;
            std::list<const_buffer> m_data;
        };

        typedef std::pair<io_callback, size_t> handler;
        typedef std::pair<const_buffer, boost::posix_time::ptime> fragment;

        boost::asio::io_context& m_io;
        streambuf m_buffer;
        std::map<uint64_t, fragment> m_flight;
        std::map<uint64_t, handler> m_callers;
    };

    struct istreamer
    {
        istreamer(boost::asio::io_context& io)
            : m_io(io)
        {}

        void error(const boost::system::error_code& ec)
        {
            auto iter = m_callers.begin();
            while (iter != m_callers.end())
            {
                m_io.post(boost::bind(iter->second, ec, 0));
                ++iter;
            }

            m_callers.clear();
            m_acks.clear();
            m_buffer.clear();
        }

        void parse(const packet& pack)
        {
            auto sect = pack.payload();

            while (sect.type() != section::list_stub)
            {
                if (sect.type() == section::data_move)
                {
                    snippet snip(sect.value());
                    
                    m_buffer.map(snip.handle(), snip.fragment());
                    m_acks.emplace_back(snip.handle());
                }

                sect = sect.next();
            }

            transmit();
        }

        void imbue(packet& pack)
        {
            auto sect = pack.useless();

            auto iter = m_acks.begin();
            while (iter != m_acks.end() && sect.size() >= section::header_size + handle::handle_size)
            {
                handle hand(sect.value());
                hand.value(*iter);

                sect.type(section::data_ackn);
                sect.length(handle::handle_size);

                iter = m_acks.erase(iter);
                sect = sect.next();
            }
        }

        void append(const mutable_buffer& buf, const io_callback& caller)
        {
            m_callers.push_back(std::make_pair(buf, caller));
            transmit();
        }
    
        bool deffered() const
        {
            return !m_acks.empty();
        }

    private:

        void transmit()
        {
            while (m_buffer.available())
            {
                auto iter = m_callers.begin();
                if (iter == m_callers.end())
                    break;
                
                size_t read = 0;

                while (m_buffer.available() && iter->first.size() > 0)
                {
                    auto buffer = m_buffer.pop(iter->first.size());
                    iter->first.fill(0, buffer.size(), buffer.data());
                    iter->first.crop(buffer.size());
                    read += buffer.size();
                }

                m_io.post(boost::bind(iter->second, boost::system::error_code(), read));
                m_callers.erase(iter);
            }
        }

        struct streambuf
        {
            const_buffer pop(uint64_t max)
            {
                static const const_buffer zero("");

                if (!available())
                    return zero;

                auto top = m_data.begin()->second;
                m_data.erase(m_data.begin());

                if (top.size() <= max)
                {
                    m_head += top.size();
                    return top;
                }

                m_head += max;

                auto ret = top.pop_front(max);
                m_data.emplace(m_head, top);

                return ret;
            }

            void map(uint64_t handle, const const_buffer& buffer)
            {
                m_data.emplace(handle, buffer);
            }

            void clear()
            {
                m_data.clear();
                m_head = 0;
            }

            bool available() const
            {
                return m_data.size() > 0 && m_data.begin()->first == m_head;
            }

        private:

            uint64_t m_head = 0;
            std::map<uint64_t, const_buffer> m_data;
        };

        typedef std::pair<mutable_buffer, io_callback> handler;
        
        boost::asio::io_context& m_io;
        streambuf m_buffer;
        std::list<uint64_t> m_acks;
        std::list<handler> m_callers;
    };

protected:

    void mistake(const boost::system::error_code& ec)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_connector.error(ec);
        m_istreamer.error(ec);
        m_ostreamer.error(ec);
    }

    void feed(const mutable_buffer& buffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (buffer.size() < packet::header_size)
            return;

        packet pack(buffer);

        if (m_secret)
            pack.make_opened(m_secret);

        if (m_connector.valid(pack))
        {
            m_connector.parse(pack);
            
            if (m_connector.status() == state::linked)
            {
                m_istreamer.parse(pack);
                m_ostreamer.parse(pack);
            }

            if (m_connector.status() == state::tearing || m_connector.status() == state::shutting)
            {
                m_istreamer.error(boost::asio::error::connection_aborted);
                m_ostreamer.error(boost::asio::error::connection_aborted);
            }
        }
    }

    void schedule()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.status() == state::initial || m_connector.status() == state::finished)
            return;

        if (m_connector.pingless())
        {
            m_connector.error(boost::asio::error::broken_pipe);
            m_istreamer.error(boost::asio::error::broken_pipe);
            m_ostreamer.error(boost::asio::error::broken_pipe);
            return;
        }

        std::weak_ptr<transport> weak = shared_from_this();
        packet pack(m_store->obtain(packet::max_packet_size));

        m_connector.imbue(pack);

        if (m_connector.status() == state::linked)
        {
            m_istreamer.imbue(pack);
            m_ostreamer.imbue(pack);
        }

        pack.trim();

        if (pack.size() > packet::header_size)
        {
            if (m_secret)
                pack.make_opaque(m_secret);

            boost::system::error_code err;
            m_socket.cancel(err);

            if (err)
            {
                m_connector.error(err);
                m_istreamer.error(err);
                m_ostreamer.error(err);
                return;
            }

            m_socket.async_send(pack, [weak, pack](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error)
                        ptr->mistake(error);
                    else if (pack.size() < size)
                        ptr->mistake(boost::asio::error::message_size);
                    else
                        ptr->schedule();
                }
            });
        }
        else if (m_connector.status() == state::finished)
        {
            m_istreamer.error(boost::asio::error::connection_aborted);
            m_ostreamer.error(boost::asio::error::connection_aborted);
        }
        else
        {
            novemus::mutable_buffer buffer = m_store->obtain(packet::max_packet_size);
            m_socket.async_receive(buffer, [weak, buffer](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error)
                    {
                        if (error != boost::asio::error::operation_aborted)
                            ptr->mistake(error);
                    }
                    else
                    {
                        ptr->feed(buffer.slice(0, size));
                    }
                    ptr->wakeup();
                }
            });

            auto timeout = m_connector.deffered() || m_istreamer.deffered() || m_ostreamer.deffered() ? resend_timeout() : ping_timeout();

            m_timer.expires_from_now(timeout);
            m_timer.async_wait([weak](const boost::system::error_code& error)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error && error != boost::asio::error::operation_aborted)
                        ptr->mistake(error);
                    else
                        ptr->schedule();
                }
            });
        }
    }

    void wakeup()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        boost::system::error_code err;
        m_timer.cancel(err);
    }

public:

    transport(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t secret)
        : m_reactor(novemus::reactor::shared_reactor())
        , m_socket(m_reactor->io(), bind.protocol())
        , m_timer(m_reactor->io())
        , m_connector(m_reactor->io())
        , m_istreamer(m_reactor->io())
        , m_ostreamer(m_reactor->io())
        , m_store(novemus::buffer_factory::shared_factory())
        , m_secret(secret)
    {
        static const size_t SOCKET_BUFFER_SIZE = 1048576;

        m_socket.set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
        m_socket.set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));
        m_socket.set_option(boost::asio::socket_base::reuse_address(true));
        
        m_socket.bind(bind);
        m_socket.connect(peer);
    }

    void shutdown(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.shutdown(handler))
        {
            boost::system::error_code err;
            m_timer.cancel(err);
        }
    }

    void connect(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.connect(handler))
        {
            std::weak_ptr<transport> weak = shared_from_this();
            m_reactor->io().post([weak]()
            {
                auto ptr = weak.lock();
                if (ptr)
                    ptr->schedule();
            });
        }
    }

    void accept(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.accept(handler))
        {
            std::weak_ptr<transport> weak = shared_from_this();
            m_reactor->io().post([weak]()
            {
                auto ptr = weak.lock();
                if (ptr)
                    ptr->schedule();
            });
        }
    }

    void read(const mutable_buffer& buffer, const io_callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto status = m_connector.status();
        if (status != state::linked)
        {
            boost::system::error_code ec = status == state::initial ? boost::asio::error::not_connected : 
                status == state::connecting || state::accepting ? boost::asio::error::try_again : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handler, ec, 0));
            return;
        }

        m_istreamer.append(buffer, handler);
    }

    void write(const const_buffer& buffer, const io_callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto status = m_connector.status();
        if (status != state::linked)
        {
            boost::system::error_code ec = status == state::initial ? boost::asio::error::not_connected : 
                status == state::connecting || state::accepting ? boost::asio::error::try_again : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handler, ec, 0));
            return;
        }

        m_ostreamer.append(buffer, handler);
        boost::system::error_code err;
        m_timer.cancel(err);
    }

private:

    std::shared_ptr<reactor> m_reactor;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::deadline_timer m_timer;
    connector m_connector;
    istreamer m_istreamer;
    ostreamer m_ostreamer;
    std::shared_ptr<buffer_factory> m_store;
    uint64_t m_secret;
    std::mutex m_mutex;
};

std::shared_ptr<channel> create_channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t secret) noexcept(false)
{
    return std::make_shared<transport>(bind, peer, secret);
}

}}
