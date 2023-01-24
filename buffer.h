#pragma once

#include <list>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <deque>
#include <numeric>
#include <iostream>
#include <type_traits>
#include <boost/asio.hpp>
#include <boost/shared_array.hpp>

namespace novemus {

struct const_buffer
{
    typedef boost::asio::const_buffer value_type;
    typedef const boost::asio::const_buffer* const_iterator;

    const_buffer() noexcept(true) 
    {
    }

    const_buffer(const void* data, size_t size) noexcept(true) 
        : m_array(new uint8_t[size])
        , m_frame(m_array.get(), size)
    {
        std::memcpy(m_array.get(), data, size);
    }

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

    inline const uint8_t* data() const noexcept(true)
    {
        return (const uint8_t*)m_frame.data();
    }

    inline size_t size() const noexcept(true)
    {
        return m_frame.size();
    }

    inline const_iterator begin() const { return &m_frame; }

    inline const_iterator end() const { return &m_frame + 1; }

    inline bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    inline const_buffer slice(size_t pos, size_t len) const noexcept(false)
    {
        if (pos > size() || pos + len > size())
            throw std::runtime_error("const_buffer::slice: out of range");

        size_t offset = data() - m_array.get();
        return const_buffer(m_array, offset + pos, len);
    }

    inline const_buffer pop_front(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::pop_front: out of range");

        size_t offset = data() - m_array.get();
        m_frame += len;

        return const_buffer(m_array, offset, len);
    }

    inline const_buffer pop_back(size_t len) noexcept(false)
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

    inline void copy(size_t pos, size_t len, uint8_t* dst) const noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("const_buffer::copy: out of range");

        std::memcpy(dst, data() + pos, len);
    }

    inline void truncate(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::truncate: out of range");

        m_frame = boost::asio::const_buffer(data(), len);
    }

    inline void crop(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("const_buffer::crop: out of range");

        m_frame += len;
    }

    template<class value> static const_buffer create(const value& data) noexcept(true)
    {
        return const_buffer(&data, sizeof(value));
    }

    inline const_buffer& operator+=(size_t len) noexcept(true)
    {
        m_frame += len;
        return *this;
    }

    inline operator const boost::asio::const_buffer&() const noexcept(true)
    {
        return m_frame;
    }

private:

    boost::shared_array<uint8_t> m_array;
    boost::asio::const_buffer m_frame;
};

struct mutable_buffer
{
    typedef boost::asio::mutable_buffer value_type;
    typedef const boost::asio::mutable_buffer* const_iterator;

    mutable_buffer() noexcept(true)
    {
    }

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

    inline uint8_t* data() const noexcept(true)
    {
        return (uint8_t*)m_frame.data();
    }
    
    inline size_t size() const noexcept(true)
    {
        return m_frame.size();
    }

    inline const_iterator begin() const { return &m_frame; }

    inline const_iterator end() const { return &m_frame + 1; }

    inline bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    inline mutable_buffer slice(size_t pos, size_t len) const noexcept(false)
    {
        if (pos > size() || pos + len > size())
            throw std::runtime_error("mutable_buffer::slice: out of range");

        return mutable_buffer(m_array, data() - m_array.get() + pos, len);
    }

    inline mutable_buffer pop_front(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::pop_front: out of range");

        size_t offset = data() - m_array.get();
        m_frame += len;

        return mutable_buffer(m_array, offset, len);
    }

    inline mutable_buffer pop_back(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::pop_back: out of range");

        m_frame = boost::asio::mutable_buffer(data(), size() - len);

        return mutable_buffer(m_array, data() - m_array.get() + size(), len);
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

    inline void fill(size_t pos, size_t len, const uint8_t* src) noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("mutable_buffer::fill: out of range");
        
        std::memcpy(data() + pos, src, len);
    }

    inline void copy(size_t pos, size_t len, uint8_t* dst) const noexcept(false)
    {
        if (pos + len > size())
            throw std::runtime_error("mutable_buffer::copy: out of range");
        
        std::memcpy(dst, data() + pos, len);
    }

    inline void crop(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::crop: out of range");
        
        m_frame += len;
    }

    inline void truncate(size_t len) noexcept(false)
    {
        if (len > size())
            throw std::runtime_error("mutable_buffer::truncate: out of range");

        m_frame = boost::asio::mutable_buffer(data(), len);
    }

    inline mutable_buffer& operator+=(size_t len) noexcept(true)
    {
        m_frame += len;
        return *this;
    }

    inline operator const const_buffer&() const noexcept(true)
    {
        return reinterpret_cast<const const_buffer&>(*this);
    }

    inline operator const boost::asio::mutable_buffer&() const noexcept(true)
    {
        return m_frame;
    }

    static mutable_buffer create(size_t size) noexcept(true);

private:

    boost::shared_array<uint8_t> m_array;
    boost::asio::mutable_buffer m_frame;
};

class buffer_factory : public std::enable_shared_from_this<buffer_factory>
{
    typedef std::weak_ptr<buffer_factory> weak_ptr;
    typedef std::shared_ptr<buffer_factory> self_ptr;

    static void destroy(weak_ptr weak, uint8_t* ptr)
    {
        self_ptr self = weak.lock();
        if (self)
        {
            if (self->cache(ptr))
                return;
        }
        delete[] ptr;
    }

    bool cache(uint8_t* ptr)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        static const size_t s_max_cache_size = 16;

        if (m_cache.size() < s_max_cache_size)
        {
            m_cache.emplace_back(ptr,
                std::bind(&buffer_factory::destroy, shared_from_this(), std::placeholders::_1)
                );
            return true;
        }

        return false;
    }

public:

    buffer_factory(size_t size) : m_size(size)
    {
    }

    mutable_buffer obtain() noexcept(true)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        boost::shared_array<uint8_t> array;

        auto iter = m_cache.begin();
        if (iter == m_cache.end())
        {
            array.reset(
                new uint8_t[m_size],
                std::bind(&buffer_factory::destroy, shared_from_this(), std::placeholders::_1)
                );
        }
        else
        {
            array = *iter;
            m_cache.erase(iter);
        }

        return mutable_buffer(array, m_size);
    }

private:

    size_t m_size;
    std::deque<boost::shared_array<uint8_t>> m_cache;
    std::mutex m_mutex;
};

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

}
