#include "wormhole.h"
#include <regex>
#include <iostream>
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>

template<class proto> 
boost::asio::ip::basic_endpoint<proto> parse_address(const std::string& str)
{
    std::smatch match;
    if (std::regex_search(str, match, std::regex("(\\w+://)?(.+):(.*)")))
    {
        return boost::asio::ip::basic_endpoint<proto>(boost::asio::ip::make_address(match[2].str()), boost::lexical_cast<uint16_t>(match[3].str()));
    }
    throw std::runtime_error("wrong endpoint format");
}

int main(int argc, char *argv[])
{
    boost::program_options::options_description desc("wormhole options");
    desc.add_options()
        ("help", "produce help message")
        ("purpose", boost::program_options::value<std::string>()->required(), "wormhole purpose: <export|import>")
        ("service", boost::program_options::value<std::string>()->required(), "endpoint of the exported/imported service: <ip:port>")
        ("gateway", boost::program_options::value<std::string>()->required(), "gateway endpoint of the transport tunnel: <ip:port>")
        ("faraway", boost::program_options::value<std::string>()->required(), "faraway endpoint of the transport tunnel: <ip:port>")
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
        auto service = parse_address<boost::asio::ip::tcp>(vm["service"].as<std::string>());
        auto gateway = parse_address<boost::asio::ip::udp>(vm["gateway"].as<std::string>());
        auto faraway = parse_address<boost::asio::ip::udp>(vm["faraway"].as<std::string>());

        auto router = vm["purpose"].as<std::string>() == "import"
                    ? novemus::wormhole::create_importer(service, gateway, faraway, vm["obscure"].as<uint64_t>())
                    : novemus::wormhole::create_exporter(service, gateway, faraway, vm["obscure"].as<uint64_t>());

        router->employ();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
