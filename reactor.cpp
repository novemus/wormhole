#include "reactor.h"
#include <mutex>

namespace novemus {

boost::asio::io_context& reactor::shared_io() noexcept(true)
{
    std::shared_ptr<reactor> s_reactor = std::make_shared<reactor>();
    return s_reactor->io();
}

}
