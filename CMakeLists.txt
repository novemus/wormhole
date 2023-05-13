message("***** wormhole *****")

cmake_minimum_required(VERSION 3.10)

project(wormhole)
project(${PROJECT_NAME} VERSION 1.0)

set(tubus_SKIP_INSTALL_TARGET ON)
add_subdirectory(tubus)

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

include_directories(tubus)

if(BUILD_TESTING)
    find_package(Boost REQUIRED COMPONENTS system thread filesystem program_options unit_test_framework)
else()
    find_package(Boost REQUIRED COMPONENTS system thread filesystem program_options)
endif()
    
message(status "* Boost Version: ${Boost_VERSION}")
message(status "* Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message(status "* Boost Librariy Dirs: ${Boost_LIBRARY_DIRS}")
message(status "* Boost Libraries: ${Boost_LIBRARIES}")

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

add_executable(${PROJECT_NAME} ${SOURCES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    target_link_libraries(${PROJECT_NAME} Boost::system Boost::thread Boost::filesystem Boost::program_options)
endif()

target_link_libraries(${PROJECT_NAME} tubus)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

if(BUILD_SHARED_LIBS)
    if(WIN32)
        install(FILES "$<TARGET_FILE:tubus>" DESTINATION bin)
    else()
        install(FILES "$<TARGET_FILE:tubus>" DESTINATION lib)
    endif()
endif()

if(BUILD_TESTING)
    set(TESTS_NAME wormhole_ut)
    set(TESTS_SOURCES reactor.cpp logger.cpp tests/logger.cpp tests/reactor.cpp)

    add_executable(${TESTS_NAME} ${TESTS_SOURCES})

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_link_libraries(${TESTS_NAME} Threads::Threads)
        target_link_libraries(${TESTS_NAME} Boost::system Boost::thread Boost::filesystem Boost::unit_test_framework)
    endif()

    enable_testing()
    add_test(NAME ${TESTS_NAME} COMMAND ${TESTS_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

message("***** wormhole done *****")
