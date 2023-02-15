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

severity      g_level(severity::info);
reactor_ptr   g_reactor;
std::ofstream g_file;
std::mutex    g_mutex;

void append(std::string&& entry) 
{
    auto job = [line = entry]()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file.is_open())
            g_file << line << std::endl;
        else
            std::cout << line << std::endl;
    };

    if (g_reactor)
        g_reactor->io().post(job);
    else
        job();
};

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
    return g_level;
}

line::line(severity l, const char* func, const char* file, int line) : level(l <= novemus::logger::level() ? l : severity::none)
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

line::~line()
{
    if (level != severity::none)
    {
        novemus::logger::append(std::move(stream.str()));
    }
}

void set(severity level, const std::string& file, bool async)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (async)
    {
        if (!g_reactor)
        {
            g_reactor = std::make_shared<novemus::reactor>(1);
            g_reactor->activate();
        }
    }
    else
    {
        if (g_reactor)
        {
            g_reactor->terminate();
            g_reactor.reset();
        }
    }

    if (file.empty())
    {
        g_file.close();
    }
    else
    {
        g_file.open(file);
    }
    g_level = level;
}

}
