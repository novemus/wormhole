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
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace novemus { namespace tubus {

template<class value_type> value_type getenv(const std::string& name, const value_type& def)
{
    try
    {
        const char *env = std::getenv(name.c_str());
        return env ? boost::lexical_cast<value_type>(env) : def;
    }
    catch (const boost::bad_lexical_cast& ex)
    {
        std::cout << "getenv: " << ex.what() << std::endl;
    }

    return def;
}

class transport : public novemus::tubus::channel, public std::enable_shared_from_this<transport>
{
    enum state { neither, initial, accepting, connecting, linked, shutting, tearing, finished };

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
        static boost::posix_time::milliseconds s_timeout(getenv("TUBUS_SHUTDOWN_TIMEOUT", 2000));
        return s_timeout;
    }

    inline static size_t snippet_flight()
    {
        static size_t s_flight(getenv("TUBUS_SNIPPET_FLIGHT", 5));
        return s_flight;
    }

    inline static size_t receive_buffer_size()
    {
        static size_t s_size(getenv("TUBUS_RECEIVE_BUFFER_SIZE", 5 * 1024 * 1024));
        return s_size;
    }

    inline static size_t send_buffer_size()
    {
        static size_t s_size(getenv("TUBUS_SEND_BUFFER_SIZE", 5 * 1024 * 1024));
        return s_size;
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
            , m_status(state::neither)
            , m_local(0)
            , m_remote(0)
            , m_seen(boost::posix_time::max_date_time)
            , m_dead(boost::posix_time::max_date_time)
        {
        }

        void init(const callback& fallback)
        {
            on_error = fallback;
            m_status = state::initial;
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
            m_seen = boost::posix_time::max_date_time;
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

            m_seen = boost::posix_time::microsec_clock::universal_time();

            auto payload = pack.data();
            auto section = payload.advance();

            while (section.valid())
            {
                auto type = section.type();
                switch (type)
                {
                    case section::link:
                    {
                        m_jobs.emplace(section::link | section::echo, m_seen);

                        if (m_status == state::accepting)
                        {
                            m_remote = pack.pin();
                        }
                        break;
                    }
                    case section::link | section::echo:
                    {
                        if (m_status == state::connecting)
                        {
                            m_status = state::linked;
                            m_remote = pack.pin();

                            m_jobs.erase(section::link);
                            m_jobs.emplace(section::ping, m_seen + ping_timeout());

                            m_io.post(boost::bind(on_connect, boost::system::error_code()));
                            on_connect = 0;
                        }
                        break;
                    }
                    case section::tear:
                    {
                        m_jobs.emplace(section::tear | section::echo, m_seen);

                        if (m_status == state::linked)
                        {
                            m_status = state::tearing;
                            m_dead = m_seen + shutdown_timeout();
                        }
                        break;
                    }
                    case section::tear | section::echo:
                    {
                        if (m_status == state::shutting)
                        {
                            m_status = state::finished;
                            m_jobs.erase(section::tear);

                            m_io.post(boost::bind(on_shutdown, boost::system::error_code()));
                            on_shutdown = 0;
                        }
                        break;
                    } 
                    case section::ping:
                    {
                        m_jobs.emplace(section::ping | section::echo, m_seen);
                        break;
                    }
                    case section::ping | section::echo:
                    {
                        m_jobs.erase(section::ping);
                        m_jobs.emplace(section::ping, m_seen + ping_timeout());
                        break;
                    }
                    default: break;
                }

                section = payload.advance();
            }
        }

        packet imbue()
        {
            packet pack(m_local);

            auto now = boost::posix_time::microsec_clock::universal_time();
            if (now > m_dead || m_seen + ping_timeout() < now - boost::posix_time::seconds(5))
            {
                m_io.post(boost::bind(on_error, boost::asio::error::broken_pipe));
                return pack;
            }

            while (packet::max_packet_size - pack.size() >= section::header_size)
            {
                auto iter = std::find_if(m_jobs.begin(), m_jobs.end(), [now](const auto& item)
                {
                    return item.second < now;
                });

                if (iter == m_jobs.end())
                    break;
 
                pack.push_back(novemus::tubus::section(iter->first));
                
                if (iter->first == (section::link | section::echo))
                {
                    m_jobs.erase(iter);

                    if (m_status == state::accepting)
                    {
                        m_status = state::linked;
                        m_io.post(boost::bind(on_connect, boost::system::error_code()));
                        on_connect = 0;
                    }
                }
                else if (iter->first == (section::ping | section::echo) || iter->first == (section::tear | section::echo))
                {
                    m_jobs.erase(iter);
                }
                else
                {
                    iter->second = now + resend_timeout();
                }
            }

            return pack;
        }

        state status() const
        {
            return m_status;
        }

        void shutdown(const callback& handler)
        {
            if (m_status != state::linked)
            {
                boost::system::error_code error = m_status == state::shutting ? 
                    boost::asio::error::in_progress : boost::asio::error::broken_pipe;

                if (error == boost::asio::error::broken_pipe)
                    m_io.post(boost::bind(on_error, boost::asio::error::broken_pipe));

                m_io.post(boost::bind(handler, error));
                return;
            }

            auto now = boost::posix_time::microsec_clock::universal_time();
            on_shutdown = handler;

            m_dead = now + shutdown_timeout();
            m_status = state::shutting;

            m_jobs.emplace(section::tear, now);
        }

        void connect(const callback& handler)
        {
            if (m_status != state::initial)
            {
                boost::system::error_code error = m_status == state::connecting ? 
                    boost::asio::error::in_progress : m_status == state::linked ?
                        boost::asio::error::already_connected : boost::asio::error::broken_pipe;

                if (error == boost::asio::error::broken_pipe)
                    m_io.post(boost::bind(on_error, boost::asio::error::broken_pipe));

                m_io.post(boost::bind(handler, error));
                return;
            }

            m_local = make_pin();
            on_connect = handler;

            m_seen = boost::posix_time::microsec_clock::universal_time();
            m_status = state::connecting;

            m_jobs.emplace(section::link, m_seen);
        }

        void accept(const callback& handler)
        {
            if (m_status != state::initial)
            {
                boost::system::error_code error = m_status == state::accepting ? 
                    boost::asio::error::in_progress : m_status == state::linked ?
                        boost::asio::error::already_connected : boost::asio::error::broken_pipe;

                if (error == boost::asio::error::broken_pipe)
                    m_io.post(boost::bind(on_error, boost::asio::error::broken_pipe));

                m_io.post(boost::bind(handler, error));
                return;
            }

            m_local = make_pin();
            on_connect = handler;

            m_seen = boost::posix_time::microsec_clock::universal_time();
            m_status = state::accepting;
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
        boost::posix_time::ptime m_seen;
        boost::posix_time::ptime m_dead;
        callback on_error;
        callback on_connect;
        callback on_shutdown;
    };

    struct ostreamer
    {
        ostreamer(boost::asio::io_context& io) 
            : m_io(io)
        {
        }

        void init(const callback& fallback)
        {
            on_error = fallback;
        }

        void error(const boost::system::error_code& err)
        {
            auto cursor = m_moves.empty() ? m_buffer.head() : m_moves.begin()->first;

            auto iter = m_writers.begin();
            while (iter != m_writers.end())
            {
                size_t sent = 0;

                if (cursor >= iter->head + iter->size)
                    sent = iter->size;
                else if (cursor > iter->head) 
                    sent = cursor - iter->head;

                m_io.post(boost::bind(iter->callback, err, sent));
                ++iter;
            }

            m_writers.clear();
            m_moves.clear();
            m_buffer.clear();
        }

        void parse(const packet& pack)
        {
            auto payload = pack.data();
            auto section = payload.advance();

            while (section.valid())
            {
                auto type = section.type();
                if (type == (section::data | section::echo))
                {
                    cursor handle(section.value());
                    m_moves.erase(handle.value());

                    auto cursor = m_moves.empty() ? m_buffer.head() : m_moves.begin()->first;

                    auto iter = m_writers.begin();
                    while (iter != m_writers.end() && iter->head + iter->size <= cursor)
                    {
                        m_io.post(boost::bind(iter->callback, boost::system::error_code(), iter->size));
                        iter = m_writers.erase(iter);
                    }
                }

                section = payload.advance();
            }
        }

        void imbue(packet& pack)
        {
            auto now = boost::posix_time::microsec_clock::universal_time();
            while (packet::max_packet_size - pack.size() > section::header_size + snippet::header_size)
            {
                auto iter = std::find_if(m_moves.begin(), m_moves.end(), [&pack, &now](const auto& item)
                {
                    return item.second.time < now && item.second.data.size() <= packet::max_packet_size - pack.size();
                });

                if (iter == m_moves.end())
                    break;

                iter->second.time = now + resend_timeout();

                pack.push_back(iter->second.data);
            }

            while (m_buffer.available() && m_moves.size() < snippet_flight())
            {
                auto rest = packet::max_packet_size - pack.size();
                if (rest <= section::header_size + snippet::header_size)
                    break;

                auto handle = m_buffer.head();
                auto buffer = m_buffer.pull(rest - section::header_size - snippet::header_size);
                
                section sect(snippet(handle, buffer));
                pack.push_back(sect);
                m_moves.emplace(handle, flight(sect, now + resend_timeout()));
            }
        }

        void append(const const_buffer& buffer, const io_callback& caller)
        {
            auto tail = m_buffer.tail();
            if (m_buffer.add(buffer) == tail)
            {
                m_io.post(boost::bind(caller, boost::asio::error::no_buffer_space, 0));

                if (on_error)
                    m_io.post(boost::bind(on_error, boost::asio::error::no_buffer_space));

                return;
            }

            m_writers.emplace_back(tail, buffer.size(), caller);
        }

        bool deffered() const
        {
            return !m_moves.empty() || m_buffer.available();
        }

    private:

        struct streambuf
        {
            const_buffer pull(uint64_t max)
            {
                static const const_buffer zero;

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

            uint64_t add(const const_buffer& buf)
            {
                if (m_tail + buf.size() - m_head >= send_buffer_size())
                    return m_tail;

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

        struct writer
        {
            uint64_t head;
            uint64_t size;
            io_callback callback;

            writer(uint64_t h, uint64_t s, const io_callback& c)
                : head(h)
                , size(s)
                , callback(c)
            {
            }
        };

        struct flight
        {
            section data;
            boost::posix_time::ptime time;

            flight(const section& s, const boost::posix_time::ptime& t)
                : data(s)
                , time(t)
            {
            }
        };

        boost::asio::io_context& m_io;
        streambuf m_buffer;
        std::map<uint64_t, flight> m_moves;
        std::list<writer> m_writers;
        callback on_error;
    };

    struct istreamer
    {
        istreamer(boost::asio::io_context& io)
            : m_io(io)
        {
        }

        void init(const callback& fallback)
        {
            on_error = fallback;
        }

        void error(const boost::system::error_code& err)
        {
            auto iter = m_readers.begin();
            while (iter != m_readers.end())
            {
                m_io.post(boost::bind(iter->callback, err, 0));
                ++iter;
            }

            m_readers.clear();
            m_acks.clear();
            m_buffer.clear();
        }

        void parse(const packet& pack)
        {
            auto payload = pack.data();
            auto section = payload.advance();

            while (section.valid())
            {
                if (section.type() == section::data)
                {
                    snippet snip(section.value());
                    
                    if (m_buffer.map(snip.handle(), snip.fragment()))
                    {
                        m_acks.emplace_back(snip.handle());
                    }
                    else
                    {
                        if(on_error)
                            m_io.post(boost::bind(on_error, boost::asio::error::no_buffer_space));

                        break;
                    }
                }

                section = payload.advance();
            }

            transmit();
        }

        void imbue(packet& pack)
        {
            auto ackn = m_acks.begin();
            while (ackn != m_acks.end() && packet::max_packet_size - pack.size() >= section::header_size + cursor::cursor_size)
            {
                pack.push_back(section(cursor(*ackn)));
                ackn = m_acks.erase(ackn);
            }
        }

        void append(const mutable_buffer& buf, const io_callback& caller)
        {
            m_readers.emplace_back(buf, caller);
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
                auto iter = m_readers.begin();
                if (iter == m_readers.end())
                    break;
                
                size_t read = 0;
                while (m_buffer.available() && iter->buffer.size() > 0)
                {
                    auto buffer = m_buffer.pull(iter->buffer.size());
                    iter->buffer.fill(0, buffer.size(), buffer.data());
                    iter->buffer.crop(buffer.size());
                    read += buffer.size();
                }

                m_io.post(boost::bind(iter->callback, boost::system::error_code(), read));
                m_readers.erase(iter);
            }
        }

        struct streambuf
        {
            const_buffer pull(uint64_t max)
            {
                static const const_buffer zero;

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

            bool map(uint64_t handle, const const_buffer& buffer)
            {
                if (handle >= m_tail && handle + buffer.size() - m_head > receive_buffer_size())
                    return false;

                if (handle >= m_head)
                {
                    auto res = m_data.emplace(handle, buffer);
                    if (res.second)
                    {
                        auto iter = m_data.rbegin();
                        m_tail = iter->first + iter->second.size();
                    }
                }

                return true;
            }

            uint64_t head() const
            {
                return m_head;
            }

            uint64_t tail() const
            {
                return m_tail;
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
            uint64_t m_tail = 0;
            std::map<uint64_t, const_buffer> m_data;
        };

        struct reader
        {
            mutable_buffer buffer;
            io_callback callback;

            reader(const mutable_buffer& b, const io_callback& c)
                : buffer(b)
                , callback(c)
            {}
        };
        
        boost::asio::io_context& m_io;
        streambuf m_buffer;
        std::list<uint64_t> m_acks;
        std::list<reader> m_readers;
        callback on_error;
    };

protected:

    void mistake(const boost::system::error_code& ec)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_connector.error(ec);
        m_istreamer.error(ec);
        m_ostreamer.error(ec);

        boost::system::error_code err;
        m_socket.close(err);
        m_timer.cancel(err);
    }

    void feed(const mutable_buffer& buffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (buffer.size() < packet::header_size)
            return;

        packet pack(m_secret ? dimmer::invert(m_secret, buffer) : buffer);

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
                m_istreamer.error(boost::asio::error::interrupted);
                m_ostreamer.error(boost::asio::error::interrupted);
            }

            boost::system::error_code err;
            m_timer.cancel(err);
        }
    }

    void consume()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.status() == state::finished)
        {
            m_istreamer.error(boost::asio::error::interrupted);
            m_ostreamer.error(boost::asio::error::interrupted);
            return;
        }

        std::weak_ptr<transport> weak = shared_from_this();
        auto buffer = mutable_buffer::create(packet::max_packet_size);
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
                ptr->consume();
            }
        });
    }

    void produce()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connector.status() == state::finished)
        {
            m_istreamer.error(boost::asio::error::interrupted);
            m_ostreamer.error(boost::asio::error::interrupted);

            boost::system::error_code err;
            m_socket.close(err);
            return;
        }

        std::weak_ptr<transport> weak = shared_from_this();
        packet pack = m_connector.imbue();

        if (m_connector.status() == state::linked)
        {
            m_istreamer.imbue(pack);
            m_ostreamer.imbue(pack);
        }

        if (pack.size() > packet::header_size)
        {
            auto buffer = m_secret == 0 ? pack : multibuffer<const_buffer>(dimmer::invert(m_secret, pack.unite()));
            m_socket.async_send(buffer, [weak, buffer](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error)
                        ptr->mistake(error);
                    else if (buffer.size() < size)
                        ptr->mistake(boost::asio::error::message_size);
                    else
                        ptr->produce();
                }
            });
        }
        else
        {
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
                        ptr->produce();
                }
            });
        }
    }

