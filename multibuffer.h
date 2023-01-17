#pragma once

#include "buffer.h"
#include <deque>
#include <random>
#include <numeric>

namespace novemus {

struct multibuffer
{
    typedef const_buffer value_type;
    typedef std::deque<const_buffer>::const_iterator const_iterator;
    typedef std::deque<const_buffer>::const_reverse_iterator const_reverse_iterator;

    multibuffer()
    {
    }

    multibuffer(const multibuffer& chain) : m_chain(chain.begin(), chain.end())
    {
    }

    multibuffer(const_iterator beg, const_iterator end) : m_chain(beg, end)
    {
    }

    void push_back(const const_buffer& buffer)
    {
        m_chain.push_back(buffer);
    }

    void push_front(const const_buffer& buffer)
    {
        m_chain.push_front(buffer);
    }

    void push_back(const multibuffer& chain)
    {
        std::copy(chain.begin(), chain.end(), std::back_inserter(m_chain));
    }

    void push_front(const multibuffer& chain)
    {
        std::copy(chain.rbegin(), chain.rend(), std::front_inserter(m_chain));
    }

    void pop_front(size_t count = 0)
    {
        m_chain.pop_front();
    }

    void pop_back(size_t count = 0)
    {
        m_chain.pop_back();
    }

    void count(size_t size)
    {
        m_chain.resize(size);
    }

    size_t count() const
    {
        return m_chain.size();
    }

    size_t size() const
    {
        return std::accumulate(m_chain.begin(), m_chain.end(), 0, [](const const_buffer& buffer)
        {
            return buffer.size();
        });
    }

    const const_buffer& at(size_t pos) const
    {
        return m_chain.at(pos);
    }

    const_iterator begin() const
    {
        return m_chain.begin();
    }

    const_iterator end() const
    {
        return m_chain.end();
    }

    const_reverse_iterator rbegin() const
    {
        return m_chain.rbegin();
    }

    const_reverse_iterator rend() const
    {
        return m_chain.rend();
    }

    mutable_buffer merge() const
    {
        mutable_buffer buffer(size());
        
        size_t offset = 0;
        std::for_each(m_chain.begin(), m_chain.end(), [&offset, &buffer](const const_buffer& item)
        {
            std::memcpy(buffer.data() + offset, item.data(), item.size());
            offset += item.size();
        });

        return buffer;
    }

private:

    std::deque<const_buffer> m_chain;
};

struct cursor : public multibuffer
{
    static constexpr uint16_t cursor_size = sizeof(uint64_t);
    
    explicit cursor(uint64_t number)
    {
        mutable_buffer buffer(cursor_size);
        buffer.set<uint64_t>(0, htole64(number));
        push_back(buffer);
    }

    explicit cursor(const mutable_buffer& buffer)
    {
        if (buffer.size() < cursor_size)
            throw std::runtime_error("cursor: bad buffer");

        push_back(buffer.slice(0, cursor_size));
    }

    uint64_t value() const
    {
        return le64toh(at(0).get<uint64_t>(0));
    }
};

struct snippet : public multibuffer
{
    static constexpr uint16_t header_size = sizeof(uint64_t);

    explicit snippet(const mutable_buffer& buffer)
    {
        if (buffer.size() < header_size)
            throw std::runtime_error("snippet: bad buffer");

        push_back(buffer.slice(0, header_size));
        push_back(buffer.slice(header_size, buffer.size() - header_size));
    }

    explicit snippet(const multibuffer& buffer) : multibuffer(buffer)
    {
        count(2);
    }

    snippet(const_iterator beg, const_iterator end) : multibuffer(beg, end)
    {
        count(2);
    }

    snippet(uint64_t handle, const const_buffer& fragment) : multibuffer(cursor(handle))
    {
        push_back(fragment);
    }

    uint64_t handle() const
    {
        return le64toh(at(0).get<uint64_t>(0));
    }

    const_buffer fragment() const
    {
        return at(1);
    }
};

struct section : public multibuffer
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

    explicit section(const mutable_buffer& buffer)
    {
        push_back(buffer.slice(0, header_size));
            
        if (type() != list_stub && buffer.size() < header_size + length())
            throw std::runtime_error("section: bad buffer");

        if (type() == move_data)
        {
            snippet data(buffer.slice(header_size, length()));
            std::copy(data.begin(), data.end(), std::back_inserter(*this));
        }
        else if (buffer.size() > header_size)
        {
            push_back(buffer.slice(header_size, length()));
        }
    }

