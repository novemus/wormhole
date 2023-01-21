#pragma once

#include "buffer.h"
#include <deque>
#include <random>
#include <numeric>

namespace novemus {

template<class buffer_type> struct multibuffer
{
    typedef buffer_type value_type;
    typedef typename std::deque<value_type>::const_iterator const_iterator;

    multibuffer()
    {
    }

    multibuffer(const value_type& buffer) : m_chain(1, buffer)
    {
    }

    multibuffer(const multibuffer& chain) : m_chain(chain.begin(), chain.end())
    {
    }

    multibuffer(const const_iterator& beg, const const_iterator& end) : m_chain(beg, end)
    {
    }

    inline void push_back(const value_type& buffer)
    {
        m_chain.push_back(buffer);
    }

    inline void push_front(const value_type& buffer)
    {
        m_chain.push_front(buffer);
    }

    inline void push_back(const multibuffer& chain)
    {
        std::copy(chain.begin(), chain.end(), std::back_inserter(m_chain));
    }

    inline void push_front(const multibuffer& chain)
    {
        std::copy(chain.m_chain.rbegin(), chain.m_chain.rend(), std::front_inserter(m_chain));
    }

    inline void pop_front(size_t count = 1)
    {
        m_chain.erase(m_chain.begin(), m_chain.begin() + count);
    }

    inline void pop_back(size_t count = 1)
    {
        m_chain.erase(m_chain.begin() + (m_chain.size() - count), m_chain.end());
    }

    inline multibuffer slice(size_t pos, size_t count) const
    {
        return multibuffer(m_chain.begin() + pos, m_chain.begin() + pos + count);
    }

    inline void count(size_t size)
    {
        m_chain.resize(size);
    }

    inline size_t count() const
    {
        return m_chain.size();
    }

    inline size_t size() const
    {
        return std::accumulate(m_chain.begin(), m_chain.end(), 0, [](size_t sum, const value_type& buffer)
        {
            return sum + buffer.size();
        });
    }

    inline const value_type& at(size_t pos) const
    {
        return m_chain.at(pos);
    }

    inline const_iterator begin() const
    {
        return m_chain.begin();
    }

    inline const_iterator end() const
    {
        return m_chain.end();
    }

    mutable_buffer unite() const
    {
        mutable_buffer buffer = mutable_buffer::create(size());
        
        size_t offset = 0;
        std::for_each(m_chain.begin(), m_chain.end(), [&offset, &buffer](const value_type& item)
        {
            std::memcpy(buffer.data() + offset, item.data(), item.size());
            offset += item.size();
        });

        return buffer;
    }

private:

    std::deque<value_type> m_chain;
};

struct cursor : public multibuffer<const_buffer>
{
    static constexpr size_t cursor_size = sizeof(uint64_t);
    
    cursor()
    {
    }

    cursor(const multibuffer& buffer) : multibuffer(buffer)
    {
    }

    cursor(const const_buffer& buffer) : multibuffer(buffer.slice(0, std::min(cursor::cursor_size, buffer.size())))
    {
    }

    cursor(uint64_t number) : multibuffer(const_buffer::create(htole64(number)))
    {
    }

    inline uint64_t value() const
    {
        return le64toh(at(0).get<uint64_t>(0));
    }

    inline bool valid() const
    {
        return count() == 1 && at(0).size() == cursor_size;
    }
};

struct snippet : public multibuffer<const_buffer>
{
    static constexpr size_t header_size = sizeof(uint64_t);

    snippet()
    {
    }

    snippet(const const_buffer& buffer)
    {
        if (buffer.size() > snippet::header_size)
        {
            push_back(buffer.slice(0, snippet::header_size));
            push_back(buffer.slice(snippet::header_size, buffer.size() - snippet::header_size));
        }
    }

    snippet(const multibuffer& buffer) : multibuffer(buffer)
    {
    }

    snippet(uint64_t handle, const const_buffer& fragment)
    {
        push_back(const_buffer::create(htole64(handle)));
        push_back(fragment);
    }

    inline uint64_t handle() const
    {
        return le64toh(at(0).get<uint64_t>(0));
    }

    inline const_buffer fragment() const
    {
        return at(1);
    }

    inline bool valid() const
    {
        return count() == 2 && at(0).size() == header_size;
    }
};

struct section : public multibuffer<const_buffer>
{
    static constexpr size_t header_size = sizeof(uint16_t) * 2;

    enum flag
    {
        echo = 0x1,
        link = 0x2,
        tear = 0x4,
        ping = 0x6,
        data = 0x8
    };

