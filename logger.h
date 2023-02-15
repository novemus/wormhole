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

namespace novemus::logger {

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

std::ostream& operator<<(std::ostream& out, novemus::logger::severity level);
std::istream& operator>>(std::istream& in, novemus::logger::severity& level);

struct line
{
    line(severity level, const char* func, const char* file, int line);
    ~line();
    
    template<typename type_t> 
    line& operator<<(const type_t& value)
    {
        if (level != severity::none)
            stream << value;
        return *this;
    }

private:

    severity level;
    std::stringstream stream;
};

void set(severity level, const std::string& file = "", bool async = true);

}

#define LOG_LINE(severity) novemus::logger::line(severity, __FUNCTION__, __FILE__, __LINE__)

#define _ftl_ LOG_LINE(novemus::logger::fatal)
#define _err_ LOG_LINE(novemus::logger::error)
#define _wrn_ LOG_LINE(novemus::logger::warning)
#define _inf_ LOG_LINE(novemus::logger::info)
#define _dbg_ LOG_LINE(novemus::logger::debug)
#define _trc_ LOG_LINE(novemus::logger::trace)
