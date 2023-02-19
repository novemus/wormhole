/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include "../logger.h"
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <regex>

namespace {

std::regex pattern("\\[\\d+\\] \\d{4}-\\w{2,3}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{1,6} INFO: line 1\n"
                   "\\[\\d+\\] \\d{4}-\\w{2,3}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{1,6} WARN: line 2\n"
                   "\\[\\d+\\] \\d{4}-\\w{2,3}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{1,6} ERROR: line 3\n"
                   "\\[\\d+\\] \\d{4}-\\w{2,3}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{1,6} FATAL: line 4\n");
}

BOOST_AUTO_TEST_CASE(stdlog)
{
    novemus::logger::set(novemus::logger::severity::info, false);

    std::stringstream out;
    std::streambuf *coutbuf = std::cout.rdbuf();
    std::cout.rdbuf(out.rdbuf());

    _inf_ << "line " << 1;
    _wrn_ << "line " << 2;
    _err_ << "line " << 3;
    _ftl_ << "line " << 4;
    _dbg_ << "line " << 5;
    _trc_ << "line " << 6;

    std::smatch match;
    std::string text = out.str();
    out.clear();
    std::cout.rdbuf(coutbuf);

    BOOST_CHECK(std::regex_match(text, match, pattern));
}

BOOST_AUTO_TEST_CASE(filelog)
{
    novemus::logger::set(novemus::logger::severity::info, false, "log.txt");

    _inf_ << "line " << 1;
    _wrn_ << "line " << 2;
    _err_ << "line " << 3;
    _ftl_ << "line " << 4;
    _dbg_ << "line " << 5;
    _trc_ << "line " << 6;

    novemus::logger::set(novemus::logger::severity::info);

    std::smatch match;
    std::ifstream file("log.txt");
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    BOOST_CHECK(std::regex_match(text, match, pattern));

    file.close();
    std::remove("log.txt");
}
