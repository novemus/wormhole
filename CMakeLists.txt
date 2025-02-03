message("***** wormhole ***** ")

########## setup ##########

cmake_minimum_required(VERSION 3.21)

project(wormhole VERSION 1.2.2 DESCRIPTION "The tool for tunneling TCP streams via UDP" LANGUAGES CXX)

option(WORMHOLE_USE_SHARED_LIB "Build shared library" ${BUILD_SHARED_LIBS})
option(WORMHOLE_SKIP_TEST_RULES "Don't generate test rules" ON)
option(WORMHOLE_SKIP_INSTALL_RULES "Don't generate install rules" OFF)
option(WORMHOLE_SKIP_PACKAGE_RULES "Don't generate package rules" ON)

if (WORMHOLE_USE_SHARED_LIB)
    set(BUILD_SHARED_LIBS ON)
endif()

########## dependencies ##########

if(NOT WORMHOLE_SKIP_TEST_RULES)
    find_package(Boost REQUIRED COMPONENTS system program_options unit_test_framework)
else()
    find_package(Boost REQUIRED COMPONENTS system program_options)
endif()

message("* Boost Version: ${Boost_VERSION}")
message("* Boost Include Dirs: ${Boost_INCLUDE_DIRS}")
message("* Boost Library Dirs: ${Boost_LIBRARY_DIRS}")
message("* Boost Libraries: ${Boost_LIBRARIES}")

find_package(tubus REQUIRED COMPONENTS libtubus)

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

set(EXPORT_FILE export_shared.h)

if(NOT BUILD_SHARED_LIBS)
    set(EXPORT_FILE export_static.h)
endif()

generate_export_header(${LIBWORMHOLE} EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/export_header/wormhole/${EXPORT_FILE})

set(SOURCES ${CMAKE_CURRENT_BINARY_DIR}/export_header/wormhole/${EXPORT_FILE}
    src/wormhole/export.h
    src/wormhole/executor.h
    src/wormhole/logger.h
    src/wormhole/wormhole.h
    src/wormhole/logger.cpp
    src/wormhole/wormhole.cpp
    )

set(HEADERS ${CMAKE_CURRENT_BINARY_DIR}/export_header/wormhole/${EXPORT_FILE}
    src/wormhole/export.h
    src/wormhole/executor.h
    src/wormhole/logger.h
    src/wormhole/wormhole.h
    )

if(MSVC)
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
target_link_libraries(${BINWORMHOLE} PRIVATE wormhole::libwormhole tubus::libtubus Boost::program_options)

target_compile_definitions(${LIBWORMHOLE} PUBLIC "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:LIBWORMHOLE_STATIC_DEFINE>")
target_compile_definitions(${BINWORMHOLE} PUBLIC "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:LIBWORMHOLE_STATIC_DEFINE>")

target_compile_features(${LIBWORMHOLE} PRIVATE cxx_std_17)
target_compile_features(${BINWORMHOLE} PRIVATE cxx_std_17)

target_include_directories(${BINWORMHOLE} PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/export_header>"
        ${Boost_INCLUDE_DIRS})

target_include_directories(${LIBWORMHOLE} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/export_header>"
        PRIVATE ${Boost_INCLUDE_DIRS})

set_target_properties(${BINWORMHOLE} PROPERTIES DEBUG_POSTFIX "d")
set_target_properties(${LIBWORMHOLE} PROPERTIES DEBUG_POSTFIX "d" OUTPUT_NAME ${PROJECT_NAME})
set_target_properties(${LIBWORMHOLE} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})

########## install ##########

if(NOT WORMHOLE_SKIP_INSTALL_RULES AND NOT CMAKE_SKIP_INSTALL_RULES)

    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    if(MSVC)
        set(WORMHOLE_INSTALL_BINDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_BINDIR})
        set(WORMHOLE_INSTALL_LIBDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR})
        set(WORMHOLE_INSTALL_CMAKEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_DATADIR}/cmake)
        set(WORMHOLE_INSTALL_INCLUDEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
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
            set(WORMHOLE_INSTALL_BINDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_BINDIR})
            set(WORMHOLE_INSTALL_LIBDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE})
            set(WORMHOLE_INSTALL_CMAKEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE}/cmake)
            set(WORMHOLE_INSTALL_INCLUDEDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_INCLUDEDIR})
        endif()
    endif()

    configure_package_config_file(cmake/wormhole-config.cmake.in wormhole-config.cmake INSTALL_DESTINATION "${WORMHOLE_INSTALL_CMAKEDIR}")

    write_basic_package_version_file(wormhole-config-version.cmake COMPATIBILITY SameMajorVersion)

    install(TARGETS ${BINWORMHOLE} EXPORT wormhole_export RUNTIME DESTINATION ${WORMHOLE_INSTALL_BINDIR} COMPONENT binary_files)

    install(TARGETS ${LIBWORMHOLE} EXPORT wormhole_export
        RUNTIME DESTINATION ${WORMHOLE_INSTALL_LIBDIR} COMPONENT runtime_files
        LIBRARY DESTINATION ${WORMHOLE_INSTALL_LIBDIR} COMPONENT library_files
        ARCHIVE DESTINATION ${WORMHOLE_INSTALL_LIBDIR} COMPONENT archive_files
        INCLUDES DESTINATION ${WORMHOLE_INSTALL_INCLUDEDIR})

    set(TARGET_FILE wormhole-shared-targets.cmake)

    if(NOT BUILD_SHARED_LIBS)
        set(TARGET_FILE wormhole-static-targets.cmake)
    endif()

    install(EXPORT wormhole_export COMPONENT cmake_files
        FILE ${TARGET_FILE}
        DESTINATION ${WORMHOLE_INSTALL_CMAKEDIR}
        NAMESPACE wormhole::)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/wormhole-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/wormhole-config-version.cmake
        COMPONENT cmake_files
        DESTINATION ${WORMHOLE_INSTALL_CMAKEDIR})

    install(FILES ${HEADERS} DESTINATION ${WORMHOLE_INSTALL_INCLUDEDIR}/${PROJECT_NAME} COMPONENT header_files)

    if(MSVC)
        install(FILES "$<TARGET_PDB_FILE:${BINWORMHOLE}>" COMPONENT pdb_files
            CONFIGURATIONS Debug RelWithDebInfo
            DESTINATION ${WORMHOLE_INSTALL_BINDIR}
            OPTIONAL)
        if(BUILD_SHARED_LIBS)
            install(FILES "$<TARGET_PDB_FILE:${LIBWORMHOLE}>" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION ${WORMHOLE_INSTALL_LIBDIR}
                OPTIONAL)
        else()
            install(FILES "$<TARGET_FILE_DIR:${LIBWORMHOLE}>/$<TARGET_FILE_PREFIX:${LIBWORMHOLE}>$<TARGET_FILE_BASE_NAME:${LIBWORMHOLE}>-static.pdb" COMPONENT pdb_files
                CONFIGURATIONS Debug RelWithDebInfo
                DESTINATION ${WORMHOLE_INSTALL_LIBDIR}
                OPTIONAL)
        endif()
    endif()
