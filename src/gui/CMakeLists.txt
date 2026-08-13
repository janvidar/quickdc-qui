#
## The Qt widget layer
## Copyright (c) 2026, Jan Vidar Krey
#

# A library rather than part of the executable, so that the shell can be built
# and driven by a test without a main().
add_library(qdcgui STATIC
    boardmodel.cpp
    boardmodel.h
    boardpane.cpp
    boardpane.h
    chatfeed.cpp
    chatfeed.h
    chatmodel.cpp
    chatmodel.h
    chatpane.cpp
    chatpane.h
    composer.cpp
    composer.h
    connectionbar.cpp
    connectionbar.h
    cornermask.cpp
    cornermask.h
    favouritedelegate.cpp
    favouritedelegate.h
    fileactions.cpp
    fileactions.h
    filelistview.cpp
    filelistview.h
    homeview.cpp
    homeview.h
    hubbrowser.cpp
    hubbrowser.h
    hublistmodel.cpp
    hublistmodel.h
    hubpane.cpp
    hubpane.h
    hubsview.cpp
    hubsview.h
    hubheader.cpp
    hubheader.h
    mainwindow.cpp
    mainwindow.h
    markdown.cpp
    markdown.h
    markdownentities.h
    nativewindow.h
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
    richtext.cpp
    richtext.h
    searchmodel.cpp
    searchmodel.h
    searchview.cpp
    searchview.h
    settingsview.cpp
    settingsview.h
    theme.cpp
    theme.h
    transfermodels.cpp
    transfermodels.h
    transfersview.cpp
    transfersview.h
    usermenu.cpp
    usermenu.h
    view.cpp
    view.h
    widgets.cpp
    widgets.h)

# The schemes and the stylesheet template are data, read at run time rather than
# compiled in as string literals - which is what lets a colour or a corner radius be
# changed without a build. Compiled into the binary as resources all the same, so an
# installed application depends on no file it does not carry; a copy of any of them
# in ~/.quickdc/themes is read instead.
#
# qt_add_resources rather than a .qrc in the sources: this is a static library, and
# a resource in one is dropped by the linker unless something registers it. The Qt
# function generates that registration.
qt_add_resources(qdcgui "themes"
    PREFIX "/themes"
    BASE themes
    FILES
        themes/widgets.qss
        themes/dark.json)

# The title bar is the platform's, so colouring it is platform code: Objective-C++
# against AppKit on macOS, and a no-op elsewhere.
if(APPLE)
    target_sources(qdcgui PRIVATE nativewindow_mac.mm)
    target_link_libraries(qdcgui PRIVATE "-framework AppKit")
else()
    target_sources(qdcgui PRIVATE nativewindow_other.cpp)
endif()

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
