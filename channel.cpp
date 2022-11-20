#include "salt.h"
#include "transport.h"
#include "reactor.h"
#include <map>
#include <set>
#include <list>
#include <atomic>
#include <iostream>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>


namespace salt {

class channel_impl : public channel, public pipe
{
    struct packet : public mutable_buffer
    {
        struct traits
        {
            static constexpr uint8_t sign = 0x99;
            static constexpr uint8_t version = 1 << 4;
            static constexpr size_t max_packet_size = 9992;
            static constexpr size_t header_size = 16;
            static constexpr size_t max_payload_size = max_packet_size - header_size;
        };

        enum flag 
        {
            syn = 0x01,
            fin = 0x02,
            psh = 0x04,
            ack = 0x08
        };

        packet() : mutable_buffer(traits::max_packet_size)
        {
            data()[0] = traits::sign;
            data()[1] = traits::version;
        }

        uint8_t sign() const
        { 
            return const_buffer::data()[0];
        }

        uint8_t version() const
        {
            return const_buffer::data()[1];
        }

        uint32_t pin() const
        {
            return ntohl(*(uint32_t *)(const_buffer::data() + 2));
        }

        uint16_t flags() const
        {
            return ntohs(*(uint16_t *)(const_buffer::data() + 6));
        }

        uint64_t cursor() const
        {
            return le64toh(*(uint64_t *)(const_buffer::data() + 8));
        }

        void set_sign(uint8_t s)
        {
            data()[0] = s;
        }

        void set_version(uint8_t v)
        {
            data()[1] = v;
        }

        void set_pin(uint32_t v)
        {
            *(uint32_t *)(data() + 2) = htonl(v);
        }

        void set_flags(uint16_t v)
        {
            *(uint16_t *)(data() + 6) = htons(v);
        }

        void set_cursor(uint64_t v)
        {
            *(uint64_t *)(data() + 8) = htole64(v);
        }

        bool has_flag(uint16_t v) const
        {
            return flags() & v;
        }

        bool valid() const
        {
            return sign() == traits::sign && size() >= traits::header_size;
        }

        void set_payload(const const_buffer& data)
        {
            std::memcpy(mutable_buffer::data() + packet::traits::header_size, data.data(), data.size());
            shrink(packet::traits::header_size + data.size());
        }

        const_buffer payload() const
        {
            return const_buffer::slice(traits::header_size, size() - traits::header_size);
        }
    };

    struct cursor
    {
        uint64_t value;

        cursor(uint64_t val) : value(val) { }

        bool operator<(const cursor& other) const
        {
            static const uint64_t pivot = std::numeric_limits<uint64_t>::max() / 2;
            return value + pivot < other.value + pivot;
        }

        bool operator<=(const cursor& other) const
        {
            return this->operator<(other) || this->operator==(other);
        }

        bool operator>(const cursor& other) const
        {
            static const uint64_t pivot = std::numeric_limits<uint64_t>::max() / 2;
            return value + pivot > other.value + pivot;
        }

        bool operator==(const cursor& other) const
        {
            return value > other.value;
        }

        cursor& operator++()
        {
            value++;
            return *this;
        }

        cursor operator++(int)
        {
            value++;
            return cursor(value - 1);
        }
        
        cursor& operator--()
        {
            value--;
            return *this;
        }

        cursor operator--(int)
        {
            value--;
            return cursor(value + 1);
        }

        cursor operator+(uint64_t v) const
        {
            return cursor(value - v);
        }

        cursor operator-(uint64_t v) const
        {
            return cursor(value - v);
        }
    };

    struct connect_handler
    {
        static uint32_t make_pin()
        {
            static std::atomic<uint32_t> s_pin;
            uint32_t pin = ++s_pin;
            return pin > 0 ? pin : make_pin();
        };

        enum job
        {
            snd_syn,
            snd_ack_syn,
            snd_fin,
            snd_ack_fin
        };

        connect_handler(boost::asio::io_context& io)
            : m_io(io)
            , m_alive(true)
            , m_linked(false)
            , m_loc_pin(make_pin())
            , m_rem_pin(0)
            , m_last_in(0)
            , m_last_out(0)
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

            m_alive = false;
            m_linked = false;
        }

