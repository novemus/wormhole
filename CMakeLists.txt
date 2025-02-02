message("***** wormhole ***** ")

########## setup ##########

cmake_minimum_required(VERSION 3.21)

project(wormhole VERSION 1.2.2 DESCRIPTION "The tool for tunneling TCP streams via UDP" LANGUAGES CXX)

option(WORMHOLE_USE_SHARED_LIBS "Build shared library" ${BUILD_SHARED_LIBS})
option(WORMHOLE_SKIP_TEST_RULES "Don't generate test rules" ON)
option(WORMHOLE_SKIP_INSTALL_RULES "Don't generate install rules" OFF)
option(WORMHOLE_SKIP_PACKAGE_RULES "Don't generate package rules" ON)

if (WORMHOLE_USE_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ON)
endif()

########## dependencies ##########

if(NOT WORMHOLE_SKIP_TEST_RULES)
    find_package(OpenSSL REQUIRED)

    message("* OpenSSL Version: ${OPENSSL_VERSION}")
    message("* OpenSSL Include Dir: ${OPENSSL_INCLUDE_DIR}")
    message("* OpenSSL Libraries: ${OPENSSL_LIBRARIES}")

    find_package(Boost REQUIRED COMPONENTS system program_options unit_test_framework)
else()
    find_package(Boost REQUIRED COMPONENTS system program_options)
endif()

message("* Boost Version: ${Boost_VERSION}")
message("* Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message("* Boost Library Dirs: ${Boost_LIBRARY_DIRS}")
message("* Boost Libraries: ${Boost_LIBRARIES}")

include_directories(tubus)

########## build ##########

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)

set(WORMHOLE_EXE ${PROJECT_NAME}_exe)
set(WORMHOLE_LIB ${PROJECT_NAME}_lib)

add_executable(${WORMHOLE_EXE})
add_executable(${PROJECT_NAME}::${WORMHOLE_EXE} ALIAS ${WORMHOLE_EXE})

add_library(${WORMHOLE_LIB})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${WORMHOLE_LIB})

set(EXPORT_FILE "export_shared.h")

if(NOT BUILD_SHARED_LIBS)
    set(EXPORT_FILE "export_static.h")
endif()

generate_export_header(${WORMHOLE_LIB} EXPORT_FILE_NAME ${PROJECT_NAME}/${EXPORT_FILE})

if(MSVC)
    set(WORMHOLE_INSTALL_BINDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_BINDIR})
    set(WORMHOLE_INSTALL_LIBDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR})
    set(WORMHOLE_INSTALL_CMAKEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_DATADIR}/cmake)
    set(WORMHOLE_INSTALL_INCLUDEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_INCLUDEDIR})
    add_definitions(-D_WIN32_WINNT=0x0601 -DINCL_EXTRA_HTON_FUNCTIONS)
