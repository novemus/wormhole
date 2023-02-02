#include "wormhole.h"
#include <regex>
#include <iostream>
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>

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

int main(int argc, char *argv[])
{
    boost::program_options::options_description desc("wormhole options");
    desc.add_options()
        ("help", "produce help message")
        ("purpose", boost::program_options::value<std::string>()->required(), "wormhole purpose: <export|import>")
        ("service", boost::program_options::value<boost::asio::ip::tcp::endpoint>()->required(), "endpoint of the exported/imported service: <ip:port>")
        ("gateway", boost::program_options::value<boost::asio::ip::udp::endpoint>()->required(), "gateway endpoint of the transport tunnel: <ip:port>")
        ("faraway", boost::program_options::value<boost::asio::ip::udp::endpoint>()->required(), "faraway endpoint of the transport tunnel: <ip:port>")
        ("obscure", boost::program_options::value<uint64_t>()->default_value(0), "pre-shared key to obscure the transport tunnel: <number>");

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
