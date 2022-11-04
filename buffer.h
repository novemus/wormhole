#pragma once

#include <cstring>
#include <type_traits>
#include <boost/shared_array.hpp>

namespace salt {

template<class value_t> class shared_buffer
{
    boost::shared_array<value_t> m_buffer;
    size_t m_beg;
    size_t m_end;

    shared_buffer(boost::shared_array<value_t> buffer, size_t beg, size_t end)
        : m_buffer(buffer)
        , m_beg(beg)
        , m_end(end)
    {
    }

public:

    shared_buffer(size_t len) 
        : m_buffer(new value_t[len])
        , m_beg(0)
        , m_end(len)
    {
        std::memset(m_buffer.get(), 0, len);
    }

    value_t* data() const
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

    buffer slice(size_t off, size_t len) const
    {
        if (off > size() || off + len > size())
            throw std::runtime_error("slice: out of range");

        return buffer(m_buffer, m_beg + off, m_beg + off + len);
    }

    bool unique() const
    {
        return m_buffer.unique();
    }

    operator shared_buffer<const value_t>&() const
    {
        if (!std::is_const<value_t>::value)
            return reinterpret_cast<const shared_buffer<const value_t>&>(*this);
        return *this;
    }
};

typedef shared_buffer<uint8_t> mutable_buffer;
typedef shared_buffer<const uint8_t> const_buffer;

}
