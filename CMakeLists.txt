message("***** wormhole *****")

cmake_minimum_required(VERSION 3.10)

project(wormhole)
project(${PROJECT_NAME} VERSION 1.1)

set(Boost_USE_STATIC_LIBS ON)

add_subdirectory(tubus)

include(GNUInstallDirs)

set(SOURCES reactor.cpp logger.cpp wormhole.cpp main.cpp)

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

add_executable(${PROJECT_NAME} ${SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "d")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    target_link_libraries(${PROJECT_NAME} Boost::system Boost::thread Boost::filesystem Boost::program_options)
endif()

if(APPLE AND Boost_USE_STATIC_LIBS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

target_link_libraries(${PROJECT_NAME} ${TUBUS_STATIC_TARGET})

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
    set(TESTS_SOURCES reactor.cpp logger.cpp tests/logger.cpp tests/reactor.cpp)

    add_executable(${TESTS_NAME} ${TESTS_SOURCES})
    set_target_properties(${TESTS_NAME} PROPERTIES DEBUG_POSTFIX "d")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_libraries(${TESTS_NAME} Threads::Threads)
        target_link_libraries(${TESTS_NAME} Boost::system Boost::thread Boost::filesystem Boost::unit_test_framework)
    endif()

    enable_testing()
    add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

message("***** wormhole done *****")
