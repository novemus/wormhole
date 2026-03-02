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
#include <boost/program_options.hpp>

constexpr const char* envs = R"(environment options:
  WORMHOLE_SECRET=<number>        pre-shared 64-bit key to obscure the tcp/udp tunnel
  WORMHOLE_CERT=<path>            host certificate for ssl verification
  WORMHOLE_KEY=<path>             host private key for ssl verification
  WORMHOLE_CA=<path>              peer certificate for ssl verification)";

int main(int argc, char *argv[])
{
    boost::program_options::options_description base("command line options", 120, 60);
    base.add_options()
        ("help,h", "produce help message")
        ("purpose,p", boost::program_options::value<std::string>()->required(), "wormhole purpose: export|import")
        ("service,s", boost::program_options::value<wormhole::endpoint>()->required(), "endpoint of the exported/imported tcp service: <ip:port>")
        ("gateway,g", boost::program_options::value<wormhole::endpoint>()->required(), "gateway endpoint of the tunnel: <ip:port>")
        ("faraway,f", boost::program_options::value<wormhole::endpoint>()->required(), "faraway endpoint of the tunnel: <ip:port>")
        ("quality,q", boost::program_options::value<wormhole::quality>()->default_value(wormhole::quality()), "quality of the tunnel: udp:client|udp:server|tcp:client|tcp:server|ssl:client|ssl:server")
        ("journal,j", boost::program_options::value<std::string>()->default_value(""), "journal file: <path>")
        ("logging,l", boost::program_options::value<wormhole::log::severity>()->default_value(wormhole::log::info), "logging level: fatal|error|warning|info|debug|trace");

    boost::program_options::options_description more("security options");
    more.add_options()
        ("secret", boost::program_options::value<uint64_t>()->default_value(0))
        ("cert", boost::program_options::value<std::string>()->default_value(""))
        ("key", boost::program_options::value<std::string>()->default_value(""))
        ("ca", boost::program_options::value<std::string>()->default_value(""));

    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, base), vm);
        if (vm.count("help"))
        {
            std::cout << base << std::endl << envs << std::endl;
            return 0;
        }

        auto mapper = [](const std::string& env)
        {
            return env == "WORMHOLE_SECRET" ? "secret" : 
                   env == "WORMHOLE_CERT" ? "cert" : 
                   env == "WORMHOLE_KEY" ? "key" : 
                   env == "WORMHOLE_CA" ? "ca" : "";
        };

        boost::program_options::store(boost::program_options::parse_environment(more, mapper), vm);
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

        wormhole::security guard = { 
            vm["secret"].as<uint64_t>(),
            wormhole::security::crypto {
                vm["cert"].as<std::string>(),
                vm["key"].as<std::string>(),
                vm["ca"].as<std::string>()
            }
        };

        _inf_ << "starting wormhole for purpose=" << purpose << " service=" << service << " gateway=" << gateway << " faraway=" << faraway << " quality=" << quality;

        boost::asio::io_context io;

        auto router = purpose == "import"
            ? wormhole::create_importer(io, service, gateway, faraway, quality, guard)
            : wormhole::create_exporter(io, service, gateway, faraway, quality, guard);
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
