#include <map>
#include <set>
#include <list>
#include <iostream>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/shared_array.hpp>


namespace salt {

template<class byte_t> struct buffer
{
    buffer(size_t len) 
        : m_buffer(new uint8_t[len])
        , m_beg(0)
        , m_end(len)
    {
        std::memset(m_buffer.get(), 0, len);
    }

    buffer(byte_t* data, size_t len) 
        : m_buffer(data, [](uint8_t*){})
        , m_beg(0)
        , m_end(len)
    {
    }

    byte_t* data() const
    {
        return m_buffer.get() + m_beg;
    }
    
    size_t size() const
    {
        return m_end - m_beg;
    }

    void shrink(size_t len)
    {
        if (len > size())
            throw std::runtime_error("shrink: out of range");

        m_end = m_beg + len;
    }

    void prune(size_t len)
    {
        if (len > size())
            throw std::runtime_error("prune: out of range");

        m_beg += len;
    }

    buffer slice(size_t off, size_t len) const
    {
        if (off > size() || off + len > size())
            throw std::runtime_error("slice: out of range");

        return buffer(m_buffer, m_beg + off, m_beg + off + len);
    }

    bool unique() const
    {
        return m_buffer.unique();
    }

private:

    buffer(boost::shared_array<byte_t> buffer, size_t beg, size_t end)
        : m_buffer(buffer)
        , m_beg(beg)
        , m_end(end)
    {
    }

    boost::shared_array<byte_t> m_buffer;
    size_t m_beg;
    size_t m_end;
};

typedef buffer<uint8_t> mutable_buffer;
typedef buffer<const uint8_t> const_buffer;

typedef std::function<void(const boost::system::error_code&)> callback;
typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

namespace udp {

class channel
{
    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;
    typedef std::function<void(const io_callback&)> io_call;

    boost::asio::io_context        m_io;
    boost::asio::ip::udp::socket   m_socket;
    boost::asio::ip::udp::endpoint m_peer;

    size_t exec(const io_call& invoke)
    {
        boost::asio::deadline_timer timer(m_io);
        timer.expires_from_now(boost::posix_time::seconds(30));
        timer.async_wait([&](const boost::system::error_code& error)
        {
            if (error)
            {
                if (error == boost::asio::error::operation_aborted)
                    return;

                try
                {
                    m_socket.cancel();
                }
                catch (const std::exception &ex)
                {
                    std::cout << ex.what();
                }
            }
        });

        boost::system::error_code code = boost::asio::error::would_block;
        size_t length = 0;

        invoke([&code, &length](const boost::system::error_code& c, size_t l) {
            code = c;
            length = l;
        });

        do {
            m_io.run_one();
        } while (code == boost::asio::error::would_block);

        timer.cancel();

        if (code)
            throw boost::system::system_error(code);

        return length;
    }

public:

    channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer)
        : m_socket(m_io)
        , m_peer(peer)
    {
        m_socket.open(bind.protocol());
        m_socket.non_blocking(true);
        m_socket.bind(bind);
    }

    ~channel()
    {
        if (m_socket.is_open())
        {
            boost::system::error_code ec;
            m_socket.shutdown(boost::asio::ip::udp::socket::shutdown_both, ec);
            m_socket.close(ec);
        }
    }

    boost::asio::ip::udp::endpoint local_endpoint() const
    {
        return m_socket.local_endpoint();
    }

    boost::asio::ip::udp::endpoint remote_endpoint() const
    {
        return m_peer;
    }

    template<class mutable_byte> size_t receive(const buffer<mutable_byte>& pack)
    {
        auto timer = [start = boost::posix_time::microsec_clock::universal_time()]()
        {
            return boost::posix_time::microsec_clock::universal_time() - start;
        };

        while (timer().total_seconds() < 30)
        {
            boost::asio::ip::udp::endpoint src;
            size_t size = exec([&](const io_callback& callback)
            {
                m_socket.async_receive_from(boost::asio::buffer(pack.data(), pack.size()), src, callback);
            });

            if (src == m_peer)
                return size;
        }

        throw boost::system::error_code(boost::asio::error::operation_aborted);
    }

    template<class const_byte> size_t send(const buffer<const_byte>& pack)
    {
        size_t size = exec([&](const io_callback& callback)
        {
            m_socket.async_send_to(boost::asio::buffer(pack.data(), pack.size()), m_peer, callback);
        });

        if (size < pack.size())
            throw std::runtime_error("can't send message");
    }
};

}