else()
    if(CMAKE_INSTALL_PREFIX MATCHES "^(/usr)|(/usr/local)$")
        set(WORMHOLE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
        if(APPLE)
            set(WORMHOLE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
            set(WORMHOLE_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake)
            set(WORMHOLE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
        elseif(CMAKE_INSTALL_LIBDIR MATCHES ${CMAKE_LIBRARY_ARCHITECTURE} OR CMAKE_INSTALL_PREFIX MATCHES "/usr/local")
            set(WORMHOLE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
            set(WORMHOLE_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
            set(WORMHOLE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
        else()
            set(WORMHOLE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE}/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
            set(WORMHOLE_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
            set(WORMHOLE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}-${${PROJECT_NAME}_VERSION})
        endif()
    else()
        set(WORMHOLE_INSTALL_BINDIR ${PROJECT_NAME}/${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_BINDIR})
        set(WORMHOLE_INSTALL_LIBDIR ${PROJECT_NAME}/${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE})
        set(WORMHOLE_INSTALL_CMAKEDIR ${PROJECT_NAME}/${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE}/cmake)
        set(WORMHOLE_INSTALL_INCLUDEDIR ${PROJECT_NAME}/${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endif()

if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

if(NOT DEFINED CMAKE_CXX_VISIBILITY_PRESET)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
endif()
if(NOT DEFINED CMAKE_VISIBILITY_INLINES_HIDDEN)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

set_target_properties(${WORMHOLE_EXE} PROPERTIES DEBUG_POSTFIX "d")
set_target_properties(${WORMHOLE_LIB} PROPERTIES DEBUG_POSTFIX "d")

set(HEADERS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}/${EXPORT_FILE}
    src/wormhole/executor.h
    src/wormhole/logger.h
    src/wormhole/wormhole.h
    )

target_sources(${WORMHOLE_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}/${EXPORT_FILE}
    src/wormhole/executor.h
    src/wormhole/logger.h
    src/wormhole/wormhole.h
    src/wormhole/logger.cpp
    src/wormhole/wormhole.cpp)

target_sources(${WORMHOLE_EXE} PRIVATE "$<TARGET_OBJECTS:${WORMHOLE_LIB}>" src/wormhole/main.cpp)

target_link_libraries(${WORMHOLE_EXE} PRIVATE tubus::tubus Boost::program_options)
target_link_libraries(${WORMHOLE_LIB} PRIVATE tubus::tubus)

if(NOT MSVC)
    target_link_libraries(${WORMHOLE_EXE} PRIVATE Boost::system)
    target_link_libraries(${WORMHOLE_LIB} PRIVATE Boost::system)
endif()

target_compile_definitions(${WORMHOLE_LIB} PUBLIC "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:WORMHOLE_STATIC_DEFINE>")

target_include_directories(${WORMHOLE_EXE} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tubus/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        PRIVATE ${Boost_INCLUDE_DIRS})

target_include_directories(${WORMHOLE_LIB} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tubus/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        PRIVATE ${Boost_INCLUDE_DIRS})

set_target_properties(${WORMHOLE_LIB} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})

########## install ##########

if(NOT WORMHOLE_SKIP_INSTALL_RULES AND NOT CMAKE_SKIP_INSTALL_RULES)
    configure_package_config_file(cmake/wormhole-config.cmake.in wormhole-config.cmake INSTALL_DESTINATION "${WORMHOLE_INSTALL_CMAKEDIR}")

    write_basic_package_version_file(wormhole-config-version.cmake COMPATIBILITY SameMajorVersion)

    install(TARGETS ${WORMHOLE_EXE} EXPORT wormhole_export RUNTIME DESTINATION ${WORMHOLE_INSTALL_BINDIR} COMPONENT runtime_files)

    install(TARGETS ${WORMHOLE_LIB} EXPORT wormhole_export
        RUNTIME DESTINATION ${WORMHOLE_INSTALL_LIBDIR} COMPONENT runtime_files
        LIBRARY DESTINATION ${WORMHOLE_INSTALL_LIBDIR} COMPONENT library_files
        ARCHIVE DESTINATION ${WORMHOLE_INSTALL_LIBDIR} COMPONENT archive_files
        INCLUDES DESTINATION ${WORMHOLE_INSTALL_INCLUDEDIR})

    set(TARGET_FILE "wormhole-shared-targets.cmake")

    if(NOT BUILD_SHARED_LIBS)
        set(TARGET_FILE "wormhole-static-targets.cmake")
    endif()

    install(EXPORT wormhole_export COMPONENT cmake_files
        FILE "${TARGET_FILE}"
        DESTINATION "${WORMHOLE_INSTALL_CMAKEDIR}"
        NAMESPACE wormhole::)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/wormhole-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/wormhole-config-version.cmake"
        COMPONENT cmake_files
        DESTINATION "${WORMHOLE_INSTALL_CMAKEDIR}")

    install(FILES ${HEADERS} DESTINATION ${WORMHOLE_INSTALL_INCLUDEDIR} COMPONENT header_files)

    if(MSVC)
        install(FILES "$<TARGET_PDB_FILE:${WORMHOLE_EXE}>" COMPONENT pdb_files
            CONFIGURATIONS Debug RelWithDebInfo
            DESTINATION ${WORMHOLE_INSTALL_BINDIR}
            OPTIONAL)
        if(BUILD_SHARED_LIBS)
            install(FILES "$<TARGET_PDB_FILE:${WORMHOLE_LIB}>" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION "${WORMHOLE_INSTALL_LIBDIR}"
                OPTIONAL)
        else()
            install(FILES "$<TARGET_FILE_DIR:${WORMHOLE_LIB}>/$<TARGET_FILE_PREFIX:${WORMHOLE_LIB}>$<TARGET_FILE_BASE_NAME:${WORMHOLE_LIB}>-static.pdb" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION "${WORMHOLE_INSTALL_LIBDIR}"
                OPTIONAL)
        endif()
    endif()
endif()

########## tests ##########

if(NOT WORMHOLE_SKIP_TEST_RULES)
    set(WORMHOLE_TEST wormhole_ut)
    add_executable(${WORMHOLE_TEST} tests/logger.cpp tests/executor.cpp tests/wormhole.cpp)
    target_link_libraries(${WORMHOLE_TEST} PRIVATE wormhole::wormhole tubus::tubus Boost::unit_test_framework)

    set_target_properties(${WORMHOLE_TEST} PROPERTIES DEBUG_POSTFIX "d")

    if(NOT MSVC))
        target_link_libraries(${WORMHOLE_TEST} PRIVATE Boost::system)
    endif()

    enable_testing()
    add_test(NAME ${WORMHOLE_TEST} COMMAND ${WORMHOLE_TEST} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
endif()

########## package ##########

if(NOT WORMHOLE_SKIP_PACKAGE_RULES AND NOT WORMHOLE_SKIP_INSTALL_RULES AND NOT CMAKE_SKIP_INSTALL_RULES)
    if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
        cmake_host_system_information(RESULT BUILD_DISTRIB_ID QUERY DISTRIB_ID)
        if (BUILD_DISTRIB_ID MATCHES "(debian)|(ubuntu)|(kali)|(astra)")
            include("cmake/debian-cpack.cmake")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
        include("cmake/windows-cpack.cmake")
    endif()
endif()
