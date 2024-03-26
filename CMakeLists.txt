message("***** wormhole *****")

cmake_minimum_required(VERSION 3.10)

project(wormhole)
project(${PROJECT_NAME} VERSION 1.1)

set(Boost_USE_STATIC_LIBS ON)

add_subdirectory(tubus)

include(GNUInstallDirs)

add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_definitions(
        -D_CRT_NO_VA_START_VALIDATION
        -D_CRT_SECURE_NO_WARNINGS
        -D_WIN32_WINNT=0x0600
        -D_WINSOCK_DEPRECATED_NO_WARNINGS
        -DINCL_EXTRA_HTON_FUNCTIONS
    )
endif()

if(BUILD_TESTING)
    find_package(Boost REQUIRED COMPONENTS system thread filesystem program_options unit_test_framework)
else()
    find_package(Boost REQUIRED COMPONENTS system thread filesystem program_options)
endif()

message(status "* Boost Version: ${Boost_VERSION}")
message(status "* Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message(status "* Boost Librariy Dirs: ${Boost_LIBRARY_DIRS}")
message(status "* Boost Libraries: ${Boost_LIBRARIES}")

include_directories(tubus)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

set(WORMHOLE_OBJECT ${PROJECT_NAME}_object)

if(NOT PROJECT_IS_TOP_LEVEL)
    set(TUBUS_OBJECT ${TUBUS_OBJECT} PARENT_SCOPE)
    set(WORMHOLE_OBJECT ${WORMHOLE_OBJECT} PARENT_SCOPE)
endif()

add_library(${WORMHOLE_OBJECT} OBJECT reactor.cpp logger.cpp wormhole.cpp)

if(NOT WORMHOLE_OBJECT_ONLY)
    add_executable(${PROJECT_NAME} main.cpp)
    target_link_libraries(${PROJECT_NAME} ${TUBUS_OBJECT} ${WORMHOLE_OBJECT})

    set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "d")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -fPIC")
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
        target_link_libraries(${PROJECT_NAME} Threads::Threads Boost::system Boost::thread Boost::filesystem Boost::program_options)
    endif()

    if(APPLE AND Boost_USE_STATIC_LIBS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_BINDIR})
        install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> CONFIGURATIONS "Debug" "RelWithDebInfo" DESTINATION ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_BINDIR})
    elseif(CMAKE_INSTALL_PREFIX MATCHES "^(/usr)|(/usr/local)$")
        install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${PROJECT_NAME}/${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_BINDIR})
    endif()

    if(BUILD_TESTING)
        set(TESTS_NAME wormhole_ut)
        add_executable(${TESTS_NAME} tests/logger.cpp tests/reactor.cpp tests/wormhole.cpp)
        target_link_libraries(${TESTS_NAME} ${TUBUS_OBJECT} ${WORMHOLE_OBJECT})

        set_target_properties(${TESTS_NAME} PROPERTIES DEBUG_POSTFIX "d")

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_link_libraries(${TESTS_NAME} Threads::Threads Boost::system Boost::thread Boost::filesystem Boost::unit_test_framework)
        endif()

        enable_testing()
        add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    endif()
endif()

message("***** wormhole done *****")
