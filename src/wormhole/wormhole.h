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

#include <iostream>
#include <wormhole/export.h>
#include <boost/asio.hpp>

namespace wormhole {

using udp_endpoint = boost::asio::ip::udp::endpoint;
using tcp_endpoint = boost::asio::ip::tcp::endpoint;

struct endpoint
{
    boost::asio::ip::address address;
    uint16_t port = 0;

    operator udp_endpoint() const noexcept(true)
    {
        return udp_endpoint(address, port);
    }

    operator tcp_endpoint() const noexcept(true)
    {
        return tcp_endpoint(address, port);
    }

    LIBWORMHOLE_EXPORT static std::string to_string(const endpoint& ep) noexcept(false);
    LIBWORMHOLE_EXPORT static endpoint from_string(const std::string& ep) noexcept(false);
};

enum protocol { any, udp, tcp, ssl };
enum relation { either, client, server };

LIBWORMHOLE_EXPORT std::istream& operator>>(std::istream& is, endpoint& ep) noexcept(false);
LIBWORMHOLE_EXPORT std::ostream& operator<<(std::ostream& os, const endpoint& ep) noexcept(false);
LIBWORMHOLE_EXPORT bool operator==(const endpoint& lhs, const endpoint& rhs) noexcept(true);

struct quality
{
    protocol proto = protocol::udp;
    relation role = relation::either;

    LIBWORMHOLE_EXPORT static std::string to_string(const quality& qos) noexcept(false);
    LIBWORMHOLE_EXPORT static quality from_string(const std::string& str) noexcept(false);
};

LIBWORMHOLE_EXPORT std::istream& operator>>(std::istream& is, quality& qos) noexcept(false);
LIBWORMHOLE_EXPORT std::ostream& operator<<(std::ostream& os, const quality& qos) noexcept(false);
LIBWORMHOLE_EXPORT bool operator==(const quality& lhs, const quality& rhs) noexcept(true);

struct security
{
    uint64_t secret = 0;
    struct crypto
    {
        std::string cert;
        std::string key;
        std::string ca;
    } identity;
};

struct router
{
    virtual ~router() noexcept(true) {}
    virtual void launch() noexcept(true) = 0;
    virtual void cancel() noexcept(true) = 0;
};

LIBWORMHOLE_EXPORT std::shared_ptr<router> create_exporter(
        boost::asio::io_context& io, const endpoint& service, const endpoint& gateway, const endpoint& faraway, const quality& qos, const security& guard) noexcept(false);

LIBWORMHOLE_EXPORT std::shared_ptr<router> create_importer(
        boost::asio::io_context& io, const endpoint& service, const endpoint& gateway, const endpoint& faraway, const quality& qos, const security& guard) noexcept(false);
}