endif()

########## tests ##########

if(NOT WORMHOLE_SKIP_TEST_RULES)
    set(WORMHOLE_TEST wormhole_ut)
    add_executable(${WORMHOLE_TEST} tests/logger.cpp tests/executor.cpp tests/wormhole.cpp)
    target_link_libraries(${WORMHOLE_TEST} PRIVATE wormhole::libwormhole tubus::libtubus Boost::unit_test_framework)
    target_compile_definitions(${WORMHOLE_TEST} PUBLIC "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:LIBWORMHOLE_STATIC_DEFINE>")

    target_compile_features(${WORMHOLE_TEST} PRIVATE cxx_std_17)
    set_target_properties(${WORMHOLE_TEST} PROPERTIES DEBUG_POSTFIX "d")

    enable_testing()
    add_test(NAME ${WORMHOLE_TEST} COMMAND ${WORMHOLE_TEST} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

########## package ##########

if(NOT WORMHOLE_SKIP_PACKAGE_RULES AND NOT WORMHOLE_SKIP_INSTALL_RULES AND NOT CMAKE_SKIP_INSTALL_RULES)
    set(DOC_FILES_COMPONENT lib_doc_files)

    if(CMAKE_INSTALL_PREFIX MATCHES "^(/usr)|(/usr/local)$")
        set(BINWORMHOLE_INSTALL_DOCDIR ${CMAKE_INSTALL_DOCDIR}/${BINWORMHOLE}-${${PROJECT_NAME}_VERSION})
        set(LIBWORMHOLE_INSTALL_DOCDIR ${CMAKE_INSTALL_DOCDIR}/${LIBWORMHOLE}-${${PROJECT_NAME}_VERSION})
    else()
        set(BINWORMHOLE_INSTALL_DOCDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_DATADIR}/doc/${BINWORMHOLE})
        set(LIBWORMHOLE_INSTALL_DOCDIR ${PROJECT_NAME}-${${PROJECT_NAME}_VERSION}/${CMAKE_INSTALL_DATADIR}/doc/${LIBWORMHOLE})
    endif()

    if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
        cmake_host_system_information(RESULT DISTR QUERY DISTRIB_ID)
        if(DISTR MATCHES "(debian)|(ubuntu)|(kali)|(astra)")
            if(NOT BUILD_SHARED_LIBS)
                set(DOC_FILES_COMPONENT dev_doc_files)
                set(LIBWORMHOLE_INSTALL_DOCDIR "${LIBWORMHOLE_INSTALL_DOCDIR}-dev")
            endif()
            add_custom_command(DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog"
                COMMAND gzip -cn9 "${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog" > "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz"
                OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz")
            add_custom_target(changelog ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz")
            
            set(DEBIAN_DOC_FILES
                ${CMAKE_CURRENT_BINARY_DIR}/changelog.gz
                ${CMAKE_CURRENT_SOURCE_DIR}/debian/copyright
                ${CMAKE_CURRENT_SOURCE_DIR}/debian/README.Debian
                )
            install(FILES ${DEBIAN_DOC_FILES} DESTINATION ${LIBWORMHOLE_INSTALL_DOCDIR} COMPONENT ${DOC_FILES_COMPONENT})
            install(FILES ${DEBIAN_DOC_FILES} DESTINATION ${BINWORMHOLE_INSTALL_DOCDIR} COMPONENT bin_doc_files)
        endif()
    endif()

    if(NOT BUILD_SHARED_LIBS)
        set(SOURCE_DOC_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/README.md
            ${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md
            )
        install(FILES ${SOURCE_DOC_FILES} DESTINATION ${LIBWORMHOLE_INSTALL_DOCDIR} COMPONENT ${DOC_FILES_COMPONENT})
    endif()

    include(CPack)
endif()

message("***** wormhole done ***** ")
