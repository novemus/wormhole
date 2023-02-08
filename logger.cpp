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
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/shared_ptr.hpp>
#include <fstream>
#include <ostream>

namespace novemus::logger {

void set(const std::string& file, boost::log::trivial::severity_level level)
{
    boost::log::add_common_attributes();

    auto sink = boost::make_shared<boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>>();

    if (!file.empty())
        sink->locked_backend()->add_stream(boost::shared_ptr<std::ostream>(new std::ofstream(file.c_str())));

    sink->locked_backend()->add_stream(boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter()));

    sink->set_formatter([](const boost::log::record_view& view, boost::log::formatting_ostream& strm)
    {
        strm << view[boost::log::trivial::severity] << " "
             << boost::log::extract<boost::posix_time::ptime>("TimeStamp", view)
             << " " << view[boost::log::expressions::message];
    });

    boost::log::core::get()->set_filter(boost::log::trivial::severity >= level);
    boost::log::core::get()->add_sink(sink);
}

}