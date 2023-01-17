#pragma once

#include <list>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <type_traits>
#include <boost/asio.hpp>
#include <boost/shared_array.hpp>

namespace novemus {

struct const_buffer
{
    typedef boost::asio::const_buffer value_type;
    typedef const boost::asio::const_buffer* const_iterator;

    const_buffer(const std::string& str) noexcept(true) 
        : m_array(new uint8_t[str.size()])
        , m_frame(m_array.get(), str.size())
    {
        std::memcpy(m_array.get(), str.data(), str.size());
    }

    const_buffer(boost::shared_array<uint8_t> array, size_t size) noexcept(true)
        : const_buffer(array, 0, size)
    {
    }

    const_buffer(boost::shared_array<uint8_t> array, size_t offset, size_t size) noexcept(true)
        : m_array(array)
        , m_frame(array.get() + offset, size)
    {
    }

    const uint8_t* data() const noexcept(true)
    {
        return (const uint8_t*)m_frame.data();
    }

    size_t size() const noexcept(true)
    {
        return m_frame.size();
    }

    const_iterator begin() const { return &m_frame; }

    const_iterator end() const { return &m_frame + 1; }

    bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    const_buffer slice(size_t pos, size_t len) const noexcept(false)
    {
        if (pos > size() || pos + len > size())
            throw std::runtime_error("const_buffer::slice: out of range");

        size_t offset = data() - m_array.get();
        return const_buffer(m_array, offset + pos, len);
    }

    const_buffer pop_front(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::pop_front: out of range");

        size_t offset = data() - m_array.get();
        m_frame += len;

        return const_buffer(m_array, offset, len);
    }

    const_buffer pop_back(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::pop_back: out of range");

        m_frame = boost::asio::const_buffer(data(), size() - len);

        return const_buffer(m_array, data() - m_array.get() + size(), len);
    }

    template<class type> type get(size_t pos) const noexcept(false)
    {
        if (pos + sizeof(type) > size())
            throw std::runtime_error("const_buffer::get: out of range");

        return *(const type*)(data() + pos);
    }

    void copy(size_t pos, size_t len, uint8_t* dst) const noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("const_buffer::copy: out of range");

        std::memcpy(dst, data() + pos, len);
    }

    void truncate(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::truncate: out of range");

        m_frame = boost::asio::const_buffer(data(), len);
    }

    void crop(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::crop: out of range");

        m_frame += len;
    }

    const_buffer& operator+=(size_t len) noexcept(true)
    {
        m_frame += len;
        return *this;
    }

private:

    boost::shared_array<uint8_t> m_array;
    boost::asio::const_buffer m_frame;
};

struct mutable_buffer
{
    typedef boost::asio::mutable_buffer value_type;
    typedef const boost::asio::mutable_buffer* const_iterator;

    mutable_buffer(size_t size) noexcept(true) 
        : m_array(new uint8_t[size])
        , m_frame(m_array.get(), size)
    {
    }

    mutable_buffer(const std::string& str) noexcept(true) 
        : m_array(new uint8_t[str.size()])
        , m_frame(m_array.get(), str.size())
    {
        std::memcpy(m_array.get(), str.data(), str.size());
    }

    mutable_buffer(const const_buffer& buffer) noexcept(true) 
        : m_array(new uint8_t[buffer.size()])
        , m_frame(m_array.get(), buffer.size())
    {
        std::memcpy(m_array.get(), buffer.data(), buffer.size());
    }

    mutable_buffer(boost::shared_array<uint8_t> array, size_t size) noexcept(true)
        : mutable_buffer(array, 0, size)
    {
    }

    mutable_buffer(boost::shared_array<uint8_t> array, size_t offset, size_t size) noexcept(true)
        : m_array(array)
        , m_frame(m_array.get() + offset, size)
    {
    }

    uint8_t* data() const noexcept(true)
    {
        return (uint8_t*)m_frame.data();
    }
    
    size_t size() const noexcept(true)
    {
        return m_frame.size();
    }

    const_iterator begin() const { return &m_frame; }

    const_iterator end() const { return &m_frame + 1; }

    bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    mutable_buffer slice(size_t pos, size_t len) const noexcept(false)
    {
        if (pos > size() || pos + len > size())
            throw std::runtime_error("mutable_buffer::slice: out of range");

        return mutable_buffer(m_array, data() - m_array.get() + pos, len);
    }

    mutable_buffer pop_front(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::pop_front: out of range");

        size_t offset = data() - m_array.get();
        m_frame += len;

        return mutable_buffer(m_array, offset, len);
    }

    mutable_buffer pop_back(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::pop_back: out of range");

        m_frame = boost::asio::mutable_buffer(data(), size() - len);

        return mutable_buffer(m_array, data() - m_array.get() + size(), len);
    }

    operator const_buffer() const noexcept(true)
    {
        return const_buffer(m_array, data() - m_array.get(), size());
    }

    template<class type> type get(size_t pos) const noexcept(false)
    {
        if (pos + sizeof(type) > size())
            throw std::runtime_error("mutable_buffer::get: out of range");

        return *(type*)(data() + pos);
    }

    template<class type> void set(size_t pos, type val) noexcept(false)
    {
        if (pos + sizeof(type) > size())
            throw std::runtime_error("mutable_buffer::set: out of range");

        *(type*)(data() + pos) = val;
    }

    void fill(size_t pos, size_t len, const uint8_t* src) noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("mutable_buffer::fill: out of range");
        
        std::memcpy(data() + pos, src, len);
    }

    void copy(size_t pos, size_t len, uint8_t* dst) const noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("mutable_buffer::copy: out of range");
        
        std::memcpy(dst, data() + pos, len);
    }

    void crop(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::crop: out of range");
        
        m_frame += len;
    }

    void truncate(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::truncate: out of range");

        m_frame = boost::asio::mutable_buffer(data(), len);
    }

    mutable_buffer& operator+=(size_t len) noexcept(true)
    {
        m_frame += len;
        return *this;
    }

private:

    boost::shared_array<uint8_t> m_array;
    boost::asio::mutable_buffer m_frame;
};

class buffer_factory : public std::enable_shared_from_this<buffer_factory>
{
    void cache(uint8_t* ptr, size_t size)
    {
        static const size_t max_cache_size = 64;

        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_cache.size() == max_cache_size)
            m_cache.erase(m_cache.begin());

        m_cache.emplace(size, boost::shared_array<uint8_t>(ptr));
    }

public:

    buffer_factory()
    {
    }

    mutable_buffer obtain(size_t size) noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        auto iter = m_cache.lower_bound(size);
        if (iter == m_cache.end())
        {
            auto array = boost::shared_array<uint8_t>(new uint8_t[size], [size, self = shared_from_this()](uint8_t* ptr)
            {
                self->cache(ptr, size);
            });

            std::memset(array.get(), 0, size);
            return mutable_buffer(array, size);
        }

        auto array = iter->second;
        m_cache.erase(iter);

        std::memset(array.get(), 0, size);
        return mutable_buffer(array, size);
    }

    static std::shared_ptr<buffer_factory> shared_factory() noexcept(true);

private:

    std::multimap<size_t, boost::shared_array<uint8_t>> m_cache;
    std::mutex m_mutex;
};

}
