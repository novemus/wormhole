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

#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

namespace novemus::logger {

void set(const std::string& file, boost::log::trivial::severity_level level);

}

#define GET_LOGGER(severity) BOOST_LOG_TRIVIAL(severity) << boost::log::add_value("Function", __FUNCTION__) << boost::log::add_value("File", __FILE__) << boost::log::add_value("Line", __LINE__)

#define _trc_ GET_LOGGER(trace)
#define _dbg_ GET_LOGGER(debug)
#define _inf_ GET_LOGGER(info)
#define _wrn_ GET_LOGGER(warning)
#define _err_ GET_LOGGER(error)
#define _ftl_ GET_LOGGER(fatal)
