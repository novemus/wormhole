#include <map>
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

class salt
{
    struct packet : public buffer
    {
        enum flag 
        {
            con = 0x01,
            sht = 0x02,
            trn = 0x04,
            ack = 0x08
        };

        packet() : buffer(1432) {}

        uint64_t salt() { return get_qword(0); }
        uint8_t sign() { return get_byte(8); }
        uint8_t flags() { return get_byte(9); }
        uint32_t number() { return get_dword(10); }
        uint16_t check_sum() { return get_word(14); }
        buffer payload() const { return buffer::push_head(16); }

        static std::shared_ptr<packet> make_keepalive();
        static std::shared_ptr<packet> make_empty();
    };

    class udp
    {
        boost::asio::io_service        m_io;
        boost::asio::ip::udp::socket   m_socket;
        boost::asio::ip::udp::endpoint m_peer;
        uint64_t                       m_mask;

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

        std::shared_ptr<packet> make_opaque(std::shared_ptr<packet> pack)
        {

        }

        std::shared_ptr<packet> make_transparent(std::shared_ptr<packet> pack)
        {

        }

    public:

        udp(const endpoint& bind, const endpoint& peer, uint64_t mask)
            : m_socket(m_io)
            , m_peer(resolve_endpoint(peer))
            , m_mask(mask)
        {
            boost::asio::ip::udp::endpoint local = resolve_endpoint(bind);

            m_socket.open(local.protocol());

            static const size_t SOCKET_BUFFER_SIZE = 1048576;

            m_socket.non_blocking(true);
            m_socket.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            m_socket.set_option(boost::asio::socket_base::send_buffer_size(SOCKET_BUFFER_SIZE));
            m_socket.set_option(boost::asio::socket_base::receive_buffer_size(SOCKET_BUFFER_SIZE));

            m_socket.bind(local);
        }

        ~udp()
        {
            if (m_socket.is_open())
            {
                boost::system::error_code ec;
                m_socket.shutdown(boost::asio::ip::udp::socket::shutdown_both, ec);
                m_socket.close(ec);
            }
        }

        void recv(std::shared_ptr<packet> pack)
        {
            auto timer = [start = boost::posix_time::microsec_clock::universal_time()]()
            {
                return boost::posix_time::microsec_clock::universal_time() - start;
            };

            auto in = packet::make_empty();
            while (timer().total_seconds() < 30)
            {
                boost::asio::ip::udp::endpoint src;
                size_t size = exec([&](const io_callback& callback)
                {
                    m_socket.async_receive_from(boost::asio::buffer(in->data(), in->size()), src, callback);
                });

                if (src == m_peer)
                {
                    in->move_tail(in->size() - size, true);
                    in = make_transparent(in);

                    if (in)
                    {
                        if (size > pack->size())
                            throw std::runtime_error("too small buffer");

                        std::memcmp(pack->data(), in->data(), size);
                        pack->move_tail(pack->size() - size, true);

                        return;
                    }
                }
            }

            throw boost::system::error_code(boost::asio::error::operation_aborted);
        }

        void send(std::shared_ptr<packet> pack)
        {
            auto out = make_opaque(pack);

            size_t size = exec([&](const io_callback& callback)
            {
                m_socket.async_send_to(boost::asio::buffer(out->data(), out->size()), m_peer, callback);
            });

            if (size < out->size())
                throw std::runtime_error("can't send message");
        }
    };

    struct packet_handler
    {
        virtual ~packet_handler() {}
        virtual bool error(const boost::system::error_code& error) = 0;
        virtual bool take(std::shared_ptr<packet> packet) = 0;
        virtual bool give(std::shared_ptr<packet> packet) = 0;
    };

    struct session_handler : public packet_handler
    {
        session_handler()
        {
        }
        
        bool error(const boost::system::error_code& error) override;
        
        bool take(std::shared_ptr<packet> packet) override;
        
        bool give(std::shared_ptr<packet> packet) override;

        void shutdown(const callback& handler)
        {
            m_shut = handler;
        }

        void connect(const callback& handler)
        {
            m_conn = handler;
        }

        void accept(const callback& handler)
        {
            m_conn = handler;
        }

        bool alive()
        {

        }

    private:

        callback m_conn;
        callback m_shut;
    };

    struct write_handler : public packet_handler
    {
        write_handler() {}

        bool error(const boost::system::error_code& error) override;
        bool take(std::shared_ptr<packet> packet) override;
        bool give(std::shared_ptr<packet> packet) override;
        bool append(std::shared_ptr<buffer>, callback handler) {}

    private:

        uint32_t m_number = 0;
        time_t m_time = 0;
        std::map<uint32_t, buffer> m_parts;
        std::list<std::pair<std::shared_ptr<buffer>, callback>> m_trans;
    };

    struct read_handler : public packet_handler
    {
        read_handler() {}
        
        bool error(const boost::system::error_code& error) override;
        bool take(std::shared_ptr<packet> packet) override;
        bool give(std::shared_ptr<packet> packet) override;
        bool append(std::shared_ptr<buffer>, callback handler) {}
    
    private:

        uint32_t m_number = 0;
        std::map<uint32_t, buffer> m_parts;
        std::list<std::pair<std::shared_ptr<buffer>, callback>> m_trans;
    };

    void on_error(const boost::system::error_code& error)
    {

    }

    void on_connected(const boost::system::error_code& error)
    {

    }

    void on_accepted(const boost::system::error_code& error)
    {

    }

    void on_shutdown(const boost::system::error_code& error)
    {

    }

    void do_read()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        while (m_session && m_session->alive())
        {
            try
            {
                m_condition.wait_for(lock, std::chrono::seconds(5));
                
                if (m_session)
                {
                    auto pack = std::shared_ptr<packet>();
                    m_udp->recv(pack);

                    m_session->take(pack);

                    if (m_session->alive())
                    {
                        if (m_writer)
                            m_writer->take(pack);
                        if (m_reader)
                            m_reader->take(pack);
                    }
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

        while (m_session && m_session->alive())
        {
            try
            {
                m_condition.wait_for(lock, std::chrono::seconds(5));

                if (m_session)
                {
                    auto pack = std::shared_ptr<packet>();

                    if (m_session->give(pack))
                        m_udp->send(pack);

                    if (m_session->alive())
                    {
                        if (m_writer)
                        {
                            if (m_writer->give(pack))
                                m_udp->send(pack);
                        }

                        if (m_reader)
                        {
                            if (m_reader->give(pack))
                                m_udp->send(pack);
                        }
                    }
                }
            }
            catch(const boost::system::error_code& ec)
            {
                on_error(ec);
            }
        }
    }

public:

    salt(const endpoint& bind, const endpoint& peer, uint64_t mask)
        : m_udp(std::make_shared<udp>(bind, peer, mask))
        , m_session(std::make_shared<session_handler>())
        , m_writer(std::make_shared<write_handler>())
        , m_reader(std::make_shared<read_handler>())
    {
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

    std::shared_ptr<udp>             m_udp;
    std::shared_ptr<session_handler> m_session;
    std::shared_ptr<write_handler>   m_writer;
    std::shared_ptr<read_handler>    m_reader;
    std::mutex                       m_mutex;
    std::condition_variable          m_condition;
};

std::shared_ptr<salt> create_salt_channel(const endpoint& bind, const endpoint& peer, uint64_t mask)
{
    return std::make_shared<salt>(bind, peer, mask);
}

}}
