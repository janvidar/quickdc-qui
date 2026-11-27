#
## Copyright (c) 2026, Jan Vidar Krey
#

find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# Deliberately only the bridge: a test that could reach a core header would not
# be testing the isolation it is here to check.
add_executable(tst_bridge tst_bridge.cpp)
# quickdc::quickdc as well as the bridge, and only here: this is the test of the
# isolation layer, and part of that is checking that what the views work out for
# themselves still matches what the core decided.
target_link_libraries(tst_bridge PRIVATE qdcbridge quickdc::quickdc Qt6::Test)

add_executable(tst_shell tst_shell.cpp)
target_link_libraries(tst_shell PRIVATE qdcgui Qt6::Test)

add_executable(tst_hubs tst_hubs.cpp)
target_link_libraries(tst_hubs PRIVATE qdcgui Qt6::Test)

add_executable(tst_transfers tst_transfers.cpp)
target_link_libraries(tst_transfers PRIVATE qdcgui Qt6::Test)

add_executable(tst_hublist tst_hublist.cpp)
target_link_libraries(tst_hublist PRIVATE qdcgui Qt6::Test)

add_executable(tst_settings tst_settings.cpp)
target_link_libraries(tst_settings PRIVATE qdcgui Qt6::Test)

add_executable(tst_search tst_search.cpp)
target_link_libraries(tst_search PRIVATE qdcgui Qt6::Test)

add_executable(tst_richtext tst_richtext.cpp)
target_link_libraries(tst_richtext PRIVATE qdcgui Qt6::Test)

# The CommonMark examples are read at run time rather than compiled in: they are
# the specification's own data file, and a build should not have to be repeated to
# take a newer one.
add_executable(tst_markdown tst_markdown.cpp)
target_link_libraries(tst_markdown PRIVATE qdcgui Qt6::Test)
target_compile_definitions(tst_markdown PRIVATE
    QDC_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

add_executable(tst_theme tst_theme.cpp)
target_link_libraries(tst_theme PRIVATE qdcgui Qt6::Test)

add_executable(tst_boards tst_boards.cpp)
target_link_libraries(tst_boards PRIVATE qdcgui Qt6::Test)

foreach(target tst_bridge tst_shell tst_hubs tst_hublist tst_transfers tst_settings tst_search tst_richtext tst_markdown tst_theme tst_boards)
    set_target_properties(${target} PROPERTIES
        AUTOMOC ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()

add_test(NAME bridge COMMAND tst_bridge)
add_test(NAME shell COMMAND tst_shell)
add_test(NAME hubs COMMAND tst_hubs)
add_test(NAME hublist COMMAND tst_hublist)
add_test(NAME transfers COMMAND tst_transfers)
add_test(NAME settings COMMAND tst_settings)
add_test(NAME search COMMAND tst_search)
add_test(NAME richtext COMMAND tst_richtext)
add_test(NAME markdown COMMAND tst_markdown)
add_test(NAME theme COMMAND tst_theme)
add_test(NAME boards COMMAND tst_boards)

# Not a program but a rule: see the script for which files may wait for the core.
# Windows runs it with Git's sh, as qdc's scenarios are (quickdc/cli/CMakeLists.txt).
if(NOT WIN32)
    add_test(NAME blocking
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/nothing-blocks-the-views.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/..)
elseif(QUICKDC_SH)
    add_test(NAME blocking
        COMMAND ${QUICKDC_SH} ${CMAKE_CURRENT_SOURCE_DIR}/nothing-blocks-the-views.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

# Generous, because most of what these do is wait for the reactor: a job posted
# while it is in poll() is answered within a quarter of a second, and there are a
# good many of them.
set_tests_properties(bridge shell hubs hublist transfers settings search richtext markdown theme boards PROPERTIES TIMEOUT 120)

# The shell test builds real widgets and so needs a QApplication, which wants a
# display. Offscreen gives it one that does not, so the suite runs over ssh and
# on a build machine.
set_tests_properties(shell hubs hublist transfers settings search richtext markdown theme boards PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(QUICKDC_SANITIZE)
    set_property(TEST bridge shell hubs hublist transfers settings search richtext markdown theme boards
        APPEND PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
endif()
