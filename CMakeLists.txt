cmake_minimum_required(VERSION 3.15)
project(strict_error_analyser C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include_directories(include)

add_executable(strict-error-analyser
    src/common.c
    src/platform.c
    src/json.c
    src/workspace.c
    src/analyzer.c
    src/main.c
)

if(WIN32)
    target_link_libraries(strict-error-analyser PRIVATE urlmon)
endif()

set_target_properties(strict-error-analyser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