const uint8_t SIGN = 0x99;
const uint8_t VERSION = 1 << 4;
const size_t PACKET_HEADER_SIZE = 16;
const size_t MAX_PACKET_SIZE = 9992;
const size_t MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - PACKET_HEADER_SIZE;

class channel
{
    struct packet : public buffer<uint8_t>
    {
        enum flag 
        {
            syn = 0x01,
            fin = 0x02,
            psh = 0x04,
            ack = 0x08
        };

        packet() : buffer(MAX_PACKET_SIZE)
        {
            data()[0] = SIGN;
            data()[1] = VERSION;
        }

        uint8_t sign() const
        { 
            return data()[0];
        }

        uint8_t version() const
        {
            return data()[1];
        }

        uint32_t pin() const
        {
            return ntohl(*(uint32_t *)(data() + 2));
        }

        uint16_t flags() const
        {
            return ntohs(*(uint16_t *)(data() + 6));
        }

        uint64_t cursor() const
        {
            return le64toh(*(uint64_t *)(data() + 8));
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

        buffer payload() const
        {
            return buffer::slice(PACKET_HEADER_SIZE, size() - PACKET_HEADER_SIZE);
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

    struct packet_factory
    {
        packet_factory()
        {
        }

        packet make_packet()
        {
            auto it = m_cache.begin();
            while (it != m_cache.end())
            {
                if (it->second.unique())
                {
                    packet pack = it->second;
                    std::memset(pack.data(), 0, pack.size());

                    it->first = std::time(0);

                    compress_cache();

                    return pack;
                }
                ++it;
            }

            m_cache.emplace_back(std::time(0), packet());

            return m_cache.back().second;
        }

    private:

        void compress_cache()
        {
            static const time_t TTL = 30;
            time_t now = std::time(0);

            auto it = m_cache.begin();
            while (it != m_cache.end())
            {
                if (it->second.unique() && it->first + TTL > now)
                    it = m_cache.erase(it);
                else
                    ++it;
            }
        }

        uint32_t m_pin;
        std::list<std::pair<time_t, packet>> m_cache;
    };

    struct connect_handler
    {
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
            , m_loc_pin(1) // TODO: random
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

            m_alive = false;
            m_linked = false;
        }

        bool parse(const packet& pack)
        {
            if (pack.has_flag(packet::syn) && m_rem_pin == 0 || m_rem_pin == pack.pin())
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
            pack.set_pin(m_loc_pin);

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

                pack.shrink(PACKET_HEADER_SIZE);
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

        bool is_charged() const 
        { 
            return m_jobs.empty();
        }

        bool is_alive() const { return m_alive; }

        bool is_linked() const { return m_linked; }

        bool is_connecting() const { return m_alive && on_connect; }

        bool is_shutdowning() const { return m_alive && on_shutdown; }

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
        uint32_t m_rem_pin;
        uint32_t m_loc_pin;
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
                m_io.post(boost::bind(hit->second, ec, 0));
                ++hit;
            }
            m_handles.clear();
        }

        bool parse(const packet& pack)
        {
            if (!pack.flags() == packet::psh | packet::ack)
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
            auto iter = std::find_if(
                    m_chunks.begin(), m_chunks.end(), [](auto it) { return it->second.ready(); }
                );

            if (iter == m_chunks.end())
                return false;

            std::memcpy(pack.data() + PACKET_HEADER_SIZE, iter->second.data.data(), iter->second.data.size());

            pack.set_cursor(iter->first.value);
            pack.set_flags(packet::psh);
            pack.shrink(PACKET_HEADER_SIZE + iter->second.data.size());

            iter->second.retime();
            
            return true;
        }

        bool is_charged() const
        {
            return std::find_if(
                    m_chunks.begin(), m_chunks.end(), [](auto it) { return it->second.ready(); }
                ) != m_chunks.end();
        }

        void append(const const_buffer& buf, const io_callback& handle)
        {
            static const size_t MAX_BUFFERED_PACKETS = 16384;

            if (m_chunks.size() >= MAX_BUFFERED_PACKETS)
            {
                m_io.post(boost::bind(handle, boost::asio::error::no_buffer_space, 0));
                return;
            }

            for(size_t shift = 0; shift < buf.size(); shift += MAX_PAYLOAD_SIZE)
            {
                m_chunks.emplace(
                    m_tail++, buf.slice(shift, std::min(MAX_PAYLOAD_SIZE, buf.size() - shift))
                    );
            }
            m_handles.insert(std::make_pair(m_tail, boost::bind(handle, boost::asio::placeholders::error, buf.size())));
        }

    private:

        struct chunk
        {
            const_buffer data;
            boost::posix_time::ptime time;
            
            chunk(const const_buffer& buf) : data(buf) { }

            bool ready()
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
        std::map<cursor, io_callback> m_handles;
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

                transmit();
            }
        }

