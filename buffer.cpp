#include "buffer.h"
#include <map>

namespace novemus {

std::shared_ptr<buffer_factory> buffer_factory::shared_factory() noexcept(true)
{
    static std::weak_ptr<buffer_factory> s_factory;
    static std::mutex s_mutex;

    std::unique_lock<std::mutex> lock(s_mutex);
    auto ptr = s_factory.lock();

    if (!ptr)
    {
        ptr = std::make_shared<buffer_factory>();
        s_factory = ptr;
    }

    return ptr;
}

}