#pragma once

#include <list>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <boost/shared_array.hpp>

namespace tubus {

struct const_buffer
{
    const_buffer(size_t len)
        : m_buffer(new uint8_t[len])
        , m_beg(0)
        , m_end(len)
    {
        std::memset(m_buffer.get(), 0, len);
    }

    const_buffer(const std::string& data)
        : m_buffer(new uint8_t[data.size()])
        , m_beg(0)
        , m_end(data.size())
    {
        std::memcpy(m_buffer.get(), data.data(), data.size());
    }

    const uint8_t* data() const
    {
        return m_buffer.get() + m_beg;
    }
    
    size_t size() const
    {
        return m_end - m_beg;
    }

    void shrink(size_t len)
    {
        if (len > size())
            throw std::runtime_error("shrink: out of range");

        m_end = m_beg + len;
    }

    void prune(size_t len)
    {
        if (len > size())
            throw std::runtime_error("prune: out of range");

        m_beg += len;
    }

    const_buffer slice(size_t off, size_t len) const
    {
        if (off > size() || off + len > size())
            throw std::runtime_error("slice: out of range");

        return const_buffer(m_buffer, m_beg + off, m_beg + off + len);
    }

    bool unique() const
    {
        return m_buffer.unique();
    }

protected:

    const_buffer(boost::shared_array<uint8_t> buffer, size_t beg, size_t end)
        : m_buffer(buffer)
        , m_beg(beg)
        , m_end(end)
    {
    }

    boost::shared_array<uint8_t> m_buffer;
    size_t m_beg;
    size_t m_end;
};

struct mutable_buffer : public const_buffer
{
    mutable_buffer(size_t len) : const_buffer(len)
    {
    }

    uint8_t* data()
    {
        return m_buffer.get() + m_beg;
    }

    mutable_buffer slice(size_t off, size_t len) const
    {
        if (off > size() || off + len > size())
            throw std::runtime_error("slice: out of range");

        return mutable_buffer(m_buffer, m_beg + off, m_beg + off + len);
    }

protected:

    mutable_buffer(boost::shared_array<uint8_t> buffer, size_t beg, size_t end) : const_buffer(buffer, beg, end)
    {
    }
};

struct buffer_factory
{
    buffer_factory(size_t buff_size) : m_buff_size(buff_size)
    {
    }

    mutable_buffer make_buffer()
    {
        auto it = m_cache.begin();
        while (it != m_cache.end())
        {
            if (it->first.unique())
            {
                mutable_buffer buff = it->first;
                std::memset(buff.data(), 0, buff.size());

                it->second = std::time(0);

                compress_cache();

                return buff;
            }
            ++it;
        }

        m_cache.emplace_back(mutable_buffer(m_buff_size), std::time(0));

        return m_cache.back().first;
    }

private:

    void compress_cache()
    {
        static const time_t TTL = 30;
        time_t now = std::time(0);

        auto it = m_cache.begin();
        while (it != m_cache.end())
        {
            if (it->first.unique() && it->second + TTL > now)
                it = m_cache.erase(it);
            else
                ++it;
        }
    }

    size_t m_buff_size;
    std::list<std::pair<mutable_buffer, time_t>> m_cache;
};
}