        bool imbue(packet& pack)
        {
            auto it = m_acks.begin();
            if (it != m_acks.end())
            {
                pack.set_cursor(it->value);
                pack.set_flags(packet::psh | packet::ack);
                pack.shrink(PACKET_HEADER_SIZE);
                return true;
            }
            return false;
        }

        bool is_charged() const
        {
            return !m_acks.empty();
        }

        void append(const mutable_buffer& buf, const io_callback& handle)
        {
            m_handles.push_back(std::make_pair(buf, handle));
            transmit();
        }
    
    private:

        void transmit()
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
        std::map<cursor, mutable_buffer> m_parts;
        std::list<std::pair<mutable_buffer, io_callback>> m_handles;
    };

    typedef std::unique_lock<std::mutex> unique_lock;

    void on_error(const boost::system::error_code& ec)
    {
        m_connect.error(ec);
        m_istream.error(ec);
        m_ostream.error(ec);
    }

    void send_packet(unique_lock& lock, packet& pack)
    {
        lock.unlock();
        try
        {
            m_udp.send(pack);
        }
        catch (...)
        {
            lock.lock();
            throw;
        }
        lock.lock();
    }

    void receive_packet(unique_lock& lock, packet& pack)
    {
        lock.unlock();
        try
        {
            pack.shrink(
                m_udp.receive(pack)
                );
        }
        catch (...)
        {
            lock.lock();
            throw;
        }
        lock.lock();
    }

    void do_read()
    {
        unique_lock lock(m_mutex);
        
        while (m_connect.is_alive())
        {
            try
            {
                auto pack = m_packer.make_packet();
                
                receive_packet(lock, pack);

                if (m_connect.is_alive())
                {
                    if (m_connect.parse(pack))
                    {
                        if (m_connect.is_remote_fin(pack))
                        {
                            m_istream.error(boost::asio::error::operation_aborted);
                            m_ostream.error(boost::asio::error::operation_aborted);
                        }
                    }
                    else if (m_connect.is_linked())
                    {
                        m_istream.parse(pack) || m_ostream.parse(pack);
                    }
                }
            }
            catch(const boost::system::system_error& err)
            {
                on_error(err.code());
            }
        }
    }

    void do_write()
    {
        unique_lock lock(m_mutex);

        while (m_connect.is_alive())
        {
            try
            {
                m_wcon.wait_for(lock, std::chrono::seconds(15), [&]()
                {
                    return !m_connect.is_alive() || m_connect.is_charged() 
                           || m_istream.is_charged() || m_ostream.is_charged();
                });

                if (m_connect.is_alive())
                {
                    auto pack = m_packer.make_packet();
                    
                    if (m_connect.imbue(pack))
                    {
                        if (m_connect.is_local_fin(pack))
                        {
                            m_istream.error(boost::asio::error::operation_aborted);
                            m_ostream.error(boost::asio::error::operation_aborted);
                        }
                    }
                    else if (m_connect.is_linked())
                    {
                        m_istream.imbue(pack) || m_ostream.imbue(pack);
                    }

                    send_packet(lock, pack);
                }
            }
            catch(const boost::system::system_error& err)
            {
                on_error(err.code());
            }
        }
    }

    void do_callback()
    {
        try
        {
            m_io.run();
        }
        catch (const boost::system::system_error& err)
        {
            std::cout << err.what();
            throw;
        }
    }

    void terminate()
    {
        unique_lock lock(m_mutex);

        if (m_connect.is_alive())
        {
            m_connect.error(boost::asio::error::operation_aborted);
            m_istream.error(boost::asio::error::operation_aborted);
            m_ostream.error(boost::asio::error::operation_aborted);
        }

        m_io.stop();
    }

public:

    channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer)
        : m_udp(bind, peer)
        , m_connect(m_io)
        , m_istream(m_io)
        , m_ostream(m_io)
        , m_rjob(boost::bind(&channel::do_read, this))
        , m_wjob(boost::bind(&channel::do_write, this))
        , m_cjob(boost::bind(&channel::do_callback, this))
    {
    }

    ~channel()
    {
        terminate();

        try
        {
            if (m_rjob.joinable())
                m_rjob.join();
        }
        catch (const std::exception& ex)
        {
            std::cout << ex.what() << std::endl;
        }

        try
        {
            if (m_wjob.joinable())
                m_wjob.join();
        }
        catch (const std::exception& ex)
        {
            std::cout << ex.what() << std::endl;
        }
        
        try
        {
            if (m_cjob.joinable() && std::this_thread::get_id() != m_cjob.get_id())
                m_cjob.join();
        }
        catch (const std::exception &ex)
        {
            std::cout << ex.what() << std::endl;
        }
    }

    boost::asio::ip::udp::endpoint local_endpoint() const
    {
        return m_udp.local_endpoint();
    }

    boost::asio::ip::udp::endpoint remote_endpoint() const
    {
        return m_udp.remote_endpoint();
    }

    void shutdown(const callback& handle)
    {
        unique_lock lock(m_mutex);

        if (!m_connect.is_alive() || !m_connect.is_linked() || m_connect.is_shutdowning())
        {
            boost::system::error_code error = m_connect.is_shutdowning() ? 
                boost::asio::error::already_started : m_connect.is_alive() ? 
                    boost::asio::error::not_connected : boost::asio::error::broken_pipe;

            m_io.post(boost::bind(handle, error));
            return;
        }

        m_connect.shutdown(handle);
    }

    void connect(const callback& handle)
    {
        unique_lock lock(m_mutex);

        if (!m_connect.is_alive() || m_connect.is_linked() || m_connect.is_connecting())
        {
            boost::system::error_code error = m_connect.is_connecting() ? 
                boost::asio::error::already_started : m_connect.is_linked() ? 
                    boost::asio::error::already_connected : boost::asio::error::broken_pipe;

            m_io.post(boost::bind(handle, error));
            return;
        }

        m_connect.connect(handle);
    }

    void accept(const callback& handle)
    {
        unique_lock lock(m_mutex);

        if (!m_connect.is_alive() || m_connect.is_linked() || m_connect.is_connecting())
        {
            boost::system::error_code error = m_connect.is_connecting() ? 
                boost::asio::error::already_started : m_connect.is_linked() ? 
                    boost::asio::error::already_connected : boost::asio::error::broken_pipe;

            m_io.post(boost::bind(handle, error));
            return;
        }

        m_connect.accept(handle);
    }

    void read(const mutable_buffer& buf, const io_callback& handle)
    {
        unique_lock lock(m_mutex);

        if (!m_connect.is_alive() || !m_connect.is_linked())
        {
            boost::system::error_code error = m_connect.is_alive() ? 
                boost::asio::error::not_connected : boost::asio::error::broken_pipe;

            m_io.post(boost::bind(handle, error, 0));
            return;
        }

        m_istream.append(buf, handle);
    }

    void write(const const_buffer& buf, const io_callback& handle)
    {
        unique_lock lock(m_mutex);
                
        if (!m_connect.is_alive() || !m_connect.is_linked())
        {
            boost::system::error_code error = m_connect.is_alive() ? 
                boost::asio::error::not_connected : boost::asio::error::broken_pipe;

            m_io.post(boost::bind(handle, error, 0));
            return;
        }

        m_ostream.append(buf, handle);
        m_wcon.notify_all();
    }

private:

    boost::asio::io_context  m_io;
    udp::channel             m_udp;
    packet_factory           m_packer;
    connect_handler          m_connect;
    istream_handler          m_istream;
    ostream_handler          m_ostream;
    std::mutex               m_mutex;
    std::condition_variable  m_wcon;
    std::thread              m_rjob;
    std::thread              m_wjob;
    std::thread              m_cjob;
};

std::shared_ptr<channel> create_channel(const boost::asio::ip::udp::endpoint& bind, const boost::asio::ip::udp::endpoint& peer)
{
    return std::make_shared<channel>(bind, peer);
}

}