        bool parse(const packet& pack)
        {
            if (!pack.valid())
                return true;

            if (m_rem_pin == 0 || m_rem_pin == pack.pin())
                m_last_in = std::time(0);

            if (pack.has_flag(packet::syn) && (m_rem_pin == 0 || m_rem_pin == pack.pin()))
            {
                m_rem_pin = pack.pin();

                if (pack.has_flag(packet::ack))
                {
                    m_linked = true;

                    if (on_connect)
                    {
                        m_io.post(boost::bind(on_connect, boost::system::error_code()));
                        on_connect = 0;
                    }

                    m_jobs.erase(job::snd_syn);
                }
                else
                {
                    m_jobs.insert(job::snd_ack_syn); 
                }

                return true;
            }
            else if (pack.has_flag(packet::fin) && m_rem_pin == pack.pin())
            {
                m_linked = false;

                if (pack.has_flag(packet::ack))
                {
                    m_rem_pin = 0;
                    m_alive = false;

                    if (on_shutdown)
                    {
                        m_io.post(boost::bind(on_shutdown, boost::system::error_code()));
                        on_shutdown = 0;
                    }

                    m_jobs.erase(job::snd_fin);
                }
                else
                {
                    m_jobs.insert(job::snd_ack_fin);
                }

                return true;
            }

            return !(m_linked && m_rem_pin != 0 && m_rem_pin == pack.pin());
        }

        bool imbue(packet& pack)
        {
            pack.set_sign(packet::traits::sign);
            pack.set_version(packet::traits::version);
            pack.set_flags(0);
            pack.set_pin(m_loc_pin);
            pack.set_cursor(0);

            auto iter = m_jobs.begin();
            if (iter != m_jobs.end())
            {
                switch (*iter)
                {
                    case job::snd_syn:
                    {
                        pack.set_flags(packet::syn);
                        break;
                    }
                    case job::snd_ack_syn:
                    {
                        pack.set_flags(packet::syn | packet::ack);
                        m_jobs.erase(job::snd_ack_syn);
                        m_linked = true;

                        if (on_connect)
                        {
                            m_io.post(boost::bind(on_connect, boost::system::error_code()));
                            on_connect = 0;
                        }

                        break;
                    }
                    case job::snd_fin:
                    {
                        pack.set_flags(packet::fin);
                        m_linked = false;
                        break;
                    }
                    case job::snd_ack_fin:
                    {
                        pack.set_flags(packet::fin | packet::ack);
                        m_jobs.erase(job::snd_ack_fin);
                        m_alive = false;

                        if (on_shutdown)
                        {
                            m_io.post(boost::bind(on_shutdown, boost::system::error_code()));
                            on_shutdown = 0;
                        }

                        break;
                    }
                    default:
                        break;
                }

                pack.shrink(packet::traits::header_size);
                m_last_out = std::time(0);
                return true;
            }

            if (m_last_out + 20 > std::time(0))
            {
                pack.shrink(packet::traits::header_size);
                m_last_out = std::time(0);

                return true;
            }

            return false;
        }

        bool is_local_fin(const packet& pack) const
        {
            return pack.pin() == m_loc_pin && pack.flags() == packet::fin;
        }

        bool is_remote_fin(const packet& pack) const
        {
            return m_rem_pin != 0 && pack.pin() == m_rem_pin && pack.flags() == packet::fin;
        }

        bool is_alive() const { return m_alive; }

        bool is_linked() const { return m_linked; }

        bool is_connecting() const { return m_alive && on_connect; }

        bool is_shutdowning() const { return m_alive && on_shutdown; }

        bool is_keepalive_lost() const
        {
            return m_last_in != 0 && m_last_in + 30 > std::time(0);
        }

        void shutdown(const callback& handle)
        {
            on_shutdown = handle;
            m_jobs.insert(job::snd_fin);
        }

        void connect(const callback& handle)
        {
            on_connect = handle;
            m_jobs.insert(job::snd_syn);
        }

        void accept(const callback& handle)
        {
            on_connect = handle;
        }

    private:

        boost::asio::io_context& m_io;
        bool m_alive;
        bool m_linked;
        uint32_t m_loc_pin;
        uint32_t m_rem_pin;
        std::time_t m_last_in;
        std::time_t m_last_out;
        std::set<job> m_jobs;
        callback on_connect;
        callback on_shutdown;
    };

    struct ostream_handler
    {
        ostream_handler(boost::asio::io_context& io)
            : m_io(io)
            , m_tail(0)
        {}

        void error(const boost::system::error_code& ec)
        {
            auto hit = m_handles.begin();
            while (hit != m_handles.end())
            {
                m_io.post(boost::bind(hit->second.first, ec, 0));
                ++hit;
            }
            m_handles.clear();
        }

