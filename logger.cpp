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
#include "logger.h"
#include <mutex>
#include <future>
#include <filesystem>
#include <sys/types.h>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#elif _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#define gettid() GetCurrentThreadId()
#endif

namespace wormhole { namespace log {

class logger
{
    severity                      m_level;
    reactor_ptr                   m_reactor;
    std::shared_ptr<std::ostream> m_sink;

public:

    logger(severity level, bool async = false, const std::string& file = "") 
        : m_level(level)
    {
        if (async)
        {
            m_reactor = std::make_shared<reactor>(1);
            m_reactor->activate();
        }

        if (file.empty())
            m_sink.reset(&std::cout, [](std::ostream*){});
        else
            m_sink = std::make_shared<std::ofstream>(file.c_str(), std::ofstream::out);
    }

    ~logger()
    {
        if (m_reactor)
            m_reactor->complete();
    }

    severity level() const
    {
        return m_level;
    }

    void append(std::string&& entry) 
    {
        auto addition = [this, line = entry]()
        {
            *m_sink << line << std::endl;
            m_sink->flush();
        };

        if (m_reactor)
            m_reactor->io().post(addition);
        else
            addition();
    };
};

std::unique_ptr<logger> g_logger = std::make_unique<logger>(severity::info);
std::mutex g_mutex;

std::ostream& operator<<(std::ostream& out, severity level)
{
    switch(level)
    {
        case severity::fatal:
            return out << "FATAL";
        case severity::error:
            return out << "ERROR";
        case severity::warning:
            return out << "WARN";
        case severity::info:
            return out << "INFO";
        case severity::debug:
            return out << "DEBUG";
        case severity::trace:
            return out << "TRACE";
        default:
            return out << "NONE";
    }
    return out;
}

std::istream& operator>>(std::istream& in, severity& level)
{
    std::string str;
    in >> str;

    if (str == "fatal" || str == "FATAL" || str == "1")
        level = severity::fatal;
    else if (str == "error" || str == "ERROR" || str == "2")
        level = severity::error;
    else if (str == "warning" || str == "WARN" || str == "3")
        level = severity::warning;
    else if (str == "info" || str == "INFO" || str == "4")
        level = severity::info;
    else if (str == "debug" || str == "DEBUG" || str == "5")
        level = severity::debug;
    else if (str == "trace" || str == "TRACE" || str == "6")
        level = severity::trace;
    else
        level = severity::info;

    return in;
}

severity level()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_logger->level();
}

line::line(severity sev, const char* func, const char* file, int line) noexcept(true) 
    : std::ostream(nullptr)
    , level(sev <= log::level() ? sev : severity::none)
{
    if (level != severity::none)
    {
        rdbuf(stream.rdbuf());

        stream << "[" << gettid() << "] " << boost::posix_time::microsec_clock::local_time() << " " << level << ": ";
        if (log::level() > severity::info)
        {
            auto name = boost::filesystem::path(file).filename().string();
            stream << "[" << func << " in " << name << ":" << line << "] ";
        }
    }
}

line::~line() noexcept(true)
{
    if (level != severity::none)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_logger->append(std::move(stream.str()));
    }
}

void set(severity level, bool async, const std::string& file) noexcept(false)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_logger = std::make_unique<logger>(level, async, file);
}

}}
