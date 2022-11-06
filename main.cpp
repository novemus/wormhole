#include <cstdlib>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/bind/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

class tubus
{
    boost::asio::io_service        m_io;
    boost::asio::deadline_timer    m_timer;
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::ip::tcp::socket   m_sink;
    boost::asio::ip::tcp::socket   m_source;
    boost::asio::ip::tcp::endpoint m_from;
    boost::asio::ip::tcp::endpoint m_to;

    enum
    {
        max_length = 1024
    };

    char m_source_data[max_length];
    char m_sink_data[max_length];

    bool is_matched(const boost::asio::ip::tcp::endpoint& source)
    {
        return (m_from.address().is_unspecified() || m_from.address() == source.address()) && (m_from.port() == 0 || m_from.port() == source.port());
    }

public:

    tubus(const boost::asio::ip::tcp::endpoint& sink, const boost::asio::ip::tcp::endpoint& source, const boost::asio::ip::tcp::endpoint& from, const boost::asio::ip::tcp::endpoint& to)
        : m_timer(m_io)
        , m_acceptor(m_io, sink)
        , m_sink(m_io)
        , m_source(m_io)
        , m_from(from)
        , m_to(to)
    {
        m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

        m_source.open(source.protocol());
        m_source.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        m_source.bind(source);

        std::cout << "bound sink to " << sink << " and source to " << source 
                  << " to transmit from " << from << " to " << to << std::endl;
    }

    ~tubus()
    {
        if (m_sink.is_open())
        {
            m_sink.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
            m_sink.close();
        }

        if (m_source.is_open())
        {
            m_source.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
            m_source.close();
        }

        std::cout << "close" << std::endl;
    }

    void start()
    {
        m_acceptor.async_accept(m_sink, boost::bind(&tubus::handle_accept, this, boost::asio::placeholders::error));
        m_io.run();
    }

    void handle_accept(const boost::system::error_code& error)
    {
        if (!error)
        {
            if (is_matched(m_sink.remote_endpoint()))
            {
                std::cout << "accepted " << m_sink.remote_endpoint() << std::endl;

                m_source.async_connect(m_to, boost::bind(&tubus::handle_connect, this, boost::asio::placeholders::error));
            }
            else
            {
                std::cout << "rejected " << m_sink.remote_endpoint() << std::endl;

                m_sink = boost::asio::ip::tcp::socket(m_io);
                m_acceptor.async_accept(m_sink, boost::bind(&tubus::handle_accept, this, boost::asio::placeholders::error));
            }
        }
        else
        {
            throw boost::system::system_error(error);
        }
    }

    void handle_connect(const boost::system::error_code& error)
    {
        if (!error)
        {
            std::cout << "connected " << m_to << std::endl;

            read_sink();
            read_source();
        }
        else
        {
            m_timer.expires_from_now(boost::posix_time::seconds(1));
            m_timer.async_wait([&](const boost::system::error_code& error)
            {
                if (error == boost::asio::error::operation_aborted)
                    return;
                
                if (error)
                    throw boost::system::system_error(error);

                m_source.async_connect(m_to, boost::bind(&tubus::handle_connect, this, boost::asio::placeholders::error));
            });
        }
    }

    void read_sink()
    {
        m_sink.async_read_some(boost::asio::buffer(m_sink_data, max_length),
                                boost::bind(&tubus::handle_read_sink, this,
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred));
    }

    void read_source()
    {
        m_source.async_read_some(boost::asio::buffer(m_source_data, max_length),
                                boost::bind(&tubus::handle_read_source, this,
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred));
    }

    void handle_read_sink(const boost::system::error_code& error, size_t bytes_transferred)
    {
        if (!error)
        {
            boost::asio::async_write(m_source,
                                     boost::asio::buffer(m_sink_data, bytes_transferred),
                                     boost::bind(&tubus::handle_write_source, this,
                                                 boost::asio::placeholders::error));
        }
        else
        {
            throw boost::system::system_error(error);
        }
    }

    void handle_read_source(const boost::system::error_code& error, size_t bytes_transferred)
    {
        if (!error)
        {
            boost::asio::async_write(m_sink,
                                    boost::asio::buffer(m_source_data, bytes_transferred),
                                    boost::bind(&tubus::handle_write_sink, this,
                                                boost::asio::placeholders::error));
        }
        else
        {
            throw boost::system::system_error(error);
        }
    }

    void handle_write_source(const boost::system::error_code &error)
    {
        if (!error)
        {
            read_sink();
        }
        else
        {
            throw boost::system::system_error(error);
        }
    }

    void handle_write_sink(const boost::system::error_code &error)
    {
        if (!error)
        {
            read_source();
        }
        else
        {
            throw boost::system::system_error(error);
        }
    }
};


int main(int argc, char *argv[])
{
    try
    {
        if (argc != 9)
        {
            std::cerr << "Usage: tubus <sink_addr> <sink_port> <source_addr> <source_port> <from_addr> <from_port> <to_addr> <to_port>\n";
            return 1;
        }

        boost::asio::ip::tcp::endpoint sink(boost::asio::ip::make_address(argv[1]), std::atoi(argv[2]));
        boost::asio::ip::tcp::endpoint source(boost::asio::ip::make_address(argv[3]), std::atoi(argv[4]));
        boost::asio::ip::tcp::endpoint from(boost::asio::ip::make_address(argv[5]), std::atoi(argv[6]));
        boost::asio::ip::tcp::endpoint to(boost::asio::ip::make_address(argv[7]), std::atoi(argv[8]));

        tubus pin(sink, source, from, to);
        pin.start();
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
