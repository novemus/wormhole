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

#include <wormhole/export.h>
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace wormhole {

typedef boost::asio::ip::udp::endpoint udp_endpoint;
typedef boost::asio::ip::tcp::endpoint tcp_endpoint;

struct LIBWORMHOLE_EXPORT router
{
    virtual ~router() {}
    virtual void launch() noexcept(true) = 0;
    virtual void cancel() noexcept(true) = 0;
};

typedef std::shared_ptr<router> router_ptr;

LIBWORMHOLE_EXPORT router_ptr create_exporter(boost::asio::io_context& io, const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(false);
LIBWORMHOLE_EXPORT router_ptr create_importer(boost::asio::io_context& io, const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(false);

}
