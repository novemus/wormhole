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

struct const_buffer : public boost::asio::const_buffer
{
    typedef boost::shared_array<const uint8_t> shared_array;

    const_buffer(size_t size) noexcept(true) 
        : const_buffer(shared_array(new uint8_t[size]), 0, size)
    {
    }

    const_buffer(shared_array array, size_t offset, size_t size) noexcept(true)
        : boost::asio::const_buffer(array.get() + offset, size)
        , m_array(array)
    {
    }

    bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    const_buffer slice(size_t off, size_t len) const noexcept(false)
    {
        if (off > size() || off + len > size())
            throw std::runtime_error("slice: out of range");

        return const_buffer(m_array, off, len);
    }

private:

    shared_array m_array;
};

struct mutable_buffer : public virtual boost::asio::mutable_buffer
{
    typedef boost::shared_array<uint8_t> shared_array;

    mutable_buffer(size_t size) noexcept(true) 
        : mutable_buffer(shared_array(new uint8_t[size]), 0, size)
    {
    }

    mutable_buffer(shared_array array, size_t offset, size_t size) noexcept(true)
        : boost::asio::mutable_buffer(array.get() + offset, size)
        , m_array(array)
    {
    }

    bool unique() const noexcept(true)
    {
        return m_array.unique();
    }

    mutable_buffer slice(size_t off, size_t len) const noexcept(false)
    {
        if (off > size() || off + len > size())
            throw std::runtime_error("slice: out of range");

        return mutable_buffer(m_array, off, len);
    }
    
    operator const_buffer() const noexcept(true)
    {
        return const_buffer(m_array, (uint8_t*)data() - m_array.get(), size());
    }

    static mutable_buffer create(size_t size) noexcept(true);

private:

    boost::shared_array<uint8_t> m_array;
};

}
