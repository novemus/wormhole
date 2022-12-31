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
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace novemus { namespace tubus {

class transport : public novemus::tubus::channel, public std::enable_shared_from_this<transport>
{
    struct packet
    {
        enum flag 
        {
            syn = 0x01,
            png = 0x02,
            fin = 0x04,
            psh = 0x08,
            ack = 0x10
        };

        static constexpr uint8_t packet_sign = 0x99;
        static constexpr uint8_t packet_version = 1 << 4;
        static constexpr size_t header_size = 24;
        static constexpr size_t max_packet_size = 9992;
        static constexpr size_t max_payload_size = max_packet_size - header_size;

        packet(const mutable_buffer& buffer) : m_buffer(buffer)
        {
        }

        uint64_t salt() const
        {
            return le64toh(*(uint64_t *)data());
        }

        uint8_t sign() const
        {
            return data()[8];
        }

        uint8_t version() const
        {
            return data()[9];
        }

        uint32_t pin() const
        {
            return ntohl(*(uint32_t *)(data() + 10));
        }

        uint16_t flags() const
        {
            return ntohs(*(uint16_t *)(data() + 14));
        }

        uint64_t cursor() const
        {
            return le64toh(*(uint64_t *)(data() + 16));
        }

        mutable_buffer payload() const
        {
            return m_buffer.slice(packet::header_size, m_buffer.size() - packet::header_size);
        }

        bool has_flag(uint16_t v) const
        {
            return flags() & v;
        }

        bool valid() const
        {
            return size() >= packet::header_size && sign() == packet::packet_sign && version() == packet::packet_version;
        }

        void set_salt(uint64_t s)
        {
            *(uint64_t*)data() = htole64(s);
        }

        void set_sign(uint8_t s)
        {
            data()[8] = s;
        }

        void set_version(uint8_t v)
        {
            data()[9] = v;
        }

        void set_pin(uint32_t v)
        {
            *(uint32_t *)(data() + 10) = htonl(v);
        }

        void set_flags(uint16_t v)
        {
            *(uint16_t *)(data() + 14) = htons(v);
        }

        void set_cursor(uint64_t v)
        {
            *(uint64_t *)(data() + 16) = htole64(v);
        }

        void set_payload(const const_buffer& payload)
        {
            shrink(payload.size() + packet::header_size);
            std::memcpy(data() + packet::header_size, payload.data(), payload.size());
        }

        void make_opened(uint64_t secret)
        {
            uint64_t s = salt() ^ secret;

            set_salt(s);
            invert(secret, s);
        }

        void make_opaque(uint64_t secret)
        {
            std::random_device dev;
            std::mt19937_64 gen(dev());
            uint64_t s = static_cast<uint64_t>(gen());

            set_salt(s ^ secret);
            invert(secret, s);
        }

        void shrink(size_t len)
        {
            if (size() < len)
                throw std::runtime_error("shrink: out of range");
            else if (size() > len)
                m_buffer = m_buffer.slice(0, len);
        }

        size_t size() const
        {
            return m_buffer.size();
        }

        uint8_t* data()
        {
            return m_buffer.data();
        }
        
        const uint8_t* data() const
        {
            return m_buffer.data();
        }

        mutable_buffer buffer() const
        {
            return m_buffer;
        }

    private:

        inline uint64_t make_inverter(uint64_t secret, uint64_t salt)
        {
            uint64_t base = secret + salt;
            uint64_t shift = (base & 0x3F) | 0x01;
            return ((base >> shift) | (base << (64 - shift))) ^ salt;
        }

        void invert(uint64_t secret, uint64_t salt)
        {
            uint64_t inverter = make_inverter(secret, salt);

            uint8_t* ptr = data() + sizeof(uint64_t);
            uint8_t* end = data() + size();

            while (ptr + sizeof(uint64_t) <= end)
            {
                *(uint64_t*)ptr ^= inverter;
                inverter = make_inverter(inverter, salt);
                ptr += sizeof(uint64_t);
            }

            uint8_t* inv = (uint8_t*)&inverter;
            while (ptr < end)
            {
                *ptr ^= *inv;
                ++ptr;
                ++inv;
            }
        }

        mutable_buffer m_buffer;
    };

    struct cursor
    {
        uint64_t value;

        cursor(uint64_t val) : value(val) { }

        bool operator<(const cursor& other) const
        {
            static const uint64_t one_to_midday = std::numeric_limits<uint64_t>::max() >> 1;

            return (value < other.value && other.value - value <= one_to_midday) 
                || (value > other.value && value - other.value > one_to_midday);
        }

        bool operator<=(const cursor& other) const
        {
            return this->operator<(other) || this->operator==(other);
        }

        bool operator>(const cursor& other) const
        {
            static const uint64_t one_to_midday = std::numeric_limits<uint64_t>::max() >> 1;

            return (value > other.value && value - other.value <= one_to_midday) 
                || (value < other.value && other.value - value > one_to_midday);
        }
        
        bool operator>=(const cursor& other) const
        {
            return this->operator>(other) || this->operator==(other);
        }

        bool operator==(const cursor& other) const
        {
            return value == other.value;
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
            return cursor(value + v);
        }

        cursor operator-(uint64_t v) const
        {
            return cursor(value - v);
        }
    };

    struct connect_handler
    {
        static uint16_t make_pin()
        {
            static std::atomic<uint16_t> s_pin;
            uint16_t pin = ++s_pin;
            return pin > 0 ? pin : make_pin();
        };

        connect_handler(boost::asio::io_context& io)
            : m_io(io)
            , m_alive(true)
            , m_linked(false)
            , m_loc_pin(0)
            , m_rem_pin(0)
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

            m_jobs.clear();

            m_alive = false;
            m_linked = false;
        }

        bool parse(const packet& pack)
        {
            if (m_loc_pin == 0)
                return true;

            auto now = boost::posix_time::second_clock::universal_time();

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

                    m_jobs.erase(packet::syn);
                    m_jobs.emplace(packet::png, now + boost::posix_time::seconds(30)); 
                }
                else
                {
                    m_jobs.emplace(packet::syn | packet::ack, now); 
                }

                return true;
            }
            else if (pack.has_flag(packet::fin) && m_rem_pin != 0 && m_rem_pin == pack.pin())
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

                    m_jobs.erase(0);
                    m_jobs.erase(packet::fin);
                }
                else
                {
                    m_jobs.emplace(packet::fin | packet::ack, now);
                }

                return true;
            }
            else if (pack.has_flag(packet::png) && m_rem_pin != 0 && m_rem_pin == pack.pin())
            {
                if (pack.has_flag(packet::ack))
                {
                    m_jobs.emplace(packet::png, now + boost::posix_time::seconds(30));
                }
                else
                {
                    m_jobs.emplace(packet::png | packet::ack, now); 
                }

                return true;
            }

            return !(m_linked && m_rem_pin != 0 && m_rem_pin == pack.pin());
        }

        bool imbue(packet& pack)
        {
            auto now = boost::posix_time::microsec_clock::universal_time();
            auto iter = std::find_if(m_jobs.begin(), m_jobs.end(), [now](const std::pair<uint16_t, boost::posix_time::ptime>& item)
            {
                return now > item.second;
            });

            pack.set_sign(packet::packet_sign);
            pack.set_version(packet::packet_version);
            pack.set_pin(m_loc_pin);

            if (iter != m_jobs.end())
            {
                static const boost::posix_time::milliseconds RESEND_TIMEOUT(100);

                if (iter->first == 0)
                {
                    m_jobs.erase(iter);
                    m_alive = false;

                    if (on_shutdown)
                    {
                        m_io.post(boost::bind(on_shutdown, boost::asio::error::timed_out));
                        on_shutdown = 0;
                    }

                    m_jobs.erase(0);
                    m_jobs.erase(packet::fin);

                    return false;
                }

                pack.set_cursor(0);
                pack.set_flags(iter->first);
                pack.shrink(packet::header_size);

                iter->second = now + RESEND_TIMEOUT;

                if (iter->first == (packet::syn | packet::ack))
                {
                    m_jobs.erase(iter);
                    m_linked = true;

                    if (on_connect)
                    {
                        m_io.post(boost::bind(on_connect, boost::system::error_code()));
                        on_connect = 0;
                    }
                }
                else if (iter->first == packet::fin)
                {
                    m_linked = false;
                }
                else if (iter->first == (packet::fin | packet::ack))
                {
                    m_jobs.erase(iter);
                }

                return true;
            }

            return false;
        }

        bool is_local_fin(const packet& pack) const
        {
            return m_loc_pin != 0 && pack.pin() == m_loc_pin && pack.flags() == packet::fin;
        }

        bool is_remote_fin(const packet& pack) const
        {
            return m_rem_pin != 0 && pack.pin() == m_rem_pin && pack.flags() == packet::fin;
        }

        bool is_alive() const { return m_alive; }

        bool is_linked() const { return m_linked; }

        bool is_connecting() const { return m_alive && on_connect; }

        bool is_shutting() const { return m_alive && on_shutdown; }

        bool is_hangup() const
        {
            auto deadline = boost::posix_time::microsec_clock::universal_time() - boost::posix_time::seconds(30);
            auto iter = std::find_if(m_jobs.begin(), m_jobs.end(), [deadline](const std::pair<uint16_t, boost::posix_time::ptime>& item)
            {
                return deadline > item.second;
            });
            return iter != m_jobs.end();
        }

        void shutdown(const callback& handle)
        {
            static const boost::posix_time::milliseconds SHUTDOWN_TIMEOUT(1000);

            auto now = boost::posix_time::microsec_clock::universal_time();
            on_shutdown = handle;

            m_jobs.emplace(packet::fin, now);
            m_jobs.emplace(0, now + SHUTDOWN_TIMEOUT);
        }

        void connect(const callback& handle)
        {
            m_loc_pin = make_pin();
            on_connect = handle;
            m_jobs.emplace(packet::syn, boost::posix_time::microsec_clock::universal_time());
        }

        void accept(const callback& handle)
        {
            m_loc_pin = make_pin();
            on_connect = handle;
        }

    private:

        boost::asio::io_context& m_io;
        bool m_alive;
        bool m_linked;
        uint32_t m_loc_pin;
        uint32_t m_rem_pin;
        std::map<uint16_t, boost::posix_time::ptime> m_jobs;
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
                m_io.post(boost::bind(hit->second, ec));
                ++hit;
            }
            m_handles.clear();
        }

        bool parse(const packet& pack)
        {
            if (pack.flags() != (packet::psh | packet::ack))
                return false;

            m_chunks.erase(pack.cursor());

            cursor top = m_chunks.empty() ? 0 : m_chunks.begin()->first;

            auto hit = m_handles.begin();
            while (hit != m_handles.end())
            {
                if (m_chunks.empty() || hit->first <= top)
                {
                    m_io.post(boost::bind(hit->second, boost::system::error_code()));
                    hit = m_handles.erase(hit);
                }
                else
                    break;
            }
            return true;
        }

        bool imbue(packet& pack)
        {
            static const boost::posix_time::milliseconds RESEND_TIMEOUT(100);

            auto now = boost::posix_time::microsec_clock::universal_time();
            auto iter = std::find_if(m_chunks.begin(), m_chunks.end(), [now](const std::pair<cursor, chunk>& p) 
            { 
                return p.second.second < now; 
            });

            if (iter == m_chunks.end())
                return false;

            pack.set_payload(iter->second.first);
            pack.set_cursor(iter->first.value);
            pack.set_flags(packet::psh);

            iter->second.second = now + RESEND_TIMEOUT;
            
            return true;
        }

        void append(const const_buffer& buffer, const callback& handle)
        {
            static const size_t MAX_BUFFERED_PACKETS = 16384;

            if (m_chunks.size() >= MAX_BUFFERED_PACKETS)
            {
                m_io.post(boost::bind(handle, boost::asio::error::no_buffer_space));
                return;
            }

            for(size_t shift = 0; shift < buffer.size(); shift += packet::max_payload_size)
            {
                m_chunks.emplace(
                    m_tail++, std::make_pair(
                        buffer.slice(shift, std::min(packet::max_payload_size, buffer.size() - shift)),
                        boost::posix_time::min_date_time)
                    );
            }
            m_handles.emplace(m_tail, handle);
        }

    private:

        typedef std::pair<const_buffer, boost::posix_time::ptime> chunk;

        boost::asio::io_context& m_io;
        cursor m_tail;
        std::map<cursor, chunk> m_chunks;
        std::map<cursor, callback> m_handles;
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
                m_io.post(boost::bind(it->second, ec));
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
                pack.shrink(packet::header_size);
                m_acks.erase(it);
                return true;
            }
            return false;
        }

        void append(const mutable_buffer& buf, const callback& handle)
        {
            m_handles.push_back(std::make_pair(buf, handle));
            transmit_data();
        }
    
    private:

        void transmit_data()
        {
            while (!m_handles.empty())
            {
                auto& top = m_handles.front();
                size_t shift = 0;

                auto part = m_parts.begin();
                while (part != m_parts.end() && part->first == m_tail + 1)
                {
                    size_t size = std::min(top.first.size() - shift, part->second.size());
                    std::memcpy((uint8_t*)top.first.data() + shift, part->second.data(), size);

                    shift += size;

                    if (part->second.size() == size)
                    {
                        m_tail = part->first;
                        part = m_parts.erase(part);
                    }
                    else
                    {
                        part->second = part->second.slice(0, size);
                        break;
                    }

                    if (top.first.size() == shift)
                        break;
                }

                if (shift < top.first.size())
                {
                    top.first = top.first.slice(shift, top.first.size() - shift);
                    break;
                }

                m_handles.pop_front();
                m_io.post(boost::bind(top.second, boost::system::error_code()));
            }
        }

        boost::asio::io_context& m_io;
        cursor m_tail;
        std::set<cursor> m_acks;
        std::map<cursor, mutable_buffer> m_parts;
        std::list<std::pair<mutable_buffer, callback>> m_handles;
    };

