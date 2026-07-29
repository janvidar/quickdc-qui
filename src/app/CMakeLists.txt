#
## The application executable
## Copyright (c) 2026, Jan Vidar Krey
#

add_executable(quickdc-qui MACOSX_BUNDLE main.cpp)

set_target_properties(quickdc-qui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

target_link_libraries(quickdc-qui PRIVATE qdcgui)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(quickdc-qui PRIVATE -Wall -Wextra)
endif()
