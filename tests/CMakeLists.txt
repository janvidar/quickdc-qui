#
## Copyright (c) 2026, Jan Vidar Krey
#

find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# Deliberately only the bridge: a test that could reach a core header would not
# be testing the isolation it is here to check.
add_executable(tst_bridge tst_bridge.cpp)
target_link_libraries(tst_bridge PRIVATE qdcbridge Qt6::Test)

add_executable(tst_shell tst_shell.cpp)
target_link_libraries(tst_shell PRIVATE qdcgui Qt6::Test)

add_executable(tst_hubs tst_hubs.cpp)
target_link_libraries(tst_hubs PRIVATE qdcgui Qt6::Test)

add_executable(tst_transfers tst_transfers.cpp)
target_link_libraries(tst_transfers PRIVATE qdcgui Qt6::Test)

foreach(target tst_bridge tst_shell tst_hubs tst_transfers)
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
add_test(NAME transfers COMMAND tst_transfers)

# Generous, because most of what these do is wait for the reactor: a job posted
# while it is in poll() is answered within a quarter of a second, and there are a
# good many of them.
set_tests_properties(bridge shell hubs transfers PROPERTIES TIMEOUT 120)

# The shell test builds real widgets and so needs a QApplication, which wants a
# display. Offscreen gives it one that does not, so the suite runs over ssh and
# on a build machine.
set_tests_properties(shell hubs transfers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
