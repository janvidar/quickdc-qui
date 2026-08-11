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

    # Named rather than left to the SDK, which would otherwise stamp whichever
    # version the machine that built it happened to have and refuse to run on
    # anything older. 11.0 is Qt 6.5's own floor, so there is nothing to gain
    # by going lower. Set before any target is defined, and cached so that a
    # packager can raise it.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "Oldest macOS this build will run on" FORCE)
    endif()
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

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Identity
# ---------------------------------------------------------------------------

# One string for every platform: the macOS bundle identifier, the basenames of
# the desktop entry, the AppStream metainfo and the icon, and the Flatpak
# application id. main.cpp is given it as a compile definition too, so the
# app_id a window announces to a Wayland compositor cannot drift from the name
# of the file that describes it - which is how a window ends up in a taskbar
# with no icon and no way back to the launcher that started it.
set(QUICKDC_QUI_APP_ID "org.extatic.QuickDC" CACHE STRING
    "Reverse-DNS application identifier")

# Desktop integration is a Linux notion. macOS has the bundle built beside it in
# src/app, and Windows wants none of this, so neither installs anything unless
# somebody asks.
if(UNIX AND NOT APPLE)
    set(QUICKDC_QUI_INSTALL_DEFAULT ON)
else()
    set(QUICKDC_QUI_INSTALL_DEFAULT OFF)
endif()

option(QUICKDC_QUI_INSTALL
    "Install the application, its desktop entry, icon and metainfo"
    ${QUICKDC_QUI_INSTALL_DEFAULT})

# ---------------------------------------------------------------------------
# The version a package advertises
#
# Not always PROJECT_VERSION: a nightly is built from a commit between releases
# and has to say which one, or every build of a fortnight claims to be 0.1.0 and
# a bug report cannot be tied to any of them.
#
# Three sources, in order. packaging/nightly-version.txt is written by
# packaging/flatpak/build.sh --nightly and is not tracked; it comes first
# because a Flatpak build is a copy of this tree with no way to pass a cache
# variable in - flatpak-builder has no flag for it - so the version has to
# arrive through a file. Then `git describe`, for a developer build. Then the
# project version alone, for a tarball with neither.
# ---------------------------------------------------------------------------

set(QUICKDC_QUI_VERSION_STRING "" CACHE STRING
    "Version reported in the desktop metadata; empty means derive one")

if(NOT QUICKDC_QUI_VERSION_STRING)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/nightly-version.txt)
        file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/nightly-version.txt
            QUICKDC_QUI_VERSION_STRING LIMIT_COUNT 1)
    else()
        find_package(Git QUIET)
        if(GIT_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
            # --always so that a tagless clone still produces something, which
            # this one is: there is not a tag in the repository yet.
            execute_process(
                COMMAND ${GIT_EXECUTABLE} describe --always --dirty
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE QUICKDC_QUI_GIT_DESCRIBE
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE QUICKDC_QUI_GIT_RESULT)

            if(QUICKDC_QUI_GIT_RESULT EQUAL 0 AND QUICKDC_QUI_GIT_DESCRIBE)
                set(QUICKDC_QUI_VERSION_STRING
                    "${PROJECT_VERSION}+${QUICKDC_QUI_GIT_DESCRIBE}")
            endif()
        endif()
    endif()
endif()

if(NOT QUICKDC_QUI_VERSION_STRING)
    set(QUICKDC_QUI_VERSION_STRING "${PROJECT_VERSION}")
endif()

# AppStream will not validate a release without a date on it. TIMESTAMP honours
# SOURCE_DATE_EPOCH when it is set, so a reproducible build gets the date of the
# source rather than the date it happened to be built.
string(TIMESTAMP QUICKDC_QUI_RELEASE_DATE "%Y-%m-%d" UTC)

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

# The desktop entry, the icon and the AppStream metainfo. Not under src, because
# none of it is compiled and none of it is reached from the application at run
# time - the desktop shell reads all three, and only after they are installed.
if(QUICKDC_QUI_INSTALL)
    add_subdirectory(packaging/linux)
endif()

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
message(STATUS "  Application id  : ${QUICKDC_QUI_APP_ID}")
message(STATUS "  Version string  : ${QUICKDC_QUI_VERSION_STRING}")
message(STATUS "  Build tests     : ${QUICKDC_QUI_BUILD_TESTS}")
message(STATUS "  Install         : ${QUICKDC_QUI_INSTALL}")
message(STATUS "")
