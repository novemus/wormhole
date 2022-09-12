#include <map>
#include <set>
#include <list>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/shared_array.hpp>


namespace tubus { namespace network {

typedef std::pair<std::string, uint16_t> endpoint;
typedef std::function<void(const boost::system::error_code&)> callback;
typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;

class buffer
{
    size_t m_size;
    boost::shared_array<uint8_t> m_buffer;

    uint8_t* m_beg;
    uint8_t* m_end;

    buffer(boost::shared_array<uint8_t> buffer, size_t size, uint8_t* beg, uint8_t* end)
        : m_size(size)
        , m_buffer(buffer)
        , m_beg(beg)
        , m_end(end)
    {
    }

public:

    buffer(size_t size, size_t padding = 0)
        : m_size(size + padding)
        , m_buffer(new uint8_t[m_size])
        , m_beg(m_buffer.get() + padding)
        , m_end(m_buffer.get() + m_size)
    {
        std::memset(m_buffer.get(), 0, m_size);
    }

    buffer(const char* data, size_t padding = 0) 
        : m_size(std::strlen(data) + padding)
        , m_buffer(new uint8_t[m_size])
        , m_beg(m_buffer.get() + padding)
        , m_end(m_buffer.get() + m_size)
    {
        std::memset(m_buffer.get(), 0, m_end - m_beg);
        std::memcpy(m_beg, data, std::strlen(data));
    }
    
    buffer(const std::vector<uint8_t>& data, size_t padding = 0) 
        : m_size(data.size() + padding)
        , m_buffer(new uint8_t[m_size])
        , m_beg(m_buffer.get() + padding)
        , m_end(m_buffer.get() + m_size)
    {
        std::memset(m_buffer.get(), 0, m_end - m_beg);
        std::memcpy(m_beg, data.data(), data.size());
    }

    buffer(const buffer& other)
        : m_size(other.m_size)
        , m_buffer(other.m_buffer)
        , m_beg(other.m_beg)
        , m_end(other.m_end)
    {
    }

    virtual ~buffer() { }

    size_t head() const
    {
        return m_beg - m_buffer.get();
    }

    size_t tail() const
    {
        return m_buffer.get() + m_size - m_end;
    }

    const uint8_t* data() const
    {
        return m_beg;
    }

    uint8_t* data()
    {
        return m_beg;
    }

    uint8_t* begin()
    {
        return m_beg;
    }

    const uint8_t* begin() const
    {
        return m_beg;
    }

    uint8_t* end()
    {
        return m_end;
    }

    const uint8_t* end() const
    {
        return m_end;
    }

    size_t size() const
    {
        return m_end - m_beg;
    }

    void set_byte(size_t pos, uint8_t val)
    {
        uint8_t* ptr = m_beg + pos;
        if (ptr >= m_end)
            throw std::out_of_range("set_byte: out of range");
        *ptr = val;
    }

    uint8_t get_byte(size_t pos) const
    {
        uint8_t* ptr = m_beg + pos;
        if (ptr >= m_end)
            throw std::out_of_range("get_byte: out of range");
        return *ptr;
    }

    void set_word(size_t pos, uint16_t val)
    {
        uint8_t* ptr = m_beg + pos;
        if (ptr + sizeof(uint16_t) > m_end)
            throw std::out_of_range("set_word: out of range");
        *(uint16_t*)(ptr) = htons(val);
    }

    uint16_t get_word(size_t pos) const
    {
        uint8_t* ptr = m_beg + pos;
        if (ptr + sizeof(uint16_t) > m_end)
            throw std::out_of_range("get_word: out of range");
        return ntohs(*(uint16_t*)ptr);
    }

    void set_dword(size_t pos, uint32_t val)
    {
        uint8_t* ptr = m_beg + pos;
        if (ptr + sizeof(uint32_t) > m_end)
            throw std::out_of_range("set_dword: out of range");
        *(uint32_t*)(ptr) = htonl(val);
    }

    uint32_t get_dword(size_t pos) const
    {
        uint8_t* ptr = m_beg + pos;
        if (ptr + sizeof(uint32_t) > m_end)
            throw std::out_of_range("get_dword: out of range");
        return ntohl(*(uint32_t*)ptr);
    }

