#pragma once

#include "buffer.h"
#include <list>
#include <iostream>
#include <functional>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace salt {

template<class protocol> class transport
{
    typedef std::function<void(const boost::system::error_code&, size_t)> io_callback;
    typedef std::function<void(const io_callback&)> io_call;

    boost::asio::io_context m_io;
    protocol::socket m_socket;
    protocol::endpoint m_peer;

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

    transport(const protocol::socket& bind, const protocol::socket& peer)
        : m_socket(m_io)
        , m_peer(peer)
    {
        m_socket.open(bind.protocol());
        m_socket.non_blocking(true);
        m_socket.bind(bind);
    }

    ~transport()
    {
        if (m_socket.is_open())
        {
            boost::system::error_code ec;
            m_socket.close(ec);
        }
    }

    size_t receive(const mutable_buffer& buf) noexcept(false)
    {
        auto timer = [start = boost::posix_time::microsec_clock::universal_time()]()
        {
            return boost::posix_time::microsec_clock::universal_time() - start;
        };

        while (timer().total_seconds() < 30)
        {
            typename protocol::endpoint src;
            size_t size = exec([&](const io_callback& callback)
            {
                m_socket.async_receive_from(boost::asio::buffer(buf.data(), buf.size()), src, callback);
            });

            if (src == m_peer)
                return size;
        }

        throw boost::system::error_code(boost::asio::error::operation_aborted);
    }

    size_t send(const const_buffer& buf) noexcept(false)
    {
        size_t size = exec([&](const io_callback& callback)
        {
            m_socket.async_send_to(boost::asio::buffer(buf.data(), buf.size()), m_peer, callback);
        });

        if (size < buf.size())
            throw std::runtime_error("can't send message");
    }
};

}
