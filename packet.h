#pragma once

#include "buffer.h"
#include <random>

namespace novemus { namespace tubus {

struct cursor : public mutable_buffer
{
    static constexpr uint16_t cursor_size = sizeof(uint64_t);
    
    explicit cursor(const mutable_buffer& buf) : mutable_buffer(buf)
    {
        truncate(cursor_size);
    }

    uint64_t value() const
    {
        return le64toh(get<uint64_t>(0));
    }

    void value(uint64_t number)
    {
        mutable_buffer::set<uint64_t>(0, htole64(number));
    }
};

struct snippet : public mutable_buffer
{
    static constexpr uint16_t handle_size = sizeof(uint64_t);

    explicit snippet(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    void set(uint64_t handle, const const_buffer& scrap)
    {
        mutable_buffer::set<uint64_t>(0, htole64(handle));
        mutable_buffer::fill(handle_size, scrap.size(), scrap.data());
        truncate(handle_size + scrap.size());
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

struct section : public mutable_buffer
{
    static constexpr uint16_t header_size = sizeof(uint16_t) * 2;

    enum kind
    {
        list_stub = 0,
        link_init, link_ackn,
        tear_init, tear_ackn,
        ping_shot, ping_ackn,
        move_data, move_ackn
    };

    section(const mutable_buffer& buf) : section(buf, buf)
    {
    }

    section(const mutable_buffer& par, const mutable_buffer& buf) : mutable_buffer(buf), m_parent(par)
    {
        if (type() != list_stub)
            truncate(header_size + length());
    }

    void set(uint16_t t)
    {
        mutable_buffer::set<uint16_t>(0, htons(t));
        mutable_buffer::set<uint16_t>(sizeof(uint16_t), 0);
        truncate(header_size);
    }

    void set(uint16_t t, const mutable_buffer& v)
    {
        mutable_buffer::set<uint16_t>(0, htons(t));
        mutable_buffer::set<uint16_t>(sizeof(uint16_t), htons(v.size()));
        fill(header_size, v.size(), v.data());
        truncate(header_size + v.size());
    }

    uint16_t type() const
    {
        return size() >= sizeof(uint16_t) ? ntohs(get<uint16_t>(0)) : 0;
    }

    uint16_t length() const
    {
        return size() >= header_size ? ntohs(get<uint16_t>(sizeof(uint16_t))) : 0;
    }

    mutable_buffer value() const
    {
        if (size() <= header_size)
            return slice(size(), 0);

        return slice(header_size, type() == list_stub ? size() - header_size : length());
    }

    void type(kind t)
    {
        mutable_buffer::set<uint16_t>(0, htons(t));
    }

    void length(uint16_t l)
    {
        mutable_buffer::set<uint16_t>(sizeof(uint16_t), htons(l));
        truncate(header_size + l);
    }

    void value(const const_buffer& v)
    {
        fill(header_size, v.size(), v.data());
        mutable_buffer::set<uint16_t>(sizeof(uint16_t), htons(v.size()));
        truncate(header_size + v.size());
    }

    section next() const
    {
        auto shift = data() - m_parent.data() + size();
        return section(m_parent, m_parent.slice(shift, m_parent.size() - shift));
    }

    section head() const
    {
        return section(m_parent, m_parent);
    }

    section tail() const
    {
        if (type() == list_stub)
            return *this;

        return next().tail();
    }

private:

    mutable_buffer m_parent;
};

struct packet : public mutable_buffer
{
    static constexpr uint16_t packet_sign = 0x0909;
    static constexpr uint16_t packet_version = 0x0100;
    static constexpr uint16_t header_size = 16;
    static constexpr uint16_t max_packet_size = 65507;
    static constexpr uint16_t max_payload_size = max_packet_size - header_size;

    explicit packet(const mutable_buffer& buf) : mutable_buffer(buf)
    {
    }

    uint64_t salt() const
    {
        return le64toh(get<uint64_t>(0));
    }

    uint16_t sign() const
    {
        return ntohs(get<uint16_t>(sizeof(uint64_t)));
    }

    uint16_t version() const
    {
        return ntohs(get<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t)));
    }

    uint32_t pin() const
    {
        return ntohl(get<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2));
    }

    bool valid() const
    {
        return size() >= packet::header_size + section::header_size 
            && sign() == packet::packet_sign && version() == packet::packet_version;
    }

    void salt(uint64_t s)
    {
        set<uint64_t>(0, htole64(s));
    }

    void sign(uint16_t s)
    {
        set<uint16_t>(sizeof(uint64_t), htons(s));
    }

    void version(uint16_t v)
    {
        set<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t), htons(v));
    }

    void pin(uint32_t p)
    {
        set<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2, htonl(p));
    }

    section payload() const
    {
        auto body = slice(packet::header_size, size() - packet::header_size);
        return section(body, body);
    }

    section useless() const
    {
        section head = payload();

        if (head.type() != section::list_stub)
            return head.tail();

        return head;
    }

    void trim()
    {
        section tail = useless();
        truncate(tail.data() - data());
    }

    void make_opened(uint64_t secret)
    {
        uint64_t s = salt() ^ secret;

        salt(s);
        invert(secret, s);
    }

    void make_opaque(uint64_t secret)
    {
        std::random_device dev;
        std::mt19937_64 gen(dev());
        uint64_t s = static_cast<uint64_t>(gen());

        salt(s ^ secret);
        invert(secret, s);
    }

private:

    inline uint64_t make_inverter(uint64_t secret, uint64_t salt)
    {
        uint64_t base = secret + salt;
        uint64_t shift = (base & 0x3F) | 0x01;
        return ((base >> shift) | (base << (64 - shift))) ^ salt;
    }

    void invert(uint64_t secret, uint64_t salt)
    {
        uint64_t inverter = make_inverter(secret, salt);

        uint8_t* ptr = data() + sizeof(uint64_t);
        uint8_t* end = data() + size();

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
    }
};

}}