    uint64_t get_qword(size_t pos) const
    {
        uint8_t* ptr = m_beg + pos;
        if (ptr + sizeof(uint64_t) > m_end)
            throw std::out_of_range("get_qword: out of range");
        return le64toh(*(uint64_t*)ptr);
    }

    void set_qword(size_t pos, uint64_t val)
    {
        uint8_t* ptr = m_beg + pos;
        if (ptr + sizeof(uint64_t) > m_end)
            throw std::out_of_range("set_qword: out of range");
        *(uint64_t*)(ptr) = htole64(val);
    }

    buffer pop_head(size_t size) const
    {
        uint8_t* ptr = m_beg - size;
        if (ptr < m_buffer.get())
            throw std::out_of_range("pop_head: out of range");
        return buffer(m_buffer, m_size, ptr, m_end);
    }

    buffer push_head(size_t size) const
    {
        uint8_t* ptr = m_beg + size;
        if (ptr > m_end)
            throw std::runtime_error("push_head: out of range");
        return buffer(m_buffer, m_size, ptr, m_end);
    }

    buffer push_tail(size_t size) const
    {
        uint8_t* ptr = m_end + size;
        if (ptr > m_buffer.get() + m_size)
            throw std::runtime_error("push_tail: out of range");
        return buffer(m_buffer, m_size, m_beg, ptr);
    }

    buffer pop_tail(size_t size) const
    {
        uint8_t* ptr = m_end - size;
        if (ptr < m_beg)
            throw std::runtime_error("pop_tail: out of range");
        return buffer(m_buffer, m_size, m_beg, ptr);
    }

    void move_head(size_t size, bool top)
    {
        uint8_t* ptr = top ? m_beg - size : m_beg + size;
        if (ptr < m_buffer.get() || ptr > m_end)
            throw std::out_of_range("pop_head: out of range");
        m_beg = ptr;
    }

    void move_tail(size_t size, bool top)
    {
        uint8_t* ptr = top ? m_end - size : m_end + size;
        if (ptr < m_beg || ptr > m_buffer.get() + m_size)
            throw std::runtime_error("pop_tail: out of range");
        m_end = ptr;
    }
};

const uint8_t SALT_SIGN = 0x99;

class salt
{
    struct packet : public buffer
    {
        enum flag 
        {
            syn = 0x01,
            fin = 0x02,
            psh = 0x04,
            ack = 0x08
        };

        packet() : buffer(10000)
        {
            memset(data(), 0, 16);
            set_byte(0, SALT_SIGN);
            set_byte(1, 0x10);
        }

        uint8_t sign() { return get_byte(0); }
        uint8_t version() { return get_byte(1); }
        uint32_t pin() { return get_dword(2); }
        uint16_t flags() { return get_word(6); }
        uint64_t cursor() { return get_qword(8); }
        void set_pin(uint32_t v) { return set_dword(2, v); }
        bool has_flag(flag v) { return flags() & v; }
        void set_flag(uint16_t v) { set_word(6, flags() | v); }
        void set_cursor(uint64_t v) { return set_qword(8, v); }
        buffer payload() const { return buffer::push_head(16); }
    };

    class udp_channel
    {
        boost::asio::io_context        m_io;
        boost::asio::ip::udp::socket   m_socket;
        boost::asio::ip::udp::endpoint m_peer;

        template<typename io_call> size_t exec(const io_call& invoke)
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
                        _err_ << ex.what();
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

            if (code)
                throw boost::system::system_error(code);

            return length;
        }

        boost::asio::ip::udp::endpoint resolve_endpoint(const endpoint& ep)
        {
            boost::asio::ip::udp::resolver resolver(m_io);
            boost::asio::ip::udp::resolver::query query(boost::asio::ip::udp::v4(), ep.first, std::to_string(ep.second));
            boost::asio::ip::udp::endpoint endpoint = *resolver.resolve(query);

            return *resolver.resolve(query);
        }

    public:

        udp_channel(const endpoint& bind, const endpoint& peer)
            : m_socket(m_io)
            , m_peer(resolve_endpoint(peer))
        {
            boost::asio::ip::udp::endpoint local = resolve_endpoint(bind);

            m_socket.open(local.protocol());
            m_socket.non_blocking(true);
            m_socket.bind(local);
        }

        ~udp_channel()
        {
            if (m_socket.is_open())
            {
                boost::system::error_code ec;
                m_socket.shutdown(boost::asio::ip::udp::socket::shutdown_both, ec);
                m_socket.close(ec);
            }
        }

