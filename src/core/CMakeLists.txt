#
## The isolation layer between the QuickDC core and the Qt views
## Copyright (c) 2026, Jan Vidar Krey
#

# Headers are listed as sources so that AUTOMOC scans them: Q_OBJECT lives in
# every bridge header, and Q_NAMESPACE in coretypes.h.
add_library(qdcbridge STATIC
    boardbridge.cpp
    boardbridge.h
    configbridge.cpp
    configbridge.h
    completed.cpp
    completed.h
    corethread.cpp
    corethread.h
    coretypes.h
    dropbridge.cpp
    dropbridge.h
    favourites.cpp
    favourites.h
    filelistbridge.cpp
    filelistbridge.h
    shares.cpp
    shares.h
    hubbridge.cpp
    hubbridge.h
    hubdirectory.cpp
    hubdirectory.h
    hublistbridge.cpp
    hublistbridge.h
    mediabridge.cpp
    mediabridge.h
    networkwatch.cpp
    recenthubs.cpp
    recenthubs.h
    relay.cpp
    relay.h
    searchbridge.cpp
    searchbridge.h
    transferbridge.cpp
    transferbridge.h)

set_target_properties(qdcbridge PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)

# PUBLIC: the bridge headers are what everything above this includes, and they
# are found relative to src/.
target_include_directories(qdcbridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# PRIVATE, and this is the whole point of the library.
#
# quickdc::quickdc carries core/ as a PUBLIC include directory, so linking it
# PUBLIC would put every core header within reach of every view that links this.
# Keeping it private means a view that reaches for one does not compile, and the
# isolation rule is enforced by the build rather than by good intentions.
#
# Qt6::Core is PUBLIC because the bridge headers are full of QObject and QString.
# Qt6::Network is where QNetworkInformation lives, which is what networkwatch.cpp
# watches this machine's own connectivity through.
target_link_libraries(qdcbridge
    PUBLIC
        Qt6::Core
    PRIVATE
        Qt6::Network
        quickdc::quickdc)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qdcbridge PRIVATE -Wall -Wextra)
endif()
