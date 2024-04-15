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

#ifdef _MSC_VER
#define LOGGER_CLASS_EXPORT_DECLSPEC __declspec(dllexport)
#define LOGGER_CLASS_IMPORT_DECLSPEC
#endif // _MSC_VER

#ifdef __GNUC__
#define LOGGER_CLASS_EXPORT_DECLSPEC __attribute__ ((visibility("default")))
#define LOGGER_CLASS_IMPORT_DECLSPEC 
#endif

#ifdef WORMHOLE_EXPORTS
#define LOGGER_CLASS_DECLSPEC LOGGER_CLASS_EXPORT_DECLSPEC
#else
#define LOGGER_CLASS_DECLSPEC LOGGER_CLASS_IMPORT_DECLSPEC
#endif

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

LOGGER_CLASS_DECLSPEC std::ostream& operator<<(std::ostream& out, severity level);
LOGGER_CLASS_DECLSPEC std::istream& operator>>(std::istream& in, severity& level);

struct line : public std::ostream
{
    LOGGER_CLASS_DECLSPEC line(severity sev, const char* func, const char* file, int line) noexcept(true);
    LOGGER_CLASS_DECLSPEC ~line() noexcept(true);

private:

    severity level;
    std::stringstream stream;
};

LOGGER_CLASS_DECLSPEC void set(severity level, const std::string& file = "") noexcept(false);

}}

#define MAKE_LOG_LINE(severity) wormhole::log::line(severity, __FUNCTION__, __FILE__, __LINE__)

#define _ftl_ MAKE_LOG_LINE(wormhole::log::fatal)
#define _err_ MAKE_LOG_LINE(wormhole::log::error)
#define _wrn_ MAKE_LOG_LINE(wormhole::log::warning)
#define _inf_ MAKE_LOG_LINE(wormhole::log::info)
#define _dbg_ MAKE_LOG_LINE(wormhole::log::debug)
#define _trc_ MAKE_LOG_LINE(wormhole::log::trace)
