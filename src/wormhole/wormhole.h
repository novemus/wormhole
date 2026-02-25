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

template<class proto> struct LIBWORMHOLE_EXPORT router
{
    typedef boost::asio::ip::basic_endpoint<proto> tunnel_endpoint;
    typedef boost::asio::ip::tcp::endpoint         server_endpoint;

    virtual ~router() {}
    virtual void launch() noexcept(true) = 0;
    virtual void cancel() noexcept(true) = 0;

    static std::shared_ptr<router<proto>> create_exporter(
        boost::asio::io_context& io, const server_endpoint& server, const tunnel_endpoint& gateway, const tunnel_endpoint& faraway, uint64_t secret) noexcept(false);

    static std::shared_ptr<router<proto>> create_importer(
        boost::asio::io_context& io, const server_endpoint& server, const tunnel_endpoint& gateway, const tunnel_endpoint& faraway, uint64_t secret) noexcept(false);
};

typedef router<boost::asio::ip::udp> udp_router;
typedef router<boost::asio::ip::tcp> tcp_router;

template <>
std::shared_ptr<udp_router> udp_router::create_exporter(
    boost::asio::io_context& io, const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(false);

template <>
std::shared_ptr<udp_router> udp_router::create_importer(
    boost::asio::io_context& io, const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(false);

template <>
std::shared_ptr<tcp_router> tcp_router::create_exporter(
    boost::asio::io_context& io, const tcp_endpoint& server, const tcp_endpoint& gateway, const tcp_endpoint& faraway, uint64_t secret) noexcept(false);

template <>
std::shared_ptr<tcp_router> tcp_router::create_importer(
    boost::asio::io_context& io, const tcp_endpoint& server, const tcp_endpoint& gateway, const tcp_endpoint& faraway, uint64_t secret) noexcept(false);
}
