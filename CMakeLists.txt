message("***** wormhole ***** ")

########## setup ##########

cmake_minimum_required(VERSION 3.21)

project(wormhole VERSION 1.2.6 DESCRIPTION "The tool for tunneling TCP streams via UDP" LANGUAGES CXX)

option(WORMHOLE_USE_SHARED_LIB "Build shared library" ${BUILD_SHARED_LIBS})
option(WORMHOLE_SKIP_TEST_RULES "Don't generate test rules" ON)
option(WORMHOLE_SKIP_INSTALL_RULES "Don't generate install rules" OFF)
option(WORMHOLE_SKIP_PACKAGE_RULES "Don't generate package rules" ON)

if (WORMHOLE_USE_SHARED_LIB)
    set(BUILD_SHARED_LIBS ON)
endif()

########## dependencies ##########

if (NOT MSVC AND NOT BUILD_SHARED_LIBS)
    set(Boost_USE_STATIC_LIBS ON)
endif()

if(NOT WORMHOLE_SKIP_TEST_RULES)
    find_package(Boost 1.74 CONFIG REQUIRED COMPONENTS system program_options unit_test_framework)
else()
    find_package(Boost 1.74 CONFIG REQUIRED COMPONENTS system program_options)
endif()

message("* Boost Version: ${Boost_VERSION}")
message("* Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message("* Boost Library Dirs: ${Boost_LIBRARY_DIRS}")
message("* Boost Libraries: ${Boost_LIBRARIES}")

find_package(tubus 1.2.5 REQUIRED COMPONENTS libtubus)

message("* tubus Version: ${tubus_VERSION}")
message("* tubus Include Dirs: ${tubus_INCLUDE_DIRS}")
message("* tubus Library: ${tubus_LIBRARY}")

########## build ##########

include(GenerateExportHeader)

set(BINWORMHOLE ${PROJECT_NAME})
set(LIBWORMHOLE lib${PROJECT_NAME})

add_executable(${BINWORMHOLE})
add_executable(${PROJECT_NAME}::${BINWORMHOLE} ALIAS ${BINWORMHOLE})

add_library(${LIBWORMHOLE})
add_library(${PROJECT_NAME}::${LIBWORMHOLE} ALIAS ${LIBWORMHOLE})

generate_export_header(${LIBWORMHOLE} EXPORT_FILE_NAME export_header/wormhole/export.h)

if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${LIBWORMHOLE} PUBLIC LIBWORMHOLE_STATIC_DEFINE)
endif()

set(SOURCES "${CMAKE_CURRENT_BINARY_DIR}/export_header/wormhole/export.h"
    src/wormhole/logger.h
    src/wormhole/wormhole.h
    src/wormhole/logger.cpp
    src/wormhole/wormhole.cpp
    )

set(HEADERS "${CMAKE_CURRENT_BINARY_DIR}/export_header/wormhole/export.h"
    src/wormhole/logger.h
    src/wormhole/wormhole.h
    )

if(MSVC)
    set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
    set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
    set(CMAKE_STATIC_LIBRARY_SUFFIX "-static.lib")
    add_definitions(-D_WIN32_WINNT=0x0601)
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

target_sources(${LIBWORMHOLE} PRIVATE ${SOURCES})
target_sources(${BINWORMHOLE} PRIVATE src/wormhole/main.cpp)

target_link_libraries(${LIBWORMHOLE} PRIVATE tubus::libtubus)
target_link_libraries(${BINWORMHOLE} PRIVATE $<TARGET_OBJECTS:${LIBWORMHOLE}> tubus::libtubus Boost::program_options)

target_compile_features(${LIBWORMHOLE} PRIVATE cxx_std_17)
target_compile_features(${BINWORMHOLE} PRIVATE cxx_std_17)

target_include_directories(${BINWORMHOLE} PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/export_header>"
        "${Boost_INCLUDE_DIRS}")

target_include_directories(${LIBWORMHOLE} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/export_header>"
        PRIVATE "${Boost_INCLUDE_DIRS}")

