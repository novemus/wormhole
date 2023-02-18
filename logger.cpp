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
#include <fstream>
#include <mutex>
#include <future>
#include <filesystem>
#include <sys/types.h>
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

namespace novemus::logger {

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
            m_reactor = std::make_shared<novemus::reactor>(1);
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

std::ostream& operator<<(std::ostream& out, novemus::logger::severity level)
{
    switch(level)
    {
        case novemus::logger::fatal:
            return out << "FATAL";
        case novemus::logger::error:
            return out << "ERROR";
        case novemus::logger::warning:
            return out << "WARN";
        case novemus::logger::info:
            return out << "INFO";
        case novemus::logger::debug:
            return out << "DEBUG";
        case novemus::logger::trace:
            return out << "TRACE";
        default:
            return out << "NONE";
    }
    return out;
}

std::istream& operator>>(std::istream& in, novemus::logger::severity& level)
{
    std::string str;
    in >> str;

    if (str == "fatal" || str == "FATAL")
        level = novemus::logger::fatal;
    else if (str == "error" || str == "ERROR")
        level = novemus::logger::error;
    else if (str == "warning" || str == "WARN")
        level = novemus::logger::warning;
    else if (str == "info" || str == "INFO")
        level = novemus::logger::info;
    else if (str == "debug" || str == "DEBUG")
        level = novemus::logger::debug;
    else if (str == "trace" || str == "TRACE")
        level = novemus::logger::trace;
    else
        level = novemus::logger::none;

    return in;
}

severity level()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_logger->level();
}

line::line(severity sev, const char* func, const char* file, int line) noexcept(true) 
    : level(sev <= novemus::logger::level() ? sev : severity::none)
{
    if (level != severity::none)
    {
         stream << "[" << gettid() << "] " << boost::posix_time::microsec_clock::local_time() << " " << level << ": ";
        if (novemus::logger::level() > severity::info)
        {
            auto name = std::filesystem::path(file).filename().string();
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

}
