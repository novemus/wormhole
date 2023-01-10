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
    typedef boost::shared_array<uint8_t> shared_array;
    typedef boost::asio::const_buffer value_type;
    typedef const boost::asio::const_buffer* const_iterator;

    const_buffer(const std::string& str) noexcept(true) 
        : const_buffer(shared_array(new uint8_t[str.size()]), 0, str.size())
    {
        std::memcpy(m_array.get(), str.data(), str.size());
    }

    const_buffer(shared_array array, size_t offset, size_t size) noexcept(true)
        : m_buffer(array.get() + offset, size)
        , m_array(array)
    {
    }
    
    const_iterator begin() const { return &m_buffer; }

    const_iterator end() const { return &m_buffer + 1; }

    const uint8_t* data() const noexcept(true)
    {
        return (const uint8_t*)m_buffer.data();
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

    const_buffer pop_front(size_t len) noexcept(false)
    {
        if (len > m_buffer.size())
            throw std::runtime_error("pop_front: out of range");

        size_t offset = (uint8_t*)m_buffer.data() - m_array.get();
        m_buffer += len;

        return const_buffer(m_array, offset, len);
    }

    const_buffer pop_back(size_t len) noexcept(false)
    {
        if (len > m_buffer.size())
            throw std::runtime_error("pop_back: out of range");

        m_buffer = boost::asio::const_buffer(m_buffer.data(), m_buffer.size() - len);

        return const_buffer(m_array, (uint8_t*)m_buffer.data() - m_array.get() + m_buffer.size(), len);
    }

    template<class type> type get(size_t off) const noexcept(false)
    {
        if (off + sizeof(type) > m_buffer.size())
            throw std::runtime_error("get: out of range");

        return *(const type*)((const uint8_t*)m_buffer.data() + off);
    }

    void copy(size_t off, size_t len, uint8_t* dst) const noexcept(false)
    {
        if (off + len > m_buffer.size())
            throw std::runtime_error("copy: out of range");

        std::memcpy(dst, (const uint8_t*)m_buffer.data() + off, len);
    }

    void truncate(size_t len) noexcept(false)
    {
        if (len > m_buffer.size())
            throw std::runtime_error("truncate: out of range");

        m_buffer = boost::asio::const_buffer(m_buffer.data(), len);
    }

    void crop(size_t len) noexcept(false)
    {
        if (len > m_buffer.size())
            throw std::runtime_error("crop: out of range");

        m_buffer += len;
    }

private:

    boost::asio::const_buffer m_buffer;
    boost::shared_array<uint8_t> m_array;
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

    mutable_buffer(const std::string& str) noexcept(true) 
        : mutable_buffer(shared_array(new uint8_t[str.size()]), 0, str.size())
    {
        std::memcpy(m_array.get(), str.data(), str.size());
    }

    mutable_buffer(shared_array array, size_t offset, size_t size) noexcept(true)
        : m_buffer(array.get() + offset, size)
        , m_array(array)
    {
    }

    const_iterator begin() const { return &m_buffer; }

    const_iterator end() const { return &m_buffer + 1; }

    uint8_t* data() const noexcept(true)
    {
        return (uint8_t*)m_buffer.data();
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

        return mutable_buffer(m_array, (uint8_t*)m_buffer.data() - m_array.get() + off, len);
    }

    mutable_buffer pop_front(size_t len) noexcept(false)
    {
        if (len > m_buffer.size())
            throw std::runtime_error("pop_front: out of range");

        size_t offset = (uint8_t*)m_buffer.data() - m_array.get();
        m_buffer += len;

        return mutable_buffer(m_array, offset, len);
    }

    mutable_buffer pop_back(size_t len) noexcept(false)
    {
        if (len > m_buffer.size())
            throw std::runtime_error("pop_back: out of range");

        m_buffer = boost::asio::mutable_buffer(m_buffer.data(), m_buffer.size() - len);

        return mutable_buffer(m_array, (uint8_t*)m_buffer.data() - m_array.get() + m_buffer.size(), len);
    }

    operator const_buffer() const noexcept(true)
    {
        return const_buffer(m_array, (uint8_t*)m_buffer.data() - m_array.get(), m_buffer.size());
    }

    template<class type> type get(size_t off) const noexcept(false)
    {
        if (off + sizeof(type) > m_buffer.size())
            throw std::runtime_error("get: out of range");

        return *(type*)((uint8_t*)m_buffer.data() + off);
    }

    template<class type> void set(size_t off, type val) noexcept(false)
    {
        if (off + sizeof(type) > m_buffer.size())
            throw std::runtime_error("set: out of range");

        *(type*)((uint8_t*)m_buffer.data() + off) = val;
    }

    void fill(size_t off, size_t len, const uint8_t* src) noexcept(false)
    {
        if (off + len > m_buffer.size())
            throw std::runtime_error("fill: out of range");
        
        std::memcpy((uint8_t*)m_buffer.data() + off, src, len);
    }

    void copy(size_t off, size_t len, uint8_t* dst) const noexcept(false)
    {
        if (off + len > m_buffer.size())
            throw std::runtime_error("copy: out of range");
        
        std::memcpy(dst, (uint8_t*)m_buffer.data() + off, len);
    }

    void crop(size_t len) noexcept(false)
    {
        if (len > m_buffer.size())
            throw std::runtime_error("crop: out of range");
        
        m_buffer += len;
    }

    void truncate(size_t len) noexcept(false)
    {
        if (len > m_buffer.size())
            throw std::runtime_error("truncate: out of range");

        m_buffer = boost::asio::mutable_buffer(m_buffer.data(), len);
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

        compress();

        iter = m_cache.emplace(size, mutable_buffer(size));
        std::memset(iter->second.data(), 0, iter->second.size());
        return iter->second;
    }

    static std::shared_ptr<buffer_factory> shared_factory() noexcept(true);

private:

    time_t m_clean;
    std::multimap<size_t, mutable_buffer> m_cache;
    std::mutex m_mutex;
};

}
