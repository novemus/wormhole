#include "reactor.h"
#include <mutex>

namespace novemus {

reactor_ptr shared_reactor() noexcept(true)
{
    static std::weak_ptr<reactor> s_reactor;
    static std::mutex s_mutex;

    std::unique_lock<std::mutex> lock(s_mutex);
    auto ptr = s_reactor.lock();

    if (!ptr)
    {
        ptr = std::make_shared<reactor>();
        ptr->activate();
        s_reactor = ptr;
    }

    return ptr;
}

}
