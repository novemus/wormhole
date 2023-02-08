/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#pragma once

#include <boost/log/trivial.hpp>

#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#elif _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#define gettid() GetCurrentThreadId()
#endif

namespace novemus::logger {

void set(const std::string& file, boost::log::trivial::severity_level level);

}

#define NOVEMUS_LOG(severity) BOOST_LOG_TRIVIAL(severity) << "[" << gettid() << "] " << __FUNCTION__ << ": "

#define _trc_ NOVEMUS_LOG(trace)
#define _dbg_ NOVEMUS_LOG(debug)
#define _inf_ NOVEMUS_LOG(info)
#define _wrn_ NOVEMUS_LOG(warning)
#define _err_ NOVEMUS_LOG(error)
#define _ftl_ NOVEMUS_LOG(fatal)