    section(const const_buffer& buffer)
    {
        if(buffer.size() < section::header_size)
            return;

        push_back(buffer.slice(0, section::header_size));

        auto typ = type();
        auto len = length();

        if (buffer.size() < section::header_size + len)
            return;

        if (typ == flag::data)
        {
            push_back(snippet(buffer.slice(section::header_size, len)));
        }
        else if (typ == (flag::data | flag::echo))
        {
            push_back(cursor(buffer.slice(section::header_size, len)));
        }
    }

    section()
    {
    }

    section(const multibuffer& buffer) : multibuffer(buffer)
    {
    }

    section(uint16_t type)
    {
        mutable_buffer header = mutable_buffer::create(header_size);
        header.set<uint16_t>(0, htons(type));
        header.set<uint16_t>(sizeof(uint16_t), 0);
        push_back(header);
    }

    section(const cursor& value)
    {
        mutable_buffer header = mutable_buffer::create(header_size);
        header.set<uint16_t>(0, htons(flag::data | flag::echo));
        header.set<uint16_t>(sizeof(uint16_t), htons(value.size()));
        push_back(header);
        push_back(value);
    }

    section(const snippet& value)
    {
        mutable_buffer header = mutable_buffer::create(header_size);
        header.set<uint16_t>(0, htons(flag::data));
        header.set<uint16_t>(sizeof(uint16_t), htons(value.size()));
        push_back(header);
        push_back(value);
    }

    inline uint16_t type() const
    {
        return ntohs(at(0).get<uint16_t>(0));
    }

    inline uint16_t length() const
    {
        return ntohs(at(0).get<uint16_t>(sizeof(uint16_t)));
    }

    inline multibuffer value() const
    {
        return slice(1, count() - 1);
    }

    bool valid() const
    {
        if (count() == 0 || at(0).size() != section::header_size)
            return false;

        switch (type())
        {
            case flag::data:
                return count() == 3 && at(1).size() == snippet::header_size && at(2).size() == length() - snippet::header_size;
            case flag::data | flag::echo:
                return count() == 2 && at(1).size() == snippet::header_size;
            default:
                break;
        }
        return count() == 1;
    }
};

struct payload : public multibuffer<const_buffer>
{
    payload()
    {
    }

    payload(const const_buffer& buffer)
    {
        const_buffer rest = buffer;

        section sect(rest);
        while (sect.valid())
        {
            push_back(sect);
            rest.crop(sect.size());
            sect = section(rest);
        }
    }

    payload(const multibuffer& buffer) : multibuffer(buffer)
    {
    }

    section advance()
    {
        if (count() == 0 || at(0).size() < section::header_size)
            return section();

        section top(*this);
        auto type = top.type();

        if (type == section::data)
        {
            top.count(3);
        }
        else if (type == (section::data | section::echo))
        {
            top.count(2);
        }
        else
        {
            top.count(1);
        }

        pop_front(top.count());

        return top;
    }
};

struct packet : public multibuffer<const_buffer>
{
    static constexpr size_t packet_sign = 0x0909;
    static constexpr size_t packet_version = 0x0100;
    static constexpr size_t header_size = 16;
    static constexpr size_t max_packet_size = 65507;
    static constexpr size_t max_payload_size = max_packet_size - header_size;

    packet(const const_buffer& buffer)
    {
        if (buffer.size() < header_size)
            return;

        push_back(buffer.slice(0, header_size));
        push_back(payload(buffer.slice(header_size, buffer.size() - header_size)));
    }

    packet(uint32_t pin)
    {
        mutable_buffer header = mutable_buffer::create(header_size);
        header.set<uint16_t>(0, 0);
        header.set<uint16_t>(sizeof(uint64_t), htons(packet_sign));
        header.set<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t), htons(packet_version));
        header.set<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2, htonl(pin));
        push_back(header);
    }

    inline bool valid() const
    {
        return count() > 0 && at(0).size() == packet::header_size;
    }

    inline uint64_t salt() const
    {
        return le64toh(at(0).get<uint64_t>(0));
    }

    inline uint16_t sign() const
    {
        return ntohs(at(0).get<uint16_t>(sizeof(uint64_t)));
    }

    inline uint16_t version() const
    {
        return ntohs(at(0).get<uint16_t>(sizeof(uint64_t) + sizeof(uint16_t)));
    }

    inline uint32_t pin() const
    {
        return ntohl(at(0).get<uint32_t>(sizeof(uint64_t) + sizeof(uint16_t) * 2));
    }

    inline payload data() const
    {
        return payload(slice(1, count() - 1));
    }
};

struct dimmer
{
    static mutable_buffer invert(uint64_t secret, const mutable_buffer& buffer)
    {
        uint8_t* ptr = buffer.data();
        uint8_t* end = buffer.data() + buffer.size();

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
