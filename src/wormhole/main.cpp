/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <iomanip>
#include <wormhole/wormhole.h>
#include <wormhole/logger.h>
#include <boost/program_options.hpp>

int main(int argc, char *argv[])
{
    boost::program_options::options_description opts("command line options", 120, 60);
    opts.add_options()
        ("help,h", "produce help message")
        ("purpose,p", boost::program_options::value<std::string>()->required(), "wormhole purpose: export|import")
        ("service,s", boost::program_options::value<wormhole::endpoint>()->required(), "endpoint of the exported/imported tcp service: <ip:port>")
        ("gateway,g", boost::program_options::value<wormhole::endpoint>()->required(), "gateway endpoint of the tunnel: <ip:port>")
        ("faraway,f", boost::program_options::value<wormhole::endpoint>()->required(), "faraway endpoint of the tunnel: <ip:port>")
        ("quality,q", boost::program_options::value<wormhole::quality>()->default_value(wormhole::quality()), "quality of the tunnel: udp:client|udp:server|tcp:client|tcp:server")
        ("journal,j", boost::program_options::value<std::string>()->default_value(""), "journal file: <path>")
        ("logging,l", boost::program_options::value<wormhole::log::severity>()->default_value(wormhole::log::info), "logging level: fatal|error|warning|info|debug|trace");

    boost::program_options::options_description envs("environment options");
    envs.add_options()
        ("obscure", boost::program_options::value<uint64_t>()->default_value(0));

    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, opts), vm);
        if (vm.count("help"))
        {
            std::cout << opts << std::endl
                      << "environment options:" << std::endl
                      << std::setw(35) << "WORMHOLE_SECRET=<64-bit number>" 
                      << std::setw(55) << "pre-shared key to obscure the tunnel (optional)" << std::endl;
            return 0;
        }

        auto mapper = [](const std::string& env)
        {
            return env == "WORMHOLE_SECRET" ? "obscure" : "";
        };

        boost::program_options::store(boost::program_options::parse_environment(envs, mapper), vm);
        boost::program_options::notify(vm);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    try
    {
        wormhole::log::set(vm["logging"].as<wormhole::log::severity>(), vm["journal"].as<std::string>());

        auto purpose = vm["purpose"].as<std::string>();
        auto service = vm["service"].as<wormhole::endpoint>();
        auto gateway = vm["gateway"].as<wormhole::endpoint>();
        auto faraway = vm["faraway"].as<wormhole::endpoint>();
        auto quality = vm["quality"].as<wormhole::quality>();
        auto obscure = vm["obscure"].as<uint64_t>();

        _inf_ << "starting wormhole for purpose=" << purpose << " service=" << service<< " gateway=" << gateway << " faraway=" << faraway << " quality=" << quality << "obscure=" << (obscure != 0);

        boost::asio::io_context io;

        auto router = purpose == "import"
            ? wormhole::create_importer(io, service, gateway, faraway, quality, obscure)
            : wormhole::create_exporter(io, service, gateway, faraway, quality, obscure);
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