        bool parse(const packet& pack)
        {
            if (pack.flags() != (packet::psh | packet::ack))
                return false;

            m_chunks.erase(pack.cursor());

            cursor top = std::numeric_limits<uint64_t>::max();

            auto cit = m_chunks.begin();
            if (cit != m_chunks.end())
                top = cit->first;

            auto hit = m_handles.begin();
            while (hit != m_handles.end())
            {
                if (hit->first <= top)
                {
                    m_io.post(boost::bind(hit->second.first, boost::system::error_code(), hit->second.second));
                    hit = m_handles.erase(hit);
                }
                else
                    break;
            }
            return true;
        }

        bool imbue(packet& pack)
        {
            auto iter = std::find_if(
                    m_chunks.begin(), m_chunks.end(), [](const std::pair<cursor, chunk>& p) { return p.second.ready(); }
                );

            if (iter == m_chunks.end())
                return false;

            pack.set_payload(iter->second.data);
            pack.set_cursor(iter->first.value);
            pack.set_flags(packet::psh);

            iter->second.retime();
            
            return true;
        }

        void append(const const_buffer& buf, const io_callback& handle)
        {
            static const size_t MAX_BUFFERED_PACKETS = 16384;

            if (m_chunks.size() >= MAX_BUFFERED_PACKETS)
            {
                m_io.post(boost::bind(handle, boost::asio::error::no_buffer_space, 0));
                return;
            }

            for(size_t shift = 0; shift < buf.size(); shift += packet::traits::max_payload_size)
            {
                m_chunks.emplace(
                    m_tail++, buf.slice(shift, std::min(packet::traits::max_payload_size, buf.size() - shift))
                    );
            }
            m_handles.insert(std::make_pair(m_tail, std::make_pair(handle, buf.size())));
        }

    private:

        struct chunk
        {
            const_buffer data;
            boost::posix_time::ptime time;
            
            chunk(const const_buffer& buf) : data(buf) { }

            bool ready() const
            {
                static const int64_t ACK_AGE = 50;

                auto now = boost::posix_time::microsec_clock::universal_time();
                if (time.is_not_a_date_time())
                {
                    return true;
                }

                return (time - now).total_milliseconds() > ACK_AGE;
            };

            void retime()
            {
                time = boost::posix_time::microsec_clock::universal_time();
            }
        };

        boost::asio::io_context& m_io;
        cursor m_tail;
        std::map<cursor, chunk> m_chunks;
        std::map<cursor, std::pair<io_callback, size_t>> m_handles;
    };

    struct istream_handler
    {
        istream_handler(boost::asio::io_context& io)
            : m_io(io)
            , m_tail(std::numeric_limits<uint64_t>::max())
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
        }

        bool parse(const packet& pack)
        {
            static const size_t MAX_BUFFERED_PACKETS = 16384;

            if (pack.flags() != packet::psh || m_parts.size() >= MAX_BUFFERED_PACKETS)
                return false;

            m_acks.insert(pack.cursor());

            if (m_tail < pack.cursor())
            {
                m_parts.insert(
                    std::make_pair(pack.cursor(), pack.payload())
                    );

                transmit_data();
            }

            return true;
        }

        bool imbue(packet& pack)
        {
            auto it = m_acks.begin();
            if (it != m_acks.end())
            {
                pack.set_cursor(it->value);
                pack.set_flags(packet::psh | packet::ack);
                pack.shrink(packet::traits::header_size);
                return true;
            }
            return false;
        }

        void append(const mutable_buffer& buf, const io_callback& handle)
        {
            m_handles.push_back(std::make_pair(buf, handle));
            transmit_data();
        }
    
    private:

        void transmit_data()
        {
            while (!m_handles.empty())
            {
                auto top = m_handles.front();
                size_t shift = 0;

                auto it = m_parts.begin();
                while (it != m_parts.end() && it->first == m_tail + 1)
                {
                    size_t size = std::min(top.first.size() - shift, it->second.size());
                    std::memcpy(top.first.data() + shift, it->second.data(), size);

                    shift += size;

                    if (it->second.size() == size)
                    {
                        m_tail = it->first;
                        it = m_parts.erase(it);
                    }
                    else
                    {
                        it->second.prune(size);
                        break;
                    }

                    if (top.first.size() == shift)
                        break;
                }

                if (shift == 0)
                    break;

                m_handles.pop_front();

                m_io.post(boost::bind(top.second, boost::system::error_code(), shift));
            }
        }

