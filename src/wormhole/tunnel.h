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

#include <tubus/buffer.h>
#include <wormhole/wormhole.h>
#include <wormhole/export.h>
#include <boost/asio.hpp>
#include <memory>

namespace wormhole {

struct packet : public tubus::mutable_buffer
{
    static constexpr size_t header_size = sizeof(uint32_t) + sizeof(uint32_t);
    static constexpr size_t max_payload_size = 65536U - header_size;

    packet() : packet(0, true)
    {
    }

    packet(uint32_t id, bool allocate) 
        : tubus::mutable_buffer(create_buffer(header_size + (allocate ? max_payload_size : 0)))
    {
        set<uint32_t>(0, htonl(id));
        set<uint32_t>(sizeof(uint32_t), htonl(static_cast<uint32_t>(allocate ? max_payload_size : 0)));
    }

    uint32_t id() const
    {
        return ntohl(get<uint32_t>(0));
    }

    uint32_t capacity() const
    {
        return ntohl(get<uint32_t>(sizeof(uint32_t)));
    }

    void shrink(size_t capacity)
    {
        truncate(header_size + capacity);
        set<uint32_t>(sizeof(uint32_t), htonl(static_cast<uint32_t>(capacity)));
    }

    tubus::mutable_buffer header() const
    {
        return slice(0, header_size);
    }

    tubus::mutable_buffer payload() const
    {
        return slice(header_size, capacity());
    }

private:

    LIBWORMHOLE_EXPORT static tubus::mutable_buffer create_buffer(size_t size) noexcept(true);
};

typedef std::shared_ptr<packet> packet_ptr;

struct tunnel
{
    typedef std::function<void(const boost::system::error_code&)> callback;

    virtual ~tunnel() {}
    virtual void open(callback&& handler) noexcept(true) = 0;
    virtual void close(callback&& handler) noexcept(true) = 0;
    virtual void send(packet_ptr packet, callback&& handle) noexcept(true) = 0;
    virtual void receive(packet_ptr packet, callback&& handle) noexcept(true) = 0;
};

using udp_endpoint = boost::asio::ip::udp::endpoint;
using tcp_endpoint = boost::asio::ip::tcp::endpoint;

LIBWORMHOLE_EXPORT std::shared_ptr<tunnel> create_udp_tunnel(
    boost::asio::io_context& io, const udp_endpoint& gateway, const udp_endpoint& faraway, strategy qos, uint64_t secret) noexcept(true);

LIBWORMHOLE_EXPORT std::shared_ptr<tunnel> create_tcp_tunnel(
    boost::asio::io_context& io, const tcp_endpoint& gateway, const tcp_endpoint& faraway, strategy qos, uint64_t secret) noexcept(true);

}
