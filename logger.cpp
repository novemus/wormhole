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
#include <filesystem>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/attributes/function.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/shared_ptr.hpp>
#include <fstream>
#include <ostream>

#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#elif _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#define gettid() GetCurrentThreadId()
#endif

namespace novemus::logger {

void set(const std::string& file, boost::log::trivial::severity_level level)
{
    boost::log::add_common_attributes();
    
    boost::log::core::get()->add_global_attribute("Thread", boost::log::attributes::make_function(&gettid));

    auto sink = boost::make_shared<boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>>();

    if (!file.empty())
        sink->locked_backend()->add_stream(boost::shared_ptr<std::ostream>(new std::ofstream(file.c_str())));

    sink->locked_backend()->add_stream(boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter()));

    sink->set_formatter([level](const boost::log::record_view& view, boost::log::formatting_ostream& strm)
    {
        strm << "[" << boost::log::extract<int>("Thread", view) << "] "
             << boost::log::extract<boost::posix_time::ptime>("TimeStamp", view) 
             << " " << view[boost::log::trivial::severity] << ": ";
             
             if (level == boost::log::trivial::debug || level == boost::log::trivial::trace)
             {
                auto file = std::filesystem::path(boost::log::extract<std::string>("File", view).get()).filename();

                strm << "[" << boost::log::extract<std::string>("Function", view)
                    << " in " << file.string() << ":" << boost::log::extract<int>("Line", view) << "] ";
             }

             strm << view[boost::log::expressions::message];
    });

    boost::log::core::get()->set_filter(boost::log::trivial::severity >= level);
    boost::log::core::get()->add_sink(sink);
}

}