        boost::asio::io_context& m_io;
        cursor m_tail;
        std::set<cursor> m_acks;
        std::map<cursor, const_buffer> m_parts;
        std::list<std::pair<mutable_buffer, io_callback>> m_handles;
    };

    void error(const boost::system::error_code& ec) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_connect.error(ec);
        m_istream.error(ec);
        m_ostream.error(ec);
    }

    void push(const mutable_buffer& buffer) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connect.is_alive())
        {
            const packet& pack = reinterpret_cast<const packet&>(buffer);

            if (m_connect.parse(pack))
            {
                if (m_connect.is_remote_fin(pack))
                {
                    m_istream.error(boost::asio::error::connection_aborted);
                    m_ostream.error(boost::asio::error::connection_aborted);
                }
            }
            else if (m_connect.is_linked())
            {
                m_istream.parse(pack) || m_ostream.parse(pack);
            }
        }
    }

    bool pull(mutable_buffer& buffer) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_connect.is_alive())
        {
            if (m_connect.is_keepalive_lost())
            {
                m_connect.error(boost::asio::error::connection_reset);
                m_ostream.error(boost::asio::error::connection_reset);
                m_istream.error(boost::asio::error::connection_reset);

                return false;
            }

            packet& pack = reinterpret_cast<packet&>(buffer);

            if (m_connect.imbue(pack))
            {
                if (m_connect.is_local_fin(pack))
                {
                    m_istream.error(boost::asio::error::connection_aborted);
                    m_ostream.error(boost::asio::error::connection_aborted);
                }

                buffer.shrink(pack.size());
                return true;
            }
            else if (m_connect.is_linked() && (m_istream.imbue(pack) || m_ostream.imbue(pack)))
            {
                buffer.shrink(pack.size());
                return true;
            }
        }

        return false;
    }

public:

    channel_impl(std::shared_ptr<reactor> reactor)
        : m_reactor(reactor)
        , m_connect(reactor->get_io())
        , m_istream(reactor->get_io())
        , m_ostream(reactor->get_io())
    {
    }

    ~channel_impl()
    {
    }

    void shutdown(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_connect.is_alive() || !m_connect.is_linked() || m_connect.is_shutdowning())
        {
            boost::system::error_code error = m_connect.is_shutdowning() ? 
                boost::asio::error::already_started : m_connect.is_alive() ? 
                    boost::asio::error::not_connected : boost::asio::error::broken_pipe;

            m_reactor->get_io().post(boost::bind(handle, error));
            return;
        }

        m_connect.shutdown(handle);
    }

    void connect(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_connect.is_alive() || m_connect.is_linked() || m_connect.is_connecting())
        {
            boost::system::error_code ec = m_connect.is_connecting() ? 
                boost::asio::error::already_started : m_connect.is_linked() ? 
                    boost::asio::error::already_connected : boost::asio::error::broken_pipe;

            m_reactor->get_io().post(boost::bind(handle, ec));
            return;
        }

        m_connect.connect(handle);
    }

    void accept(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_connect.is_alive() || m_connect.is_linked() || m_connect.is_connecting())
        {
            boost::system::error_code ec = m_connect.is_connecting() ? 
                boost::asio::error::already_started : m_connect.is_linked() ? 
                    boost::asio::error::already_connected : boost::asio::error::broken_pipe;

            m_reactor->get_io().post(boost::bind(handle, ec));
            return;
        }

        m_connect.accept(handle);
    }

    void read(const mutable_buffer& buf, const io_callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_connect.is_alive() || !m_connect.is_linked())
        {
            boost::system::error_code ec = m_connect.is_alive() ? 
                boost::asio::error::not_connected : boost::asio::error::broken_pipe;

            m_reactor->get_io().post(boost::bind(handle, ec, 0));
            return;
        }

        m_istream.append(buf, handle);
    }

    void write(const const_buffer& buf, const io_callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
                
        if (!m_connect.is_alive() || !m_connect.is_linked())
        {
            boost::system::error_code ec = m_connect.is_alive() ? 
                boost::asio::error::not_connected : boost::asio::error::broken_pipe;

            m_reactor->get_io().post(boost::bind(handle, ec, 0));
            return;
        }

        m_ostream.append(buf, handle);
    }

private:

    std::shared_ptr<reactor> m_reactor;
    connect_handler m_connect;
    istream_handler m_istream;
    ostream_handler m_ostream;
    std::mutex m_mutex;
};

std::shared_ptr<channel> create_channel(std::shared_ptr<reactor> reactor, const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer)
{
    return std::make_shared<channel_impl>(reactor);
}

}