public:

    transport(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t secret)
        : m_bind(bind)
        , m_peer(peer)
        , m_reactor(novemus::reactor::shared_reactor())
        , m_socket(m_reactor->io())
        , m_timer(m_reactor->io())
        , m_connector(m_reactor->io())
        , m_istreamer(m_reactor->io())
        , m_ostreamer(m_reactor->io())
        , m_secret(secret)
    {
    }

    void open() noexcept(false) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        static const size_t SOCKET_BUFFER_SIZE = 1048576;

        m_socket.open(m_bind.protocol());

        m_socket.set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
        m_socket.set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));
        m_socket.set_option(boost::asio::socket_base::reuse_address(true));
        
        m_socket.bind(m_bind);
        m_socket.connect(m_peer);

        std::weak_ptr<transport> weak = shared_from_this();
        
        auto on_error = [weak](const boost::system::error_code& error)
        {
            auto ptr = weak.lock();
            if (ptr)
            {
                ptr->mistake(error);
            }
        };

        m_connector.init(on_error);
        m_istreamer.init(on_error);
        m_ostreamer.init(on_error);

        m_reactor->io().post([weak]()
        {
            auto ptr = weak.lock();
            if (ptr)
            {
                ptr->produce();
                ptr->consume();
            }
        });
    }

    void close() noexcept(true) override
    {
        mistake(boost::asio::error::connection_aborted);
    }

    void shutdown(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_connector.shutdown(handler);
        boost::system::error_code err;
        m_timer.cancel(err);
    }

    void connect(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_connector.connect(handler);
        boost::system::error_code err;
        m_timer.cancel(err);
    }

    void accept(const callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_connector.accept(handler);
    }

    void read(const mutable_buffer& buffer, const io_callback& handler) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto status = m_connector.status();
        if (status != state::linked)
        {
            boost::system::error_code ec = status == state::initial ? boost::asio::error::not_connected : 
                status == state::connecting || status == state::accepting ? boost::asio::error::try_again : boost::asio::error::broken_pipe;

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
                status == state::connecting || status == state::accepting ? boost::asio::error::try_again : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handler, ec, 0));
            return;
        }

        m_ostreamer.append(buffer, handler);
        boost::system::error_code err;
        m_timer.cancel(err);
    }

private:

    boost::asio::ip::udp::endpoint m_bind;
    boost::asio::ip::udp::endpoint m_peer;
    std::shared_ptr<reactor> m_reactor;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::deadline_timer m_timer;
    connector m_connector;
    istreamer m_istreamer;
    ostreamer m_ostreamer;
    uint64_t m_secret;
    std::mutex m_mutex;
};

std::shared_ptr<channel> create_channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t secret) noexcept(true)
{
    return std::make_shared<transport>(bind, peer, secret);
}

}}