        void receive(std::shared_ptr<packet> pack)
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
                    m_socket.async_receive_from(boost::asio::buffer(pack->data(), pack->size()), src, callback);
                });

                if (src == m_peer)
                {
                    if (size > pack->size())
                        throw std::runtime_error("too small buffer");

                    pack->move_tail(pack->size() - size, true);
                    return;
                }
            }

            throw boost::system::error_code(boost::asio::error::operation_aborted);
        }

        void send(std::shared_ptr<packet> pack)
        {
            size_t size = exec([&](const io_callback& callback)
            {
                m_socket.async_send_to(boost::asio::buffer(pack->data(), pack->size()), m_peer, callback);
            });

            if (size < pack->size())
                throw std::runtime_error("can't send message");
        }
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
            , m_lpin(1)  // TODO: random value
        {
        }

        std::shared_ptr<packet> make_packet()
        {
            auto pack = std::make_shared<packet>();
            pack->set_pin(m_lpin);
            return pack;
        }

        void reset()
        {
            m_alive = true;
            m_linked = false;
            m_lpin++;
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

        bool parse(std::shared_ptr<packet> pack)
        {
            if (pack->has_flag(packet::syn) && m_rpin == 0 || m_rpin == pack->pin())
            {
                m_rpin = pack->pin();

                if (pack->has_flag(packet::ack))
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
            else if (pack->has_flag(packet::fin) && m_rpin == pack->pin())
            {
                m_linked = false;

                if (pack->has_flag(packet::ack))
                {
                    m_rpin = 0;
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

            return !(m_linked && m_rpin != 0 && m_rpin == pack->pin());
        }

        bool imbue(std::shared_ptr<packet> pack)
        {
            auto iter = m_jobs.begin();
            if (iter != m_jobs.end())
            {
                switch (*iter)
                {
                    case job::snd_syn:
                    {
                        pack->set_flag(packet::syn);
                        break;
                    }
                    case job::snd_ack_syn:
                    {
                        pack->set_flag(packet::syn | packet::ack);
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
                        pack->set_flag(packet::fin);
                        break;
                    }
                    case job::snd_ack_fin:
                    {
                        pack->set_flag(packet::fin | packet::ack);
                        m_jobs.erase(job::snd_ack_fin);
                        m_linked = false;
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

                return true;
            }

            return false;
        }

        bool charged() const 
        { 
            return m_jobs.empty();
        }

        bool alive() const { return m_alive; }

        bool linked() const { return m_linked; }

        void shutdown(const callback& handler)
        {
            if (!m_alive)
            {
                m_io.post(boost::bind(handler, boost::asio::error::no_permission));
                return;
            }
            on_shutdown = handler;
            m_jobs.insert(job::snd_fin);
        }

        void connect(const callback& handler)
        {
            if (!m_alive)
            {
                m_io.post(boost::bind(handler, boost::asio::error::no_permission));
                return;
            }

            on_connect = handler;
            m_jobs.insert(job::snd_syn);
        }

        void accept(const callback& handler)
        {
            if (!m_alive)
            {
                m_io.post(boost::bind(handler, boost::asio::error::no_permission));
                return;
            }

            on_connect = handler;
        }

    private:


        boost::asio::io_context& m_io;
        bool                     m_alive;
        bool                     m_linked;
        uint32_t                 m_lpin;
        uint32_t                 m_rpin;
        std::set<job>            m_jobs;
        callback                 on_connect;
        callback                 on_shutdown;
    };

    struct ostream_handler
    {
        ostream_handler(boost::asio::io_context& io) {}
        void reset() { }
        void error(const boost::system::error_code& error) {}
        bool parse(std::shared_ptr<packet> pack) {}
        bool imbue(std::shared_ptr<packet> pack) {}
        bool charged() { return true; }
        void append(std::shared_ptr<buffer>, callback handler) {}

    private:

        uint32_t m_cursor = 0;
        time_t m_time = 0;
        std::map<uint32_t, buffer> m_parts;
        std::list<std::pair<std::shared_ptr<buffer>, callback>> m_trans;
    };

    struct istream_handler
    {
        istream_handler(boost::asio::io_context& io) {}
        void reset() { }
        void error(const boost::system::error_code& error) {}
        bool parse(std::shared_ptr<packet> pack) {}
        bool imbue(std::shared_ptr<packet> pack) {}
        bool charged() { return true; }
        void append(std::shared_ptr<buffer>, callback handler) {}
    
    private:

        uint32_t m_cursor = 0;
        std::map<uint32_t, buffer> m_parts;
        std::list<std::pair<std::shared_ptr<buffer>, callback>> m_trans;
    };

    void on_error(const boost::system::error_code& error)
    {

    }

    void send_packet(std::unique_lock<std::mutex>& lock, std::shared_ptr<packet> pack)
    {
        lock.unlock();
        try
        {
            m_channel.send(pack);
        }
        catch (...)
        {
            lock.lock();
            throw;
        }
        lock.lock();
    }

    void receive_packet(std::unique_lock<std::mutex>& lock, std::shared_ptr<packet> pack)
    {
        lock.unlock();
        try
        {
            m_channel.receive(pack);
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
        std::unique_lock<std::mutex> lock(m_mutex);
        
        while (m_connect.alive())
        {
            try
            {
                auto pack = m_connect.make_packet();
                receive_packet(lock, pack);

                if (m_connect.linked())
                {
                    m_connect.parse(pack) || m_istream.parse(pack) || m_ostream.parse(pack);
                }
            }
            catch(const boost::system::error_code& ec)
            {
                on_error(ec);
            }
        }
    }

    void do_write()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        while (m_connect.alive())
        {
            try
            {
                m_wcon.wait_for(lock, std::chrono::seconds(30), [&]()
                {
                    return m_connect.charged() || m_istream.charged() || m_ostream.charged();
                });

                if (m_connect.charged())
                {
                    auto pack = m_connect.make_packet();
                    if (m_connect.imbue(pack))
                        send_packet(lock, pack);
                }

                if (m_connect.linked() && m_istream.charged())
                {
                    auto pack = m_connect.make_packet();
                    if (m_istream.imbue(pack))
                        send_packet(lock, pack);
                }
                
                if (m_connect.linked() && m_ostream.charged())
                {
                    auto pack = m_connect.make_packet();
                    if (m_ostream.imbue(pack))
                        send_packet(lock, pack);
                }
            }
            catch(const boost::system::error_code& ec)
            {
                on_error(ec);
            }
        }
    }

    void do_callback()
    {
        boost::system::error_code error;
        do
        {
            m_io.run(error);

            if (error)
                std::cout << error.message() << std::endl;
        }
        while (error && error != boost::asio::error::operation_aborted);
    }

public:

    salt(const endpoint& bind, const endpoint& peer)
        : m_channel(bind, peer)
        , m_connect(m_io)
        , m_istream(m_io)
        , m_ostream(m_io)
        , m_rjob(std::async(std::launch::async, &salt::do_read, this))
        , m_wjob(std::async(std::launch::async, &salt::do_write, this))
        , m_cjob(std::async(std::launch::async, &salt::do_callback, this))
    {
    }

    ~salt()
    {
        try
        {
            m_rjob.wait();
        }
        catch (const std::exception& ex)
        {
            std::cout << ex.what() << std::endl;
        }

        try
        {
            m_wjob.wait();
        }
        catch (const std::exception& ex)
        {
            std::cout << ex.what() << std::endl;
        }
        
        try
        {
            m_cjob.wait();
        }
        catch (const std::exception &ex)
        {
            std::cout << ex.what() << std::endl;
        }
    }

    void shutdown(const callback& handler)
    {

    }

    void connect(const callback& handler)
    {

    }

    void accept(const callback& handler)
    {

    }

    void read(std::shared_ptr<buffer> buf, const callback& handler)
    {

    }

    void write(std::shared_ptr<buffer> buf, const callback& handler)
    {

    }

private:

    udp_channel              m_channel;
    boost::asio::io_context  m_io;
    connect_handler          m_connect;
    istream_handler          m_istream;
    ostream_handler          m_ostream;
    std::mutex               m_mutex;
    std::condition_variable  m_wcon;
    std::future<void>        m_rjob;
    std::future<void>        m_wjob;
    std::future<void>        m_cjob;
};

std::shared_ptr<salt> create_salt_channel(const endpoint& bind, const endpoint& peer, uint64_t mask)
{
    return std::make_shared<salt>(bind, peer, mask);
}

}}
