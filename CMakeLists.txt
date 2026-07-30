#
## CMake build for the QuickDC Qt user interface
## Copyright (c) 2026, Jan Vidar Krey
#

cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(quickdc-qui
    VERSION 0.1.0
    DESCRIPTION "Qt6 user interface for QuickDC"
    LANGUAGES CXX)

# Objective-C++, for the one file that has to talk to AppKit: the native title
# bar. Enabled here rather than beside that file, because a language turned on in
# a subdirectory is not set up for the sibling directories.
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

# Set before the core is configured, which forces the same default. Doing it
# here means the value is ours rather than whichever subproject reached it
# first, and Debug is what turns on the core's QDBG/QERR tracing.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)

option(QUICKDC_QUI_BUILD_TESTS "Build the quickdc-qui tests" ON)

# ---------------------------------------------------------------------------
# Qt
#
# Widgets for the view layer in src/gui; the bridge in src/core links Core alone
# and deliberately knows nothing about widgets.
# ---------------------------------------------------------------------------

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

# AUTOMOC is set per target rather than globally: the core builds ~90 source
# files that carry no Q_OBJECT, and scanning them all is wasted work.

# ---------------------------------------------------------------------------
# QuickDC core submodule
# ---------------------------------------------------------------------------

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/quickdc/CMakeLists.txt)
    message(FATAL_ERROR
        "The quickdc submodule is not checked out. Run:\n"
        "    git submodule update --init --recursive")
endif()

# The core's own test programs are ~30 executables that a user interface build
# has no use for. FORCE because this is a cache variable owned by the quickdc
# subproject.
set(QUICKDC_BUILD_TESTS OFF CACHE BOOL "Build the QuickDC test programs" FORCE)

add_subdirectory(quickdc)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

add_subdirectory(src)

if(QUICKDC_QUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "quickdc-qui ${PROJECT_VERSION} configured:")
message(STATUS "  Build type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Qt              : ${Qt6_VERSION} (${Qt6_DIR})")
message(STATUS "  Build tests     : ${QUICKDC_QUI_BUILD_TESTS}")
message(STATUS "")
