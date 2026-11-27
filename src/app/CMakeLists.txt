#
## The application executable
## Copyright (c) 2026, Jan Vidar Krey
#

add_executable(quickdc-qui MACOSX_BUNDLE main.cpp diagnostics.cpp diagnostics.h)
set_target_properties(quickdc-qui PROPERTIES AUTOMOC ON)

set_target_properties(quickdc-qui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# A bundle of our own rather than the one CMake generates, for keys it has no
# variables for: NSLocalNetworkUsageDescription, without which the prompt macOS
# raises the first time this reaches a local address has nothing to say - and a
# bundle identifier of our own, which is what that decision is remembered against.
if(APPLE)
    # ---------------------------------------------------------------------
    # The icon
    #
    # Built here rather than committed, from the same drawing the Linux icon
    # is: one figure, and no second file to forget to update. sips and
    # iconutil are both part of macOS, so this needs nothing installed - but
    # neither can read an SVG, which is why the master beside them is a PNG
    # rendered from packaging/linux/app.svg (see packaging/macos/README.md).
    # ---------------------------------------------------------------------
    # What the bundle, the executable inside it and the icon are all called.
    # One name, because the three have to agree: the bundle directory is
    # <name>.app, the binary in Contents/MacOS is <name>, and CFBundleExecutable
    # has to name that binary or the bundle will not launch.
    #
    # "QuickDC" rather than "quickdc-qui", because on macOS this string is what
    # Finder shows in Applications - the same name the Linux desktop entry
    # carries - where the executable's name is an implementation detail.
    set(QUICKDC_QUI_BUNDLE_NAME "QuickDC")
    set(QUICKDC_QUI_ICNS_NAME "${QUICKDC_QUI_BUNDLE_NAME}")

    set(icon_master ${PROJECT_SOURCE_DIR}/packaging/macos/icon.png)
    set(iconset ${CMAKE_CURRENT_BINARY_DIR}/${QUICKDC_QUI_ICNS_NAME}.iconset)
    set(icns ${CMAKE_CURRENT_BINARY_DIR}/${QUICKDC_QUI_ICNS_NAME}.icns)

    # The sizes iconutil expects, each also at @2x. Anything missing is simply
    # absent from the result, and the Dock then scales whatever is nearest.
    set(icns_commands COMMAND ${CMAKE_COMMAND} -E make_directory ${iconset})
    foreach(size IN ITEMS 16 32 128 256 512)
        math(EXPR retina "${size} * 2")
        list(APPEND icns_commands
            COMMAND sips -z ${size} ${size} ${icon_master}
                    --out ${iconset}/icon_${size}x${size}.png
            COMMAND sips -z ${retina} ${retina} ${icon_master}
                    --out ${iconset}/icon_${size}x${size}@2x.png)
    endforeach()

    add_custom_command(
        OUTPUT ${icns}
        ${icns_commands}
        COMMAND iconutil --convert icns --output ${icns} ${iconset}
        DEPENDS ${icon_master}
        COMMENT "Building ${QUICKDC_QUI_ICNS_NAME}.icns"
        VERBATIM)

    # A source of the target rather than an install rule: this is what puts it
    # inside the bundle, where the plist's CFBundleIconFile can find it.
    target_sources(quickdc-qui PRIVATE ${icns})
    set_source_files_properties(${icns} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)

    # ---------------------------------------------------------------------
    # The bundle
    # ---------------------------------------------------------------------

    # Substituted here rather than by MACOSX_BUNDLE_INFO_PLIST, which resolves
    # the documented MACOSX_BUNDLE_* names and nothing else - and the icon name
    # and the deployment target are not among them.
    configure_file(Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)

    set_target_properties(quickdc-qui PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        # Still set, because CMake reads this one itself when it signs.
        MACOSX_BUNDLE_GUI_IDENTIFIER "${QUICKDC_QUI_APP_ID}"
        OUTPUT_NAME "${QUICKDC_QUI_BUNDLE_NAME}")
endif()

# On Windows: a GUI program, so no console window comes up with it, called QuickDC
# as the bundle is, with the icon and version information in its resources.
if(WIN32)
    set(QUICKDC_QUI_ICO ${PROJECT_SOURCE_DIR}/packaging/windows/app.ico)
    configure_file(app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(quickdc-qui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    set_target_properties(quickdc-qui PROPERTIES
        WIN32_EXECUTABLE ON
        OUTPUT_NAME QuickDC)
endif()

target_link_libraries(quickdc-qui PRIVATE qdcgui)

# The handler goes beside the executable, where diagnostics.cpp looks for it: in
# the bundle's Contents/MacOS on macOS, beside QuickDC.exe on Windows.
if(QUICKDC_QUI_CRASH_REPORTS)
    target_link_libraries(quickdc-qui PRIVATE sentry::sentry)
    target_compile_definitions(quickdc-qui PRIVATE
        QUICKDC_QUI_CRASH_REPORTS=1
        QUICKDC_QUI_CRASH_DSN="${QUICKDC_QUI_CRASH_DSN}")
    add_dependencies(quickdc-qui crashpad_handler)
    add_custom_command(TARGET quickdc-qui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:quickdc-qui>
        VERBATIM)
endif()

# The identity and the version as the compiler sees them. Only main.cpp needs
# either, and only to tell the desktop what it is looking at, so they are
# defined on the executable rather than anywhere a view could reach them.
target_compile_definitions(quickdc-qui PRIVATE
    QUICKDC_QUI_APP_ID="${QUICKDC_QUI_APP_ID}"
    QUICKDC_QUI_VERSION_STRING="${QUICKDC_QUI_VERSION_STRING}"
    QUICKDC_QUI_CHANNEL="${QUICKDC_UPDATE_CHANNEL}")

if(QUICKDC_QUI_INSTALL)
    install(TARGETS quickdc-qui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(quickdc-qui PRIVATE -Wall -Wextra)
endif()
