/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#pragma once

#ifdef _MSC_VER
#define ROUTER_CLASS_EXPORT_DECLSPEC __declspec(dllexport)
#define ROUTER_CLASS_IMPORT_DECLSPEC
#endif // _MSC_VER

#ifdef __GNUC__
#define ROUTER_CLASS_EXPORT_DECLSPEC __attribute__ ((visibility("default")))
#define ROUTER_CLASS_IMPORT_DECLSPEC 
#endif

#ifdef WORMHOLE_EXPORTS
#define ROUTER_CLASS_DECLSPEC ROUTER_CLASS_EXPORT_DECLSPEC
#else
#define ROUTER_CLASS_DECLSPEC ROUTER_CLASS_IMPORT_DECLSPEC
#endif

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace wormhole {

typedef boost::asio::ip::udp::endpoint udp_endpoint;
typedef boost::asio::ip::tcp::endpoint tcp_endpoint;

struct ROUTER_CLASS_DECLSPEC router
{
    virtual ~router() {}
    virtual void launch() noexcept(true) = 0;
    virtual void cancel() noexcept(true) = 0;
};

typedef std::shared_ptr<router> router_ptr;

ROUTER_CLASS_DECLSPEC router_ptr create_exporter(boost::asio::io_context& io, const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(false);
ROUTER_CLASS_DECLSPEC router_ptr create_importer(boost::asio::io_context& io, const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(false);

}
