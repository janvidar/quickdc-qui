#
## Desktop integration: the entry, the icon and the AppStream metadata
## Copyright (c) 2026, Jan Vidar Krey
#

# The three files are named for what they are rather than for the application,
# and take the application id only on the way out. A desktop entry, a metainfo
# document and an icon all have to be installed under the id or the shell will
# not find them - but that is an installation rule, and naming the sources after
# it would mean renaming three files to change one string.
configure_file(app.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${QUICKDC_QUI_APP_ID}.desktop @ONLY)

configure_file(app.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${QUICKDC_QUI_APP_ID}.metainfo.xml @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${QUICKDC_QUI_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${QUICKDC_QUI_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

# Scalable rather than a set of sizes: the figure is six straight edges and a
# plate, so there is nothing a hand-tuned raster at 16 pixels would recover.
install(FILES app.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME ${QUICKDC_QUI_APP_ID}.svg)

# ---------------------------------------------------------------------------
# Validation
#
# A malformed entry or a metainfo document that fails to parse is something a
# package build finds out about - flatpak-builder runs appstreamcli itself and
# refuses the build - so it should be reachable here rather than only there.
#
# Its own target, not part of ALL: neither tool is a build dependency, and a
# developer who has neither installed should not be told about it every time.
# ---------------------------------------------------------------------------

find_program(QUICKDC_QUI_DESKTOP_FILE_VALIDATE desktop-file-validate)
find_program(QUICKDC_QUI_APPSTREAMCLI appstreamcli)

add_custom_target(validate-metadata
    COMMENT "Validating the desktop entry and the AppStream metainfo")

if(QUICKDC_QUI_DESKTOP_FILE_VALIDATE)
    add_custom_command(TARGET validate-metadata POST_BUILD
        COMMAND ${QUICKDC_QUI_DESKTOP_FILE_VALIDATE}
                ${CMAKE_CURRENT_BINARY_DIR}/${QUICKDC_QUI_APP_ID}.desktop
        VERBATIM)
endif()

if(QUICKDC_QUI_APPSTREAMCLI)
    # --no-net because the only thing it would reach out for is a screenshot,
    # and there are none: a build should not fail because a web server did.
    add_custom_command(TARGET validate-metadata POST_BUILD
        COMMAND ${QUICKDC_QUI_APPSTREAMCLI} validate --no-net --explain
                ${CMAKE_CURRENT_BINARY_DIR}/${QUICKDC_QUI_APP_ID}.metainfo.xml
        VERBATIM)
endif()