protected:

    void mistake(const boost::system::error_code& ec)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_coupler.error(ec);
        m_istream.error(ec);
        m_ostream.error(ec);
    }

    void feed(const mutable_buffer& buffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (buffer.size() < packet::header_size)
            return;

        packet pack(buffer);

        if (m_mask)
            pack.make_opened(m_mask);

        if (pack.valid())
        {
            if (m_coupler.parse(pack))
            {
                if (m_coupler.is_remote_fin(pack))
                {
                    m_istream.error(boost::asio::error::connection_aborted);
                    m_ostream.error(boost::asio::error::connection_aborted);
                }
            }
            else if (m_coupler.is_linked())
            {
                m_istream.parse(pack) || m_ostream.parse(pack);
            }
        }
    }

    void consume()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_coupler.is_alive())
        {
            novemus::mutable_buffer buffer = m_store->obtain(packet::max_packet_size);
            std::weak_ptr<transport> weak = shared_from_this();

            m_socket.async_receive(buffer, [weak, buffer](const boost::system::error_code& error, size_t size)
            {
                auto ptr = weak.lock();
                if (ptr)
                {
                    if (error)
                    {
                        ptr->mistake(error);
                    }
                    else
                    {
                        ptr->feed(buffer.slice(0, size));
                        ptr->consume();
                    }
                }
            });
        }
    }

    void produce()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_coupler.is_alive())
        {
            if (m_coupler.is_hangup())
            {
                m_coupler.error(boost::asio::error::broken_pipe);
                m_istream.error(boost::asio::error::broken_pipe);
                m_ostream.error(boost::asio::error::broken_pipe);
                return;
            }

            std::weak_ptr<transport> weak = shared_from_this();
            packet pack(m_store->obtain(packet::max_packet_size));

            if (m_coupler.imbue(pack) || (m_coupler.is_linked() && (m_istream.imbue(pack) || m_ostream.imbue(pack))))
            {
                if (m_mask)
                    pack.make_opaque(m_mask);

                m_socket.async_send(pack.buffer(), [weak, pack](const boost::system::error_code& error, size_t size)
                {
                    auto ptr = weak.lock();
                    if (ptr)
                    {
                        if (error)
                            ptr->mistake(error);
                        else if (pack.size() < size)
                            ptr->mistake(boost::asio::error::message_size);
                        else
                            ptr->produce();
                    }
                });

                if (m_coupler.is_local_fin(pack))
                {
                    m_istream.error(boost::asio::error::connection_aborted);
                    m_ostream.error(boost::asio::error::connection_aborted);
                }
            }
            else
            {
                m_timer.expires_from_now(boost::posix_time::milliseconds(100));
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
    }

public:

    transport(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t mask)
        : m_reactor(novemus::reactor::shared_reactor())
        , m_socket(m_reactor->io(), bind.protocol())
        , m_timer(m_reactor->io())
        , m_coupler(m_reactor->io())
        , m_istream(m_reactor->io())
        , m_ostream(m_reactor->io())
        , m_store(novemus::buffer_factory::shared_factory())
        , m_mask(mask)
    {
        m_socket.set_option(boost::asio::socket_base::reuse_address(true));
        m_socket.bind(bind);
        m_socket.connect(peer);
    }

    void shutdown(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_coupler.is_alive() || m_coupler.is_shutting())
        {
            boost::system::error_code error = m_coupler.is_shutting() ? 
                boost::asio::error::in_progress : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handle, error));
            return;
        }

        m_coupler.shutdown(handle);
        boost::system::error_code ec;
        m_timer.cancel(ec);
    }

    void connect(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_coupler.is_alive() || m_coupler.is_linked() || m_coupler.is_connecting())
        {
            boost::system::error_code ec = m_coupler.is_connecting() ? 
                boost::asio::error::in_progress : m_coupler.is_linked() ?
                    boost::asio::error::already_connected : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handle, ec));
            return;
        }

        m_coupler.connect(handle);
        m_reactor->io().post(boost::bind(&transport::produce, shared_from_this()));
        m_reactor->io().post(boost::bind(&transport::consume, shared_from_this()));
    }

    void accept(const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_coupler.is_alive() || m_coupler.is_linked() || m_coupler.is_connecting())
        {
            boost::system::error_code ec = m_coupler.is_connecting() ? 
                boost::asio::error::in_progress : m_coupler.is_linked() ?
                    boost::asio::error::already_connected : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handle, ec));
            return;
        }

        m_coupler.accept(handle);
        m_reactor->io().post(boost::bind(&transport::produce, shared_from_this()));
        m_reactor->io().post(boost::bind(&transport::consume, shared_from_this()));
    }

    void read(const mutable_buffer& buffer, const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (!m_coupler.is_alive() || !m_coupler.is_linked())
        {
            boost::system::error_code ec = m_coupler.is_alive() ? 
                boost::asio::error::not_connected : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handle, ec));
            return;
        }

        m_istream.append(buffer, handle);
        boost::system::error_code ec;
        m_timer.cancel(ec);
    }

    void write(const const_buffer& buffer, const callback& handle) noexcept(true) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
                
        if (!m_coupler.is_alive() || !m_coupler.is_linked())
        {
            boost::system::error_code ec = m_coupler.is_alive() ? 
                boost::asio::error::not_connected : boost::asio::error::broken_pipe;

            m_reactor->io().post(boost::bind(handle, ec));
            return;
        }

        m_ostream.append(buffer, handle);
        boost::system::error_code ec;
        m_timer.cancel(ec);
    }

private:

    std::shared_ptr<reactor> m_reactor;
    boost::asio::ip::udp::socket m_socket;
    boost::asio::deadline_timer m_timer;
    connect_handler m_coupler;
    istream_handler m_istream;
    ostream_handler m_ostream;
    std::shared_ptr<buffer_factory> m_store;
    uint64_t m_mask;
    std::mutex m_mutex;
};

std::shared_ptr<channel> create_channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer, uint64_t secret) noexcept(false)
{
    return std::make_shared<transport>(bind, peer, secret);
}

}}
