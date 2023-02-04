cmake_minimum_required(VERSION 3.10)

project(wormhole)
project(${PROJECT_NAME} VERSION 1.0)

set(TESTS tests)

set(SOURCES buffer.cpp reactor.cpp tubus.cpp wormhole.cpp main.cpp)
set(TESTS_SOURCES buffer.cpp reactor.cpp tubus.cpp wormhole.cpp tests/buffer.cpp tests/packet.cpp tests/tubus.cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall")

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_definitions(
        -D_CRT_NO_VA_START_VALIDATION
        -D_CRT_SECURE_NO_WARNINGS
        -D_WIN32_WINNT=0x0600
        -D_WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()

find_package(Boost COMPONENTS system thread log log_setup program_options unit_test_framework REQUIRED)

message(status "** Boost Version: ${Boost_VERSION}")
message(status "** Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message(status "** Boost Librariy Dirs: ${Boost_LIBRARY_DIRS}")
message(status "** Boost Libraries: ${Boost_LIBRARIES}")

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

add_executable(${PROJECT_NAME} ${SOURCES})
add_executable(${TESTS} ${TESTS_SOURCES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES})
    target_link_libraries(${TESTS} Threads::Threads)
    target_link_libraries(${TESTS} ${Boost_LIBRARIES})
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

enable_testing()
add_test(NAME ${TESTS} COMMAND ${TESTS} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
