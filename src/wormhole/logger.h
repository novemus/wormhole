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

#include <wormhole/export.h>
#include <iostream>
#include <sstream>

namespace wormhole { namespace log {

enum severity
{
    none,
    fatal,
    error,
    warning,
    info,
    debug,
    trace
};

LIBWORMHOLE_EXPORT std::ostream& operator<<(std::ostream& out, severity level);
LIBWORMHOLE_EXPORT std::istream& operator>>(std::istream& in, severity& level);

struct line : public std::stringstream
{
    line(severity level, const char* func, const char* file, int line)
    {
        line::start(*this, level, func, file, line);
    }

    ~line() override
    { 
        line::flush(*this);
    }

private:

    LIBWORMHOLE_EXPORT static void start(std::stringstream& out, severity level, const char* func, const char* file, int line);
    LIBWORMHOLE_EXPORT static void flush(std::stringstream& out);
};

LIBWORMHOLE_EXPORT void set(severity scope, const std::string& file = "") noexcept(false);

}}

#define MAKE_LOG_LINE(level) (wormhole::log::line(level, __FUNCTION__, __FILE__, __LINE__))

#define _ftl_ MAKE_LOG_LINE(wormhole::log::fatal)
#define _err_ MAKE_LOG_LINE(wormhole::log::error)
#define _wrn_ MAKE_LOG_LINE(wormhole::log::warning)
#define _inf_ MAKE_LOG_LINE(wormhole::log::info)
#define _dbg_ MAKE_LOG_LINE(wormhole::log::debug)
#define _trc_ MAKE_LOG_LINE(wormhole::log::trace)
