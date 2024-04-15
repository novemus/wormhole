/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "logger.h"
#include <stdexcept>
#include <stdio.h>
#include <mutex>
#include <regex>
#include <filesystem>
#include <sys/types.h>
#include <boost/date_time/posix_time/posix_time.hpp>

#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#define getpid() syscall(SYS_getpid)
#elif __APPLE__
#include <unistd.h>
uint64_t gettid()
{
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
}
#elif _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#include <io.h>
#define gettid() GetCurrentThreadId()
#define getpid() GetCurrentProcessId()
#define dup(handle) _dup(handle)
#define dup2(src, dst) _dup2(src, dst)
#define fileno(file) _fileno(file)
#endif

namespace wormhole { namespace log {

int         g_out = dup(1);
int         g_err = dup(2);
FILE*       g_file = nullptr;
severity    g_level = severity::info;
std::mutex  g_mutex;

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

severity level() noexcept(true) 
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_level;
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
        std::cout << stream.rdbuf() << std::endl;
    }
}

void set(severity level, const std::string& file) noexcept(false)
{
    fflush(stdout);
    fflush(stderr);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_level = level;

    if (!file.empty())
    {
        if (g_file)
        {
            fclose(g_file);
            g_file = nullptr;
        }

        auto path = std::regex_replace(file, std::regex("%p"), std::to_string(getpid()));

        g_file = fopen(path.c_str(), "a");
        if(!g_file)
            throw std::runtime_error("can't open log file");

        dup2(fileno(g_file), 1);
        dup2(fileno(g_file), 2);
    }
    else if (g_file)
    {
        fclose(g_file);
        g_file = nullptr;

        dup2(g_out, 1);
        dup2(g_err, 2);
    }
}

}}
