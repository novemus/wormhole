#include "wormhole.h"
#include <regex>
#include <iostream>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>

#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#elif _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#define gettid() GetCurrentThreadId()
#endif

namespace boost::asio::ip {

template<class proto>
void validate(boost::any& result, const std::vector<std::string>& values, basic_endpoint<proto>*, int)
{
    const std::string& str = boost::program_options::validators::get_single_string(values);

    std::smatch match;
    if (std::regex_search(str, match, std::regex("(\\w+://)?(.+):(.+)")))
        result = boost::any(basic_endpoint<proto>(make_address(match[2].str()), boost::lexical_cast<uint16_t>(match[3].str())));
    else
        throw boost::program_options::validation_error(boost::program_options::validation_error::invalid_option_value);
}

}

void init_logger(const std::string& file, boost::log::trivial::severity_level level)
{
    boost::log::add_common_attributes();

    auto formatter = [](const boost::log::record_view& view, boost::log::formatting_ostream& strm)
    {
        strm << boost::log::extract<boost::posix_time::ptime>("TimeStamp", view)
             << " [" << gettid() << "] "
             << view[boost::log::trivial::severity] << ": "
             << view[boost::log::expressions::message];
    };

    if (file.empty())
    {
        boost::log::add_console_log(
            std::cout,
            boost::log::keywords::format = formatter,
            boost::log::keywords::severity = level
            );
    }
    else
    {
        boost::log::add_file_log(
            boost::log::keywords::file_name = file,
            boost::log::keywords::rotation_size = 500 * 1024 * 1024,
            boost::log::keywords::format = formatter,
            boost::log::keywords::severity = level
            );
    }
}

int main(int argc, char *argv[])
{
    boost::program_options::options_description desc("wormhole options");
    desc.add_options()
        ("help", "produce help message")
        ("purpose", boost::program_options::value<std::string>()->required(), "wormhole purpose: <export|import>")
        ("service", boost::program_options::value<boost::asio::ip::tcp::endpoint>()->required(), "endpoint of the exported/imported service: <ip:port>")
        ("gateway", boost::program_options::value<boost::asio::ip::udp::endpoint>()->required(), "gateway endpoint of the transport tunnel: <ip:port>")
        ("faraway", boost::program_options::value<boost::asio::ip::udp::endpoint>()->required(), "faraway endpoint of the transport tunnel: <ip:port>")
        ("obscure", boost::program_options::value<uint64_t>()->default_value(0), "pre-shared key to obscure the transport tunnel: <number>")
        ("log-file", boost::program_options::value<std::string>()->default_value(""), "log file path: <path>")
        ("log-level", boost::program_options::value<boost::log::trivial::severity_level>()->default_value(boost::log::trivial::info), "log level: <trace|debug|info|warning|error|fatal>");

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    try
    {
        boost::program_options::notify(vm);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        std::cout << desc << std::endl;
        return 1;
    }

    try
    {
        init_logger(vm["log-file"].as<std::string>(), vm["log-level"].as<boost::log::trivial::severity_level>());

        auto service = vm["service"].as<boost::asio::ip::tcp::endpoint>();
        auto gateway = vm["gateway"].as<boost::asio::ip::udp::endpoint>();
        auto faraway = vm["faraway"].as<boost::asio::ip::udp::endpoint>();
        auto obscure = vm["obscure"].as<uint64_t>();

        auto router = vm["purpose"].as<std::string>() == "import"
                    ? novemus::wormhole::create_importer(service, gateway, faraway, obscure)
                    : novemus::wormhole::create_exporter(service, gateway, faraway, obscure);

        router->employ();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
