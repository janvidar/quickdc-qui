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
    # anything older. Qt 6.5's own floor is 11.0, but std::format is lower still
    # than that here: libc++ leaves the floating point half of it in the dylib,
    # and that arrived in 13.3, so anything older fails to compile rather than
    # fails to run. Set before any target is defined, and cached so that a
    # packager can raise it.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING
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
# Widgets for the view layer in src/gui; the bridge in src/core links Core and
# Network, and deliberately knows nothing about widgets. Network is there for one
# thing only - QNetworkInformation, which says when the machine changes network so
# that a reachability check can be run again. None of the client's own traffic
# goes through it; that is samurai's sockets, underneath the core.
# ---------------------------------------------------------------------------

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

# AUTOMOC is set per target rather than globally: the core builds ~90 source
# files that carry no Q_OBJECT, and scanning them all is wasted work.

# ---------------------------------------------------------------------------
# The submodules
#
# Four of them, side by side rather than nested, and added here in the order
# they depend on each other. Each of them looks for what it needs already being
# there before it goes looking for a checkout of its own, so adding them from
# here is what makes this the one place any of them is added.
#
# Nested, exotic was reached for twice - once by QuickDC for its own .tcc suites
# and once by samurai for its - from a directory inside samurai that QuickDC had
# no business knowing the path of. Both would have defined the same targets, so
# the two were kept apart by forcing samurai's tests off, and that is what made
# the core's suite need a build directory of its own.
# ---------------------------------------------------------------------------

foreach(module IN ITEMS exotic samurai libdrop-cpp quickdc)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt)
        message(FATAL_ERROR
            "The ${module} submodule is not checked out. Run:\n"
            "    git submodule update --init")
    endif()
endforeach()

# The nested suites are off by default, because they are some 1160 and 1170 tests
# against this build's 12 and a user interface build has no use for either. No
# FORCE, unlike before: these are the subprojects' own cache variables and a
# -D on the command line has already put its value in the cache by the time this
# runs, so asking for them here is now possible rather than silently undone.
set(QUICKDC_BUILD_TESTS OFF CACHE BOOL "Build the QuickDC test programs")
set(SAMURAI_BUILD_TESTS OFF CACHE BOOL "Build the samurai test programs")
set(DROP_BUILD_TESTS OFF CACHE BOOL "Build the libdrop-cpp test suite")

# And its command line tool, which diagnoses a NAT from a terminal rather than
# from anything this interface does. Nothing here runs it.
set(DROP_BUILD_TOOLS OFF CACHE BOOL "Build the dropcheck command line tool")

# Declared here as well as in QuickDC, because samurai is configured before
# QuickDC is and the two below are forwarded to it - read any earlier than this
# and they would be empty, which is neither of the values they can take. QuickDC's
# own option() calls find these already in the cache and leave them alone, so the
# defaults and the help text stay that project's to state.
option(QUICKDC_TRACE
    "Compile in the QDBG/QNET/QHUB tracing subsystem (writes secrets to disk)" OFF)
option(QUICKDC_SANITIZE
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Samurai's flavour and its two PUBLIC usage requirements, set here because this
# is the build that adds it and they have to be answered before it is configured.
# QuickDC checks that the last two agree with its own rather than setting them,
# since by then samurai has been configured and asking would be too late.
set(SAMURAI_BUILD_STATIC ON  CACHE BOOL "Build the samurai static library" FORCE)
set(SAMURAI_BUILD_SHARED OFF CACHE BOOL "Build the samurai shared library" FORCE)
set(SAMURAI_SANITIZE ${QUICKDC_SANITIZE} CACHE BOOL "Build samurai with sanitizers" FORCE)
set(SAMURAI_TRACE ${QUICKDC_TRACE} CACHE BOOL "Build samurai with tracing" FORCE)

# Before anything that registers a test, so that ctest run from this build
# directory sees every suite that was asked for rather than only this project's.
enable_testing()

# Only where something is going to build tests through it. It is a small C
# library, but a build with no suite enabled has nothing to compile it for.
if(QUICKDC_BUILD_TESTS OR SAMURAI_BUILD_TESTS OR DROP_BUILD_TESTS)
    add_subdirectory(exotic)
endif()

add_subdirectory(samurai)

# NAT and firewall detection - STUN, RFC 5780 behaviour discovery and a UPnP
# gateway of its own. After samurai, which it links, and before QuickDC so that
# the four read in the order they depend on each other; nothing links it yet.
add_subdirectory(libdrop-cpp)

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
