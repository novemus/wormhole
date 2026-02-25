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

template<class proto> 
boost::asio::ip::basic_endpoint<proto> endpoint(const std::string& url)
{
    boost::asio::io_context io;
    typename proto::resolver resolver(io);

    std::smatch match;
    if (std::regex_search(url, match, std::regex("^(\\w+://)?([^/]+):(\\d+)?$")))
        return boost::asio::ip::basic_endpoint<proto>(*resolver.resolve(match[2].str(), match[3].str()).begin());
    else if (std::regex_search(url, match, std::regex("^(\\w+)://([^/]+).*$")))
        return boost::asio::ip::basic_endpoint<proto>(*resolver.resolve(match[2].str(), match[1].str()).begin());

    return boost::asio::ip::basic_endpoint<proto>(*resolver.resolve(url, "0").begin());
}

int main(int argc, char *argv[])
{
    boost::program_options::options_description desc("wormhole options");
    desc.add_options()
        ("help", "produce help message")
        ("purpose", boost::program_options::value<std::string>()->required(), "wormhole purpose: <export|import>")
        ("service", boost::program_options::value<std::string>()->required(), "endpoint of the exported/imported service: <ip:port>")
        ("tunnel", boost::program_options::value<std::string>()->default_value("udp"), "tunnel base protocol: <udp|tcp>")
        ("gateway", boost::program_options::value<std::string>()->required(), "gateway endpoint of the transport tunnel: <ip:port>")
        ("faraway", boost::program_options::value<std::string>()->required(), "faraway endpoint of the transport tunnel: <ip:port>")
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

        auto tunnel = vm["tunnel"].as<std::string>();
        auto purpose = vm["purpose"].as<std::string>();
        auto service = vm["service"].as<std::string>();
        auto gateway = vm["gateway"].as<std::string>();
        auto faraway = vm["faraway"].as<std::string>();
        auto obscure = vm["obscure"].as<uint64_t>();

        _inf_ << "starting wormhole for purpose=" << purpose << " service=" << service << " tunnel=" << tunnel << " gateway=" << gateway << " faraway=" << faraway << " obscure=" << (obscure != 0);

        using tcp_proto = boost::asio::ip::tcp;
        using udp_proto = boost::asio::ip::udp;

        boost::asio::io_context io;
        if (tunnel == "tcp")
        {
            auto router = purpose == "import"
                ? wormhole::tcp_router::create_importer(io, endpoint<tcp_proto>(service), endpoint<tcp_proto>(gateway), endpoint<tcp_proto>(faraway), obscure)
                : wormhole::tcp_router::create_exporter(io, endpoint<tcp_proto>(service), endpoint<tcp_proto>(gateway), endpoint<tcp_proto>(faraway), obscure);
            router->launch();
            io.run();
        }
        else
        {
            auto router = purpose == "import"
                ? wormhole::udp_router::create_importer(io, endpoint<tcp_proto>(service), endpoint<udp_proto>(gateway), endpoint<udp_proto>(faraway), obscure)
                : wormhole::udp_router::create_exporter(io, endpoint<tcp_proto>(service), endpoint<udp_proto>(gateway), endpoint<udp_proto>(faraway), obscure);
            router->launch();
            io.run();
        }
    }
    catch (const std::exception& e)
    {
        _ftl_ << e.what();
        return 1;
    }

    return 0;
}
