/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "buffer.h"
#include <map>

namespace wormhole {

mutable_buffer mutable_buffer::create(size_t size) noexcept(true)
{
    static std::mutex s_mutex;
    static std::map<size_t, std::shared_ptr<buffer_factory>> s_pool;

    std::unique_lock<std::mutex> lock(s_mutex);
    auto res = s_pool.emplace(size, std::make_shared<buffer_factory>(size));
    return res.first->second->obtain();
}

}