set_target_properties(${BINWORMHOLE} PROPERTIES DEBUG_POSTFIX "d" COMPILE_FLAGS -DLIBWORMHOLE_STATIC_DEFINE)
set_target_properties(${BINWORMHOLE} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
set_target_properties(${LIBWORMHOLE} PROPERTIES DEBUG_POSTFIX "d" OUTPUT_NAME ${PROJECT_NAME} IMPORT_PREFIX "lib")
set_target_properties(${LIBWORMHOLE} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})
set_target_properties(${LIBWORMHOLE} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

########## install ##########

if(NOT WORMHOLE_SKIP_INSTALL_RULES AND NOT CMAKE_SKIP_INSTALL_RULES)

    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(WORMHOLE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

    configure_package_config_file(cmake/wormhole-config.cmake.in wormhole-config.cmake INSTALL_DESTINATION "${WORMHOLE_INSTALL_CMAKEDIR}")

    write_basic_package_version_file(wormhole-config-version.cmake COMPATIBILITY SameMajorVersion)

    install(TARGETS ${BINWORMHOLE} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT binary_files)

    install(TARGETS ${LIBWORMHOLE} EXPORT wormhole_export
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT runtime_files
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT library_files
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT archive_files
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    set(TARGET_FILE wormhole-shared-targets.cmake)

    if(NOT BUILD_SHARED_LIBS)
        set(TARGET_FILE wormhole-static-targets.cmake)
    endif()

    install(EXPORT wormhole_export COMPONENT cmake_files
        FILE ${TARGET_FILE}
        DESTINATION "${WORMHOLE_INSTALL_CMAKEDIR}"
        NAMESPACE wormhole::)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/wormhole-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/wormhole-config-version.cmake"
        COMPONENT cmake_files
        DESTINATION "${WORMHOLE_INSTALL_CMAKEDIR}")

    install(FILES ${HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}" COMPONENT header_files)

    if(MSVC)
        install(FILES "$<TARGET_PDB_FILE:${BINWORMHOLE}>" COMPONENT pdb_files
            CONFIGURATIONS Debug RelWithDebInfo
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            OPTIONAL)
        if(BUILD_SHARED_LIBS)
            install(FILES "$<TARGET_PDB_FILE:${LIBWORMHOLE}>" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                OPTIONAL)
        else()
            install(FILES "$<TARGET_FILE_DIR:${LIBWORMHOLE}>/$<TARGET_FILE_PREFIX:${LIBWORMHOLE}>$<TARGET_FILE_BASE_NAME:${LIBWORMHOLE}>-static.pdb" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                OPTIONAL)
        endif()
    endif()
endif()

########## tests ##########

if(NOT WORMHOLE_SKIP_TEST_RULES)
    set(WORMHOLE_TEST wormhole_ut)

    add_executable(${WORMHOLE_TEST} tests/logger.cpp tests/wormhole.cpp)
    target_link_libraries(${WORMHOLE_TEST} PRIVATE $<TARGET_OBJECTS:${LIBWORMHOLE}> tubus::libtubus Boost::unit_test_framework)

    target_include_directories(${WORMHOLE_TEST} PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>" "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/export_header>")
    target_compile_features(${WORMHOLE_TEST} PRIVATE cxx_std_17)
    set_target_properties(${WORMHOLE_TEST} PROPERTIES DEBUG_POSTFIX "d" COMPILE_FLAGS -DLIBWORMHOLE_STATIC_DEFINE)

    enable_testing()
    add_test(NAME ${WORMHOLE_TEST} COMMAND ${WORMHOLE_TEST} WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()

########## package ##########

if(NOT WORMHOLE_SKIP_PACKAGE_RULES AND NOT WORMHOLE_SKIP_INSTALL_RULES AND NOT CMAKE_SKIP_INSTALL_RULES)
    if(NOT CPACK_PACKAGE_RELEASE)
        set(CPACK_PACKAGE_RELEASE "1")
    endif()

    set(DEVEL_DOC_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
        )

    if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
        set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)
        cmake_host_system_information(RESULT DISTR QUERY DISTRIB_ID)
        if (DISTR MATCHES "(debian)|(ubuntu)|(kali)|(astra)")
            cmake_host_system_information(RESULT DISTR_CODENAME QUERY DISTRIB_VERSION_CODENAME)
            set(CPACK_DEBIAN_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}+${DISTR_CODENAME}")
            set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
            set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
            set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
            set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)
            set(CPACK_DEB_COMPONENT_INSTALL ON)

            set(DEBIAN_DOC_FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/linux/copyright"
                "${CMAKE_CURRENT_SOURCE_DIR}/linux/README.Debian"
                )
            install(FILES ${DEBIAN_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${BINWORMHOLE}" COMPONENT bin_deb_files)
            if(BUILD_SHARED_LIBS)
                install(FILES ${DEBIAN_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBWORMHOLE}" COMPONENT lib_deb_files)
            else()
                install(FILES ${DEBIAN_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBWORMHOLE}-dev" COMPONENT dev_deb_files)
                install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBWORMHOLE}-dev" COMPONENT dev_doc_files)
            endif()
        else()
            cmake_host_system_information(RESULT DISTR_VERSION QUERY DISTRIB_VERSION_ID)
            if (DISTR MATCHES "fedora")
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}.fc${DISTR_VERSION}")
            elseif(DISTRO_ID MATCHES "rhel|centos|almalinux|rocky")
                string(REGEX REPLACE "\\." "" DISTR_VERSION "${DISTR_VERSION}")
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}.el${DISTR_VERSION}")
            else()
                set(CPACK_RPM_PACKAGE_RELEASE "${CPACK_PACKAGE_RELEASE}")
            endif()

            if(NOT BUILD_SHARED_LIBS)
                install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBWORMHOLE}-devel" COMPONENT dev_doc_files)
            endif()

            set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
            set(CPACK_RPM_PACKAGE_AUTOREQ "1")
            set(CPACK_RPM_PACKAGE_LICENSE "Apache 2.0")
            set(CPACK_RPM_COMPONENT_INSTALL ON)
            set(CPACK_RPM_BUILD_SOURCE_DIRS_PREFIX "/src/wormhole")
        endif()
    elseif(NOT BUILD_SHARED_LIBS)
        install(FILES ${DEVEL_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${LIBWORMHOLE}" COMPONENT dev_doc_files)
    endif()

    string(TOLOWER "${PROJECT_NAME}-${PROJECT_VERSION}-${CPACK_PACKAGE_RELEASE}-${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}" CPACK_ARCHIVE_FILE_NAME)

    include(CPack)
endif()
