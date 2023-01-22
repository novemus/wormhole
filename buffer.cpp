#include "buffer.h"
#include <map>

namespace novemus {

mutable_buffer mutable_buffer::create(size_t size) noexcept(true)
{
    static std::mutex s_mutex;
    static std::map<size_t, std::shared_ptr<buffer_factory>> s_pool;

    std::unique_lock<std::mutex> lock(s_mutex);
    auto res = s_pool.emplace(size, std::make_shared<buffer_factory>(size));
    return res.first->second->obtain();
}

}
