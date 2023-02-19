/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "reactor.h"
#include <mutex>

namespace wormhole {

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
