#
## The Qt widget layer
## Copyright (c) 2026, Jan Vidar Krey
#

# A library rather than part of the executable, so that the shell can be built
# and driven by a test without a main().
add_library(qdcgui STATIC
    chatfeed.cpp
    chatfeed.h
    chatmodel.cpp
    chatmodel.h
    chatpane.cpp
    chatpane.h
    composer.cpp
    composer.h
    fileactions.cpp
    fileactions.h
    filelistview.cpp
    filelistview.h
    homeview.cpp
    homeview.h
    hubpane.cpp
    hubpane.h
    hubsview.cpp
    hubsview.h
    hubheader.cpp
    hubheader.h
    mainwindow.cpp
    mainwindow.h
    usermodel.cpp
    usermodel.h
    navigationrail.cpp
    navigationrail.h
    railbutton.cpp
    railbutton.h
    glyphs.cpp
    glyphs.h
    progressdelegate.cpp
    progressdelegate.h
    settingsview.cpp
    settingsview.h
    transfermodels.cpp
    transfermodels.h
    transfersview.cpp
    transfersview.h
    view.cpp
    view.h)

set_target_properties(qdcgui PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)

target_include_directories(qdcgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# qdcbridge is PUBLIC because the view headers name its types, and it in turn
# keeps quickdc::quickdc private - so nothing here can reach a core header. That
# is the isolation rule, enforced by the link rather than by review.
target_link_libraries(qdcgui
    PUBLIC
        qdcbridge
        Qt6::Widgets)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qdcgui PRIVATE -Wall -Wextra)
endif()
