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
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace wormhole {

struct endpoint
{
    std::string host;
    uint16_t port;
};

enum protocol { udp, tcp };
enum strategy { depend, client, server };

LIBWORMHOLE_EXPORT std::istream& operator>>(std::istream& is, endpoint& ep);
LIBWORMHOLE_EXPORT std::ostream& operator<<(std::ostream& os, const endpoint& ep);

struct quality
{
    protocol proto = protocol::udp;
    strategy role = strategy::depend;
};

LIBWORMHOLE_EXPORT std::istream& operator>>(std::istream& is, quality& qos);
LIBWORMHOLE_EXPORT std::ostream& operator<<(std::ostream& os, const quality& qos);

struct LIBWORMHOLE_EXPORT router
{
    virtual ~router() {}
    virtual void launch() noexcept(true) = 0;
    virtual void cancel() noexcept(true) = 0;
};

LIBWORMHOLE_EXPORT std::shared_ptr<router> create_exporter(
        boost::asio::io_context& io, const endpoint& service, const endpoint& gateway, const endpoint& faraway, const quality& qos, uint64_t secret) noexcept(false);

LIBWORMHOLE_EXPORT std::shared_ptr<router> create_importer(
        boost::asio::io_context& io, const endpoint& service, const endpoint& gateway, const endpoint& faraway, const quality& qos, uint64_t secret) noexcept(false);
}
