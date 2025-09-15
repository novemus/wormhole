/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <mutex>
#include <regex>
#include <memory>
#include <fstream>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <wormhole/logger.h>

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
#endif

namespace wormhole { namespace log {

namespace
{
    class sink
    {
        using executor_work_guard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

        std::ofstream m_file;
        boost::asio::io_context m_io;
        executor_work_guard m_work;
        std::thread m_thread;

    public:

        sink(const std::string& path) : m_work(boost::asio::make_work_guard(m_io))
        {
            m_thread = std::thread([&]()
            {
                try
                {
                    m_io.run();
                }
                catch(const std::exception& ex)
                {
                    std::cout << ex.what() << std::endl;
                }
            });

            if (!path.empty())
            {
                auto file = std::regex_replace(path, std::regex("%p"), std::to_string(getpid()));

                m_file = std::ofstream(file.c_str(), std::ios_base::app);
                if(!m_file.good())
                    throw std::runtime_error("can't open log file");
            }
        }

        ~sink()
        {
            m_work.reset();
            
            if (m_thread.joinable())
                m_thread.join();

            if (m_file.is_open())
                m_file.flush();
            else
                std::cout.flush();
        }

        void append(const std::string& line)
        {
            boost::asio::post(m_io, [this, line]()
            {
                if (m_file.is_open())
                    m_file << line << std::endl;
                else
                    std::cout << line << std::endl;
            });
        }
    };

    class logger
    {
        std::shared_ptr<sink> m_sink;
        severity m_scope = severity::none;
        std::string m_path;
        std::mutex m_mutex;

    public:

        void set(severity scope, const std::string& path) noexcept(false)
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (scope == severity::none)
                m_sink.reset();
            else if (path != m_path || !m_sink)
                m_sink = std::make_shared<sink>(path);

            if (m_sink && (path != m_path || scope != m_scope))
            {
                std::stringstream line;
                line << "LOGGER: " <<  boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::local_time()) << " " << scope << " " << getpid();
                m_sink->append(line.str());
            }

            m_scope = scope;
            m_path = path;
        }

        void put(const std::string& line)
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_sink)
                m_sink->append(line);
        }

        severity scope() noexcept(true) 
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_scope;
        }
    }
    g_log;
}

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

void line::start(std::stringstream& out, severity level, const char* func, const char* file, int line)
{
    auto scope = g_log.scope();
    if (level > scope)
    {
        out.std::ostream::rdbuf(nullptr);
    }
    else 
    {
        out << "[" << gettid() << "] " << boost::posix_time::microsec_clock::local_time() << " " << level << ": ";
        if (scope == severity::trace)
        {
            auto name = std::filesystem::path(file).filename().string();
            out << "[" << func << " at " << name << ":" << line << "] ";
        }
    }
}

void line::flush(std::stringstream& out)
{
    if (out.std::ostream::rdbuf())
    {
        g_log.put(out.str());
    }
}

void set(severity scope, const std::string& file) noexcept(false)
{
    g_log.set(scope, file);
}

}}
