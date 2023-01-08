#pragma once

#include "buffer.h"
#include <random>

namespace novemus { namespace tubus {

struct handle
{
    static constexpr uint16_t size = sizeof(uint64_t) + sizeof(uint16_t);

    explicit handle(const mutable_buffer& buf)
        : offset(le64toh(buf.get<uint64_t>(0)))
        , length(ntohs(buf.get<uint16_t>(sizeof(uint64_t))))
    {
    }

    handle(uint64_t o, uint16_t l)
        : offset(o)
        , length(l)
    {
    }

    bool operator<(const handle& other) const
    {
        return offset < other.offset;
    }

    uint64_t offset;
    uint16_t length;
};

struct snippet
{
    static constexpr uint16_t header_size = sizeof(uint64_t);

    explicit snippet(const mutable_buffer& buf)
        : offset(le64toh(buf.get<uint64_t>(0)))
        , piece(buf.slice(sizeof(uint64_t), buf.size() - sizeof(uint64_t)))
    {
    }

    snippet(uint64_t o, const const_buffer& p)
        : offset(o)
        , piece(p)
    {
    }

    bool operator<(const snippet& other) const
    {
        return offset < other.offset;
    }

    uint64_t offset;
    const_buffer piece;
};

struct section : public mutable_buffer
{
    static constexpr uint16_t header_size = sizeof(uint16_t) * 2;

    enum kind
    {
        list_stub = 0,
        link_init, link_ackn,
        ping_shot, ping_ackn,
        data_move, data_ackn,
        tear_init, tear_ackn
    };

    section(const mutable_buffer& buf) : section(buf, buf)
    {
    }

    section(const mutable_buffer& par, const mutable_buffer& buf) : mutable_buffer(buf), m_parent(par)
    {
        if (type() > list_stub && type() <= tear_ackn)
            truncate(header_size + length());
    }

    void set(uint16_t t)
    {
        mutable_buffer::set<uint16_t>(0, htons(t));
        mutable_buffer::set<uint16_t>(sizeof(uint16_t), 0);
        truncate(header_size);
    }

    void set(const handle& h)
    {
        mutable_buffer::set<uint16_t>(0, htons(kind::data_ackn));
        mutable_buffer::set<uint16_t>(sizeof(uint16_t), htons(handle::size));
        mutable_buffer::set<uint64_t>(header_size, htole64(h.offset));
        mutable_buffer::set<uint16_t>(header_size + sizeof(uint64_t), htons(h.length));
        truncate(header_size + handle::size);
    }

    void set(const snippet& s)
    {
        mutable_buffer::set<uint16_t>(0, htons(kind::data_move));
        mutable_buffer::set<uint16_t>(sizeof(uint16_t), htons(s.piece.size() + snippet::header_size));
        mutable_buffer::set<uint64_t>(header_size, htole64(s.offset));
        fill(header_size + snippet::header_size, s.piece.size(), s.piece.data());
        truncate(header_size + snippet::header_size + s.piece.size());
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
    static constexpr uint16_t max_packet_size = 9992;
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