    section(uint16_t type, const const_buffer& value)
    {
        mutable_buffer header(header_size);
        header.set<uint16_t>(0, htons(type));
        header.set<uint16_t>(sizeof(uint16_t), htons(value.size()));
        push_back(header);

        if (type == move_data)
        {
            snippet data(value);
            std::copy(data.begin(), data.end(), std::back_inserter(*this));
        }
        else if (value.size() > 0)
        {
            push_back(value);
        }
    }

    section(const_iterator beg, const_iterator end) : multibuffer(beg, end)
    {
        count(type() == list_stub ? 0 : type() == move_data ? 3 : 1);

        if (type() != list_stub && size() != header_size + length())
            throw std::runtime_error("section: bad buffer");
    }

    uint16_t type() const
    {
        return begin() != end() ? ntohs(at(0).get<uint16_t>(0)) : 0;
    }

    uint16_t length() const
    {
        return begin() != end() ? ntohs(at(0).get<uint16_t>(sizeof(uint16_t))) : 0;
    }

    multibuffer value() const
    {
        return multibuffer(begin() != end() ? begin() + 1 : end(), end());
    }
};

struct payload : public multibuffer
{
    payload(const_iterator beg, const_iterator end) : multibuffer(beg, end)
    {
    }

    section advance()
    {
        section sect(begin(), end());
        pop_front(sect.count());
        return sect;
    }
};

struct packet : public multibuffer
{
    static constexpr uint16_t packet_sign = 0x0909;
    static constexpr uint16_t packet_version = 0x0100;
    static constexpr uint16_t header_size = 16;
    static constexpr uint16_t max_packet_size = 65507;
    static constexpr uint16_t max_payload_size = max_packet_size - header_size;

    explicit packet(const mutable_buffer& buffer, uint64_t secret)
    {
        if (buffer.size() < header_size)
            return;

        if (secret != 0)
            invert(secret, le64toh(buffer.get<uint64_t>(0)) ^ secret, buffer);

        push_back(buffer.slice(0, header_size));
        
        auto payload = buffer.slice(header_size, buffer.size() - header_size);
        while (payload.size() >= section::header_size)
        {
            if (payload.get<uint16_t>(0) != section::list_stub)
                break;

            section sect(payload);
            std::copy(sect.begin(), sect.end(), std::back_inserter(*this));

            payload.crop(section::header_size + sect.length());
        }
    }

    explicit packet(uint32_t pin)
    {
        mutable_buffer header(header_size);
        header.set<uint16_t>(0, 0);
        header.set<uint16_t>(sizeof(uint64_t), htons(packet_sign));
        header.set<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t), htons(packet_version));
        header.set<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2, htonl(pin));
        push_back(header);
    }

    bool valid() const
    {
        return sign() == packet::packet_sign && version() == packet::packet_version;
    }

    uint64_t salt() const
    {
        return count() > 0 ? le64toh(at(0).get<uint64_t>(0)) : 0;
    }

    uint16_t sign() const
    {
        return count() > 0 ? ntohs(at(0).get<uint16_t>(sizeof(uint64_t))) : 0;
    }

    uint16_t version() const
    {
        return count() > 0 ? ntohs(at(0).get<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t))) : 0;
    }

    uint32_t pin() const
    {
        return count() > 0 ? ntohl(at(0).get<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2)) : 0;
    }

    payload data() const
    {
        return payload(count() > 0 ? begin() + 1 : end(), end());
    }

    void append(const section& sect)
    {
        push_back(sect);
    }

    mutable_buffer opaque(uint64_t secret)
    {
        std::random_device dev;
        std::mt19937_64 gen(dev());
        uint64_t s = static_cast<uint64_t>(gen());

        mutable_buffer result = merge();
        invert(secret, s ^ secret, result);
    }

public:

    static inline uint64_t make_inverter(uint64_t secret, uint64_t salt)
    {
        uint64_t base = secret + salt;
        uint64_t shift = (base & 0x3F) | 0x01;
        return ((base >> shift) | (base << (64 - shift))) ^ salt;
    }

    static void invert(uint64_t secret, uint64_t salt, const mutable_buffer& buffer)
    {
        uint64_t inverter = make_inverter(secret, salt);

        uint8_t* ptr = buffer.data();
        uint8_t* end = buffer.data() + buffer.size();

        *(uint64_t*)ptr = salt;
        ptr += sizeof(uint64_t);

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

}
