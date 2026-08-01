#
## The application executable
## Copyright (c) 2026, Jan Vidar Krey
#

add_executable(quickdc-qui MACOSX_BUNDLE main.cpp)

set_target_properties(quickdc-qui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# A bundle of our own rather than the one CMake generates, for two keys it has no
# variables for: NSLocalNetworkUsageDescription, without which the prompt macOS
# raises the first time this reaches a local address has nothing to say - and a
# bundle identifier of our own, which is what that decision is remembered against.
if(APPLE)
    set_target_properties(quickdc-qui PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.extatic.quickdc-qui"
        MACOSX_BUNDLE_BUNDLE_NAME "QuickDC"
        MACOSX_BUNDLE_EXECUTABLE_NAME "quickdc-qui"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
endif()

target_link_libraries(quickdc-qui PRIVATE qdcgui)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(quickdc-qui PRIVATE -Wall -Wextra)
endif()
