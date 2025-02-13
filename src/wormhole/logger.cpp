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
 #include <future>
 #include <memory>
 #include <filesystem>
 #include <sys/types.h>
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
#define dup(handle) _dup(handle)
#define dup2(src, dst) _dup2(src, dst)
#define fileno(file) _fileno(file)
#endif

namespace wormhole { namespace log {

namespace
{
    const int STDOUT_FD = dup(1);
    const int STDERR_FD = dup(2);

    class sink
    {
        using executor_work_guard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

        std::string m_path;
        FILE* m_file = nullptr;
        severity m_scope = severity::none;
        boost::asio::io_context m_io;
        executor_work_guard m_work;
        std::thread m_thread;
        std::mutex m_mutex;

    public:

        sink() : m_work(boost::asio::make_work_guard(m_io))
        {
            m_thread = std::thread([&]()
            {
                boost::system::error_code ec;
                m_io.run(ec);
                if (ec)
                    std::cout << "LOGGER: " << ec.message() << std::endl;
            });
        }

        ~sink()
        {
            m_work.reset();
            m_io.stop();
            if (m_thread.joinable())
                m_thread.join();
        }

        void set(severity scope, const std::string& path) noexcept(false)
        {
            // wait for writing pended lines
            auto promise = std::make_shared<std::promise<void>>();
            m_io.post([promise]()
            {
                promise->set_value();
            });
            promise->get_future().wait();

            fflush(stdout);
            fflush(stderr);

            std::lock_guard<std::mutex> lock(m_mutex);

            if (scope == m_scope && path == m_path)
                return;

            m_scope = scope;

            if (!path.empty())
            {
                if (path != m_path)
                {
                    m_path = path;

                    if (m_file)
                    {
                        fclose(m_file);
                        m_file = nullptr;
                    }

                    auto file = std::regex_replace(path, std::regex("%p"), std::to_string(getpid()));

                    m_file = fopen(file.c_str(), "a");
                    if(!m_file)
                        throw std::runtime_error("can't open log file");

                    dup2(fileno(m_file), 1);
                    dup2(fileno(m_file), 2);
                }
            }
            else if (m_file)
            {
                fclose(m_file);

                m_path.clear();
                m_file = nullptr;

                dup2(STDOUT_FD, 1);
                dup2(STDERR_FD, 2);
            }
            
            std::cout << "LOGGER: " <<  boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::local_time()) << " " << m_scope << " " << getpid() << std::endl;
        }

        void put(const std::string& line)
        {
            m_io.post([this, line]()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                std::cout << line << std::endl;
            });
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
        if (scope > severity::info)
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
