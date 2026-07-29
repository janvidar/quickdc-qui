#
## Copyright (c) 2026, Jan Vidar Krey
#

find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_executable(tst_bridge tst_bridge.cpp)

set_target_properties(tst_bridge PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# Deliberately only the bridge: a test that could reach a core header would not
# be testing the isolation it is here to check.
target_link_libraries(tst_bridge PRIVATE qdcbridge Qt6::Test)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tst_bridge PRIVATE -Wall -Wextra)
endif()

add_test(NAME bridge COMMAND tst_bridge)

# Generous, because most of what the test does is wait for the reactor: a job
# posted while it is in poll() is answered within a quarter of a second, and
# there are a good many of them.
set_tests_properties(bridge PROPERTIES TIMEOUT 120)
