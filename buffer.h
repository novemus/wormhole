#pragma once

#include <list>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <type_traits>
#include <boost/asio.hpp>
#include <boost/shared_array.hpp>

namespace novemus {

struct const_buffer
{
    typedef boost::shared_array<const uint8_t> shared_array;
    typedef boost::asio::const_buffer value_type;
    typedef const boost::asio::const_buffer* const_iterator;

    const_buffer(size_t size) noexcept(true) 
        : const_buffer(shared_array(new uint8_t[size]), 0, size)
    {
    }

    const_buffer(shared_array array, size_t offset, size_t size) noexcept(true)
        : m_buffer(array.get() + offset, size)
        , m_array(array)
    {
    }
    
    const_iterator begin() const { return &m_buffer; }

    const_iterator end() const { return &m_buffer + 1; }

    const void* data() const noexcept(true)
    {
        return m_buffer.data();
    }

    std::size_t size() const noexcept(true)
    {
        return m_buffer.size();
    }
    
    bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    const_buffer slice(size_t off, size_t len) const noexcept(false)
    {
        if (off > m_buffer.size() || off + len > m_buffer.size())
            throw std::runtime_error("slice: out of range");

        size_t offset = (uint8_t*)m_buffer.data() - m_array.get();
        return const_buffer(m_array, offset + off, len);
    }

private:

    boost::asio::const_buffer m_buffer;
    boost::shared_array<const uint8_t> m_array;
};

struct mutable_buffer
{
    typedef boost::shared_array<uint8_t> shared_array;
    typedef boost::asio::mutable_buffer value_type;
    typedef const boost::asio::mutable_buffer* const_iterator;

    mutable_buffer(size_t size) noexcept(true) 
        : mutable_buffer(shared_array(new uint8_t[size]), 0, size)
    {
    }

    mutable_buffer(shared_array array, size_t offset, size_t size) noexcept(true)
        : m_buffer(array.get() + offset, size)
        , m_array(array)
    {
    }

    const_iterator begin() const { return &m_buffer; }

    const_iterator end() const { return &m_buffer + 1; }

    void* data() const noexcept(true)
    {
        return m_buffer.data();
    }

    std::size_t size() const noexcept(true)
    {
        return m_buffer.size();
    }
    
    bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    mutable_buffer slice(size_t off, size_t len) const noexcept(false)
    {
        if (off > m_buffer.size() || off + len > m_buffer.size())
            throw std::runtime_error("slice: out of range");

        size_t offset = (uint8_t*)m_buffer.data() - m_array.get();
        return mutable_buffer(m_array, offset + off, len);
    }
    
    operator const_buffer() const noexcept(true)
    {
        size_t offset = (uint8_t*)m_buffer.data() - m_array.get();
        return const_buffer(m_array, offset, m_buffer.size());
    }

private:

    boost::asio::mutable_buffer m_buffer;
    boost::shared_array<uint8_t> m_array;
};

class buffer_factory
{
    void compress()
    {
        static const time_t cleanup_period = 30;

        time_t now = std::time(0);
        if (m_clean < now)
        {
            auto it = m_cache.begin();
            while (it != m_cache.end())
            {
                if (it->second.unique())
                    it = m_cache.erase(it);
                else
                    ++it;
            }

            m_clean = now + cleanup_period;
        }
    }

public:

    buffer_factory() : m_clean(std::time(0))
    {
    }

    mutable_buffer obtain(size_t size) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_cache.lower_bound(size);
        while (iter != m_cache.end())
        {
            if (iter->second.unique())
            {
                mutable_buffer buf = iter->second;
                std::memset(buf.data(), 0, buf.size());

                compress();

                return buf;
            }
            ++iter;
        }

        iter = m_cache.emplace(size, mutable_buffer(size));
        return iter->second;
    }

    static std::shared_ptr<buffer_factory> shared_factory() noexcept(true);

private:

    time_t m_clean;
    std::multimap<size_t, mutable_buffer> m_cache;
    std::mutex m_mutex;
};

}
