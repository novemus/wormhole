#include "buffer.h"
#include <map>

namespace novemus {

mutable_buffer mutable_buffer::create(size_t size) noexcept(true)
{
    static std::shared_ptr<buffer_factory> s_factory = std::make_shared<buffer_factory>(64);
    return s_factory->obtain(size);
}

}
