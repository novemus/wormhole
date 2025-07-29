/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <wormhole/wormhole.h>
#include <wormhole/logger.h>
#include <regex>
#include <iostream>
#include <boost/program_options.hpp>

namespace boost { namespace asio { namespace ip {

template<class proto>
void validate(boost::any& result, const std::vector<std::string>& values, basic_endpoint<proto>*, int)
{
    boost::program_options::validators::check_first_occurrence(result);
    const std::string& url = boost::program_options::validators::get_single_string(values);

    try
    {
        boost::asio::io_context io;
        typename proto::resolver resolver(io);

        std::smatch match;
        if (std::regex_search(url, match, std::regex("^(\\w+://)?([^/]+):(\\d+)?$")))
            result = boost::any(basic_endpoint<proto>(*resolver.resolve(match[2].str(), match[3].str()).begin()));
        else if (std::regex_search(url, match, std::regex("^(\\w+)://([^/]+).*$")))
            result = boost::any(basic_endpoint<proto>(*resolver.resolve(match[2].str(), match[1].str()).begin()));
        else
            result = boost::any(basic_endpoint<proto>(*resolver.resolve(url, "0").begin()));
    }
    catch(const boost::system::system_error&)
    {
         boost::throw_exception(boost::program_options::error("can't resolve " + url));
    }
}

}}}

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
        ("log-level", boost::program_options::value<wormhole::log::severity>()->default_value(wormhole::log::info), "log level: <fatal|error|warning|info|debug|trace>");

    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            return 0;
        }

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
        wormhole::log::set(vm["log-level"].as<wormhole::log::severity>(), vm["log-file"].as<std::string>());

        auto purpose = vm["purpose"].as<std::string>();
        auto service = vm["service"].as<boost::asio::ip::tcp::endpoint>();
        auto gateway = vm["gateway"].as<boost::asio::ip::udp::endpoint>();
        auto faraway = vm["faraway"].as<boost::asio::ip::udp::endpoint>();
        auto obscure = vm["obscure"].as<uint64_t>();

        _inf_ << "starting wormhole for purpose=" << purpose << " service=" << service << " gateway=" << gateway << " faraway=" << faraway << " obscure=" << (obscure != 0);

        boost::asio::io_context io;
        auto router = purpose == "import"
                    ? wormhole::create_importer(io, service, gateway, faraway, obscure)
                    : wormhole::create_exporter(io, service, gateway, faraway, obscure);

        router->launch();
        io.run();
    }
    catch (const std::exception& e)
    {
        _ftl_ << e.what();
        return 1;
    }

    return 0;
}
