#pragma once

#include "buffer.h"
#include <random>

namespace novemus { namespace tubus {
    
struct snippet
{
    struct info
    {
        info(const mutable_buffer& buf) : m_buffer(buf)
        {
            if (m_buffer.size() > header_size)
                m_buffer.truncate(header_size);
        }

        void set(uint64_t pos, uint16_t len)
        {
            m_buffer.set<uint64_t>(0, htole64(pos));
            m_buffer.set<uint16_t>(sizeof(uint64_t), htons(len));
            m_buffer.truncate(header_size);
        }

        uint64_t pos() const
        {
            return le64toh(m_buffer.get<uint64_t>(0));
        }

        uint16_t length() const
        {
            return m_buffer.size() >= header_size ? ntohs(m_buffer.get<uint16_t>(sizeof(uint64_t))) : 0;
        }

        uint16_t size() const
        {
            return (uint16_t)m_buffer.size();
        }

        const_buffer buffer() const
        {
            return m_buffer;
        }

    private:

        mutable_buffer m_buffer;
    };

    static constexpr uint16_t header_size = sizeof(uint64_t) + sizeof(uint16_t);

    snippet(const mutable_buffer& buf) : m_buffer(buf)
    {
    }

    void set(uint64_t pos, const const_buffer& buf)
    {
        m_buffer.set<uint64_t>(0, htole64(pos));
        m_buffer.set<uint16_t>(sizeof(uint64_t), htons(buf.size()));
        m_buffer.fill(header_size, buf.size(), buf.data());
        m_buffer.truncate(header_size + buf.size());
    }

    uint64_t pos() const
    {
        return le64toh(m_buffer.get<uint64_t>(0));
    }

    uint16_t length() const
    {
        return m_buffer.size() >= header_size ? ntohs(m_buffer.get<uint16_t>(sizeof(uint64_t))) : 0;
    }

    info header() const
    {
        return info(m_buffer.slice(0, m_buffer.size() >= header_size ? header_size : 0));
    }

    mutable_buffer data() const
    {
        return m_buffer.slice(header_size, m_buffer.size() - header_size);
    }

    uint16_t size() const
    {
        return (uint16_t)m_buffer.size();
    }

    const_buffer buffer() const
    {
        return m_buffer;
    }

private:

    mutable_buffer m_buffer;
};

struct packet
{
    struct section
    {
        static constexpr uint16_t header_size = sizeof(uint16_t) * 2;

        enum kind
        {
            list_end,
            link_req,
            link_ack,
            ping_req,
            ping_ack,
            push_req,
            push_ack,
            tear_req,
            tear_ack
        };

        section(const mutable_buffer& buf) : section(buf, buf)
        {
        }

        section(const mutable_buffer& par, const mutable_buffer& buf) : m_parent(par), m_buffer(buf)
        {
            if (type() != list_end)
                m_buffer.truncate(header_size + length());
        }

        void set(uint16_t t)
        {
            m_buffer.set<uint16_t>(0, htons(t));
            m_buffer.set<uint16_t>(sizeof(uint16_t), 0);
            m_buffer.truncate(header_size);
        }

        void set(uint16_t t, const const_buffer& v)
        {
            m_buffer.set<uint16_t>(0, htons(t));
            m_buffer.set<uint16_t>(sizeof(uint16_t), htons(v.size()));
            m_buffer.fill(header_size, v.size(), v.data());
            m_buffer.truncate(header_size + v.size());
        }

        uint16_t type() const
        {
            return m_buffer.size() >= sizeof(uint16_t) ? ntohs(m_buffer.get<uint16_t>(0)) : 0;
        }

        uint16_t length() const
        {
            return m_buffer.size() >= header_size ? ntohs(m_buffer.get<uint16_t>(sizeof(uint16_t))) : 0;
        }

        mutable_buffer value() const
        {
            if (m_buffer.size() <= header_size)
                return m_buffer.slice(m_buffer.size(), 0);

            return m_buffer.slice(header_size, type() == list_end ? m_buffer.size() - header_size : length());
        }

        void type(uint16_t t)
        {
            m_buffer.set<uint16_t>(0, htons(t));
        }

        void length(uint16_t l)
        {
            m_buffer.set<uint16_t>(sizeof(uint16_t), htons(l));
            m_buffer.truncate(header_size + l);
        }

        void value(const const_buffer& v)
        {
            m_buffer.fill(header_size, v.size(), v.data());
            m_buffer.truncate(header_size + v.size());
        }

        uint16_t size() const
        {
            return m_buffer.size();
        }

        section next() const
        {
            auto shift = m_buffer.data() - m_parent.data() + m_buffer.size();
            return section(m_parent, m_parent.slice(shift, m_parent.size() - shift));
        }

        section head() const
        {
            return section(m_parent);
        }

        section tail() const
        {
            if (type() == list_end)
                return *this;

            return next().tail();
        }

        const_buffer buffer() const
        {
            return m_buffer;
        }

    private:

        mutable_buffer m_parent;
        mutable_buffer m_buffer;
    };

    static constexpr uint16_t packet_sign = 0x0909;
    static constexpr uint16_t packet_version = 0x0100;
    static constexpr uint16_t header_size = 16;
    static constexpr uint16_t max_packet_size = 9992;
    static constexpr uint16_t max_payload_size = max_packet_size - header_size;

    packet(const mutable_buffer& buf) : m_buffer(buf)
    {
    }

    uint64_t salt() const
    {
        return le64toh(m_buffer.get<uint64_t>(0));
    }

    uint16_t sign() const
    {
        return ntohs(m_buffer.get<uint16_t>(sizeof(uint64_t)));
    }

    uint16_t version() const
    {
        return ntohs(m_buffer.get<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t)));
    }

    uint32_t pin() const
    {
        return ntohl(m_buffer.get<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2));
    }

    bool valid() const
    {
        return m_buffer.size() >= packet::header_size + packet::section::header_size 
            && sign() == packet::packet_sign && version() == packet::packet_version;
    }

    void salt(uint64_t s)
    {
        m_buffer.set<uint64_t>(0, htole64(s));
    }

    void sign(uint16_t s)
    {
        m_buffer.set<uint16_t>(sizeof(uint64_t), htons(s));
    }

    void version(uint16_t v)
    {
        m_buffer.set<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t), htons(v));
    }

    void pin(uint32_t p)
    {
        m_buffer.set<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2, htonl(p));
    }

    size_t size() const
    {
        return m_buffer.size();
    }

    section payload() const
    {
        return section(m_buffer.slice(packet::header_size, m_buffer.size() - packet::header_size));
    }

    section useless() const
    {
        section head = payload();

        if (head.type() != section::list_end)
            return head.tail();

        return head;
    }

    const_buffer buffer() const
    {
        return m_buffer;
    }

    void trim()
    {
        section tail = useless();
        m_buffer.truncate(tail.buffer().data() - m_buffer.data());
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

        uint8_t* ptr = m_buffer.data() + sizeof(uint64_t);
        uint8_t* end = m_buffer.data() + m_buffer.size();

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

    mutable_buffer m_buffer;
};

}}
