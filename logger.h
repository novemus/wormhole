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

#include <iostream>
#include <sstream>
#include <functional>

namespace wormhole::log {

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

std::ostream& operator<<(std::ostream& out, severity level);
std::istream& operator>>(std::istream& in, severity& level);

struct line : public std::ostream
{
    line(severity sev, const char* func, const char* file, int line) noexcept(true);
    ~line() noexcept(true);

private:

    severity level;
    std::stringstream stream;
};

void set(severity level, bool async = false, const std::string& file = "") noexcept(false);

}

#define MAKE_LOG_LINE(severity) wormhole::log::line(severity, __FUNCTION__, __FILE__, __LINE__)

#define _ftl_ MAKE_LOG_LINE(wormhole::log::fatal)
#define _err_ MAKE_LOG_LINE(wormhole::log::error)
#define _wrn_ MAKE_LOG_LINE(wormhole::log::warning)
#define _inf_ MAKE_LOG_LINE(wormhole::log::info)
#define _dbg_ MAKE_LOG_LINE(wormhole::log::debug)
#define _trc_ MAKE_LOG_LINE(wormhole::log::trace)
