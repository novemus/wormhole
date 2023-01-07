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
                            m_jobs.emplace(section::ping_shot, m_pass);

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

        bool shutdown(const callback& handle)
        {
            if (m_status != state::linked && m_status != state::tearing)
            {
                boost::system::error_code error = m_status == state::shutting ? 
                    boost::asio::error::in_progress : boost::asio::error::broken_pipe;

                m_io.post(boost::bind(handle, error));
                return false;
            }

            if (m_status == state::tearing)
            {
                m_status = state::finished;
                m_remote = 0;

                m_io.post(boost::bind(handle, boost::system::error_code()));
                return true;
            }

            auto now = boost::posix_time::microsec_clock::universal_time();
            on_shutdown = handle;

            m_dead = now + shutdown_timeout();
            m_status = state::shutting;

            m_jobs.emplace(section::ping_shot, now);
            return true;
        }

        bool connect(const callback& handle)
        {
            if (m_status != state::initial)
            {
                boost::system::error_code ec = m_status == state::connecting ? 
                    boost::asio::error::in_progress : m_status == state::linked ?
                        boost::asio::error::already_connected : boost::asio::error::broken_pipe;

                m_io.post(boost::bind(handle, ec));
                return false;
            }

            m_local = make_pin();
            on_connect = handle;

            m_pass = boost::posix_time::microsec_clock::universal_time();
            m_status = state::connecting;

            m_jobs.emplace(section::link_init, m_pass);
            return true;
        }

        bool accept(const callback& handle)
        {
            if (m_status != state::initial)
            {
                boost::system::error_code ec = m_status == state::accepting ? 
                    boost::asio::error::in_progress : m_status == state::linked ?
                        boost::asio::error::already_connected : boost::asio::error::broken_pipe;

                m_io.post(boost::bind(handle, ec));
                return false;
            }

            m_local = make_pin();
            on_connect = handle;

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
        std::map<uint16_t, boost::posix_time::ptime> m_jobs;
        boost::posix_time::ptime m_pass;
        boost::posix_time::ptime m_dead;
        callback on_connect;
        callback on_shutdown;
    };

    struct ostreamer
    {
        struct storage
        {
            bool empty() const
            {
                return m_data.empty();
            }

            uint64_t head() const
            {
                return m_head;
            }

            uint64_t tail() const
            {
                return m_tail;
            }

            uint64_t milestone() const
            {
                return m_snapshot.empty() ? m_head : m_snapshot.begin()->lower();
            }

            void forget(uint64_t pos, uint64_t size)
            {
                m_snapshot.subtract(boost::icl::interval<uint64_t>::type(pos, pos + size));
            }

            const_buffer pop(size_t max)
            {
                static const const_buffer zero("");
                
                if (m_data.empty())
                    return zero;

                auto iter = m_data.begin();
                auto buffer = iter->pop_front(std::min(max, iter->size()));
                
                if (iter->size() == 0)
                    m_data.erase(iter);

                m_head += buffer.size();

                return buffer;
            }

            uint64_t push(const const_buffer& buffer)
            {
                m_data.push_back(buffer);
                m_tail += buffer.size();
                m_snapshot.add(boost::icl::interval<uint64_t>::type(m_tail, buffer.size()));
                return m_tail;
            }

            void clear()
            {
                m_data.clear();
                m_snapshot.clear();
                m_head = 0;
                m_tail = 0;
            }

        private:

            std::list<const_buffer> m_data;
            boost::icl::interval_set<uint64_t> m_snapshot;
            uint64_t m_head = 0;
            uint64_t m_tail = 0;
        };

        ostreamer(boost::asio::io_context& io) : m_io(io)
        {
        }

        void error(const boost::system::error_code& ec)
        {
            auto total = m_store.milestone();

            auto iter = m_senders.begin();
            while (iter != m_senders.end())
            {
                uint64_t head = iter->first - iter->second.second;
                size_t sent = 0;

                if (total > head && total < iter->first)
                    sent = total - head;
                else if (total >= head) 
                    sent = iter->second.second;

                m_io.post(boost::bind(iter->second.first, ec, sent));
                ++iter;
            }

            m_senders.clear();
            m_flight.clear();
            m_store.clear();
        }

        void parse(const packet& pack)
        {
            auto sect = pack.payload();

            while (sect.type() != section::list_stub)
            {
                if (sect.type() == section::data_ackn)
                {
                    snippet snip(sect.value());

                    m_store.forget(snip.offset(), snip.length());
                    m_flight.erase(snip);

                    auto iter = m_senders.begin();
                    while (iter != m_senders.end() && iter->first <= m_store.milestone())
                    {
                        m_io.post(boost::bind(iter->second.first, boost::system::error_code(), iter->second.second));
                        iter = m_senders.erase(iter);
                    }
                }

                sect = sect.next();
            }
        }

        void imbue(packet& pack)
        {
            auto now = boost::posix_time::microsec_clock::universal_time();

            auto sect = pack.useless();
            while (sect.size() > snippet::header_size + section::header_size)
            {
                auto iter = std::find_if(m_flight.begin(), m_flight.end(), [&sect, &now](const auto& item)
                {
                    return item.second < now && item.first.size() <= sect.size() - section::header_size;
                });

                if (iter == m_flight.end())
                    break;

                iter->second = now + resend_timeout();

                sect.set(iter->first);
                sect = sect.next();
            }

            while (m_flight.size() < snippet_flight())
            {
                if (m_store.empty() || sect.size() <= section::header_size + snippet::header_size)
                    break;

                snippet snip(sect.slice(section::header_size, sect.size() - section::header_size));
                snip.set(m_store.head(), m_store.pop(snip.size() - snippet::header_size));
                
                sect.type(section::data_move);
                sect.length(snip.size());

                m_flight.emplace(snip, now + resend_timeout());

                sect = sect.next();
            }
        }

        void append(const const_buffer& buffer, const io_callback& handle)
        {
            m_senders.emplace(m_store.push(buffer), std::make_pair(handle, buffer.size()));
        }

        bool deffered() const
        {
            return !m_flight.empty() || !m_store.empty();
        }

    private:

        boost::asio::io_context& m_io;
        storage m_store;
        std::map<snippet, boost::posix_time::ptime> m_flight;
        std::map<uint64_t, std::pair<io_callback, uint64_t>> m_senders;
    };

    struct istreamer
    {
        struct storage
        {
            bool available() const
            {
                return !m_pool.empty() && m_milestone == m_pool.begin()->first;
            }

            const_buffer pop(size_t max)
            {
                static const const_buffer zero("");

                if (!available())
                    return zero;

                auto top = m_pool.begin()->second;
                m_pool.erase(m_pool.begin());

                auto buffer = top.pop_front(std::min(max, top.size()));
                m_milestone += buffer.size();

                if (top.size() > 0)
                    m_pool.emplace(m_milestone, top);

                return buffer;
            }

            void emplace(uint64_t offset, const const_buffer& buffer)
            {
                m_pool.emplace(offset, buffer);
            }

            void clear()
            {
                m_milestone = 0;
                m_pool.clear();    
            }

        private:

            uint64_t m_milestone = 0;
            std::map<uint64_t, const_buffer> m_pool;
        };

        istreamer(boost::asio::io_context& io)
            : m_io(io)
        {}

        void error(const boost::system::error_code& ec)
        {
            auto it = m_handles.begin();
            while (it != m_handles.end())
            {
                m_io.post(boost::bind(it->second, ec, 0));
                ++it;
            }

            m_handles.clear();
            m_acks.clear();
            m_store.clear();
        }

        void parse(const packet& pack)
        {
            auto sect = pack.payload();

            while (sect.type() != section::list_stub)
            {
                if (sect.type() == section::data_move)
                {
                    snippet snip(sect.value());
                    m_store.emplace(snip.offset(), snip.scrap());
                    m_acks.emplace_back(snip.header());
                }

                sect = sect.next();
            }

            transmit();
        }

        void imbue(packet& pack)
        {
            auto sect = pack.useless();

            auto iter = m_acks.begin();
            while (iter != m_acks.end() && sect.size() >= section::header_size)
            {
                sect.set(*iter);

                iter = m_acks.erase(iter);
                sect = sect.next();
            }
        }

        void append(const mutable_buffer& buf, const io_callback& handle)
        {
            m_handles.push_back(std::make_pair(buf, handle));
            transmit();
        }
    
        bool deffered() const
        {
            return !m_acks.empty();
        }

    private:

        void transmit()
        {
            while (m_store.available())
            {
                auto iter = m_handles.begin();
                if (iter == m_handles.end())
                    break;
                
                size_t read = 0;

                while (m_store.available() && iter->first.size() > 0)
                {
                    auto buffer = m_store.pop(iter->first.size());
                    iter->first.fill(0, buffer.size(), buffer.data());
                    iter->first.crop(buffer.size());
                    read += buffer.size();
                }

                m_io.post(boost::bind(iter->second, boost::system::error_code(), read));
                m_handles.erase(iter);
            }
        }

        boost::asio::io_context& m_io;
        storage m_store;
        std::list<handle> m_acks;
        std::list<std::pair<mutable_buffer, io_callback>> m_handles;
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

        if (m_mask)
            pack.make_opened(m_mask);

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
            if (m_mask)
                pack.make_opaque(m_mask);

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
                    ptr->schedule();
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

public:

    transport(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t mask)
        : m_reactor(novemus::reactor::shared_reactor())
        , m_socket(m_reactor->io(), bind.protocol())
        , m_timer(m_reactor->io())
        , m_connector(m_reactor->io())
        , m_istreamer(m_reactor->io())
        , m_ostreamer(m_reactor->io())
        , m_store(novemus::buffer_factory::shared_factory())
        , m_mask(mask)
    {
        static const size_t SOCKET_BUFFER_SIZE = 1048576;

        m_socket.set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
        m_socket.set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));
        m_socket.set_option(boost::asio::socket_base::reuse_address(true));
        
        m_socket.bind(bind);
        m_socket.connect(peer);
    }

    void shutdown(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.shutdown(handle))
        {
            boost::system::error_code err;
            m_timer.cancel(err);
        }
    }

    void connect(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.connect(handle))
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

    void accept(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.accept(handle))
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

    void read(const mutable_buffer& buffer, const io_callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto status = m_connector.status();
        if (status != state::linked)
        {
            boost::system::error_code ec = status == state::initial ? boost::asio::error::not_connected : 
                status == state::connecting || state::accepting ? boost::asio::error::try_again : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handle, ec, 0));
            return;
        }

        m_istreamer.append(buffer, handle);
    }

    void write(const const_buffer& buffer, const io_callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto status = m_connector.status();
        if (status != state::linked)
        {
            boost::system::error_code ec = status == state::initial ? boost::asio::error::not_connected : 
                status == state::connecting || state::accepting ? boost::asio::error::try_again : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handle, ec, 0));
            return;
        }

        m_ostreamer.append(buffer, handle);
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
    uint64_t m_mask;
    std::mutex m_mutex;
};

std::shared_ptr<channel> create_channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t secret) noexcept(false)
{
    return std::make_shared<transport>(bind, peer, secret);
}

}}
