#pragma once

#include "buffer.h"
#include <deque>
#include <random>
#include <numeric>

namespace novemus::tubus {

struct section : public mutable_buffer
{
    static constexpr size_t header_size = sizeof(uint16_t) * 2;

    typedef const_buffer value_type;
    typedef const const_buffer* const_iterator;

    enum flag
    {
        echo = 1,
        link = 2,
        tear = 4,
        ping = 6,
        move = 8
    };

    explicit section()
    {
    }

    explicit section(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    void stub()
    {
        std::memset(data(), 0, std::min(header_size, size()));
    }

    void simple(uint16_t type)
    {
        set<uint16_t>(0, htons(type));
        set<uint16_t>(sizeof(uint16_t), 0);
    }

    void cursor(uint64_t handle)
    {
        set<uint16_t>(0, htons(flag::move | flag::echo));
        set<uint16_t>(sizeof(uint16_t), htons(sizeof(handle)));
        set<uint64_t>(header_size, htole64(handle));
    }

    void snippet(uint64_t handle, const const_buffer& data)
    {
        set<uint16_t>(0, htons(flag::move));
        set<uint16_t>(sizeof(uint16_t), htons(sizeof(handle) + data.size()));
        set<uint64_t>(header_size, htole64(handle));
        fill(header_size + sizeof(handle), data.size(), data.data());
    }

    uint16_t type() const
    {
        return size() >= header_size ? ntohs(get<uint16_t>(0)) : 0;
    }

    uint16_t length() const
    {
        return size() >= header_size ? ntohs(get<uint16_t>(sizeof(uint16_t))) : 0;
    }

    mutable_buffer value() const
    {
        return slice(std::min(header_size, size()), length());
    }

    void advance()
    {
        crop(std::min(header_size, size()) + length());
    }
};

struct cursor : public mutable_buffer
{
    static constexpr size_t handle_size = sizeof(uint64_t);
    
    explicit cursor(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    uint64_t handle() const
    {
        return le64toh(get<uint64_t>(0));
    }
};

struct snippet : public mutable_buffer
{
    static constexpr uint16_t handle_size = sizeof(uint64_t);

    explicit snippet(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    uint64_t handle() const
    {
        return le64toh(get<uint64_t>(0));
    }

    mutable_buffer fragment() const
    {
        return slice(handle_size, size() - handle_size);
    }
};

struct packet : public mutable_buffer
{
    static constexpr size_t packet_sign = 0x0909;
    static constexpr size_t packet_version = 0x0100;
    static constexpr size_t header_size = 16;
    static constexpr size_t max_packet_size = 65507;
    static constexpr size_t max_payload_size = max_packet_size - header_size;

    packet(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    uint64_t salt() const
    {
        return size() > packet::header_size ? le64toh(get<uint64_t>(0)) : 0;
    }

    uint16_t sign() const
    {
        return size() > packet::header_size ? ntohs(get<uint16_t>(sizeof(uint64_t))) : 0;
    }

    uint16_t version() const
    {
        return size() > packet::header_size ? ntohs(get<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t))) : 0;
    }

    uint32_t pin() const
    {
        return size() > packet::header_size ? ntohl(get<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2)) : 0;
    }

    section body() const
    {
        return section(slice(std::min(size(), header_size), size() - std::min(size(), header_size)));
    }

    section stub() const
    {
        section sect(slice(std::min(size(), header_size), size() - std::min(size(), header_size)));
        while (sect.type() != 0)
        {
            sect.crop(section::header_size + sect.length());
        }
        return sect;
    }

    void trim()
    {
        truncate((uint8_t*)stub().data() - (uint8_t*)data());
    }
};

struct dimmer
{
    static mutable_buffer invert(uint64_t secret, const mutable_buffer& buffer)
    {
        uint8_t* ptr = (uint8_t*)buffer.data();
        uint8_t* end = ptr + buffer.size();

        uint64_t salt = le64toh(*(uint64_t*)ptr);
        if (salt == 0)
        {
            std::random_device dev;
            std::mt19937_64 gen(dev());
            salt = static_cast<uint64_t>(gen());
            *(uint64_t*)ptr = salt ^ secret;
        }
        else
        {
            salt ^= secret;
            *(uint64_t*)ptr = 0;
        }

        ptr += sizeof(uint64_t);

        uint64_t inverter = make_inverter(secret, salt);
        while (ptr + sizeof(uint64_t) <= end)
        {
            *(uint64_t*)ptr ^= inverter;
            inverter = make_inverter(inverter, salt);
            ptr += sizeof(uint64_t);
        }

        uint8_t* inv = (uint8_t*)&inverter;
        while (ptr < end)
        {
            *ptr ^= *inv;
            ++ptr;
            ++inv;
        }

        return buffer;
    }

private:

    static inline uint64_t make_inverter(uint64_t secret, uint64_t salt)
    {
        uint64_t base = secret + salt;
        uint64_t shift = (base & 0x3F) | 0x01;
        return ((base >> shift) | (base << (64 - shift))) ^ salt;
    }
};

}
