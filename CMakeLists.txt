cmake_minimum_required(VERSION 3.22)
project(octocrux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

# Enable compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Core library (parsers, domain logic, models, and UI)
add_library(octocrux_core STATIC
    include/octocrux/types.h
    include/octocrux/icruxbackend.h
    src/core/printf_parser.h
    src/core/printf_parser.cpp
    src/core/aliases_parser.h
    src/core/aliases_parser.cpp
    src/core/dependency_parser.h
    src/core/dependency_parser.cpp
    src/core/build_log_analyzer.h
    src/core/build_log_analyzer.cpp
    src/backend/command_runner.h
    src/backend/command_runner.cpp
    src/backend/privilege_helper.h
    src/backend/privilege_helper.cpp
    src/backend/crux_process_runner.h
    src/backend/crux_process_runner.cpp
    src/backend/crux_adapter.h
    src/backend/crux_adapter.cpp
    src/model/package_model.h
    src/model/package_model.cpp
    src/model/package_filter_proxy_model.h
    src/model/package_filter_proxy_model.cpp
    src/operations/transaction_planner.h
    src/operations/transaction_planner.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
)

target_link_libraries(octocrux_core PUBLIC
    Qt6::Core
    Qt6::Widgets
)

target_include_directories(octocrux_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Main OctoCRUX Application Executable
add_executable(octocrux
    src/main.cpp
)

target_link_libraries(octocrux PRIVATE
    octocrux_core
    Qt6::Widgets
)

# Testing
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# Standalone Level 2 Live Sanity Check Tool (for bare metal CRUX verification)
add_executable(octocrux_live_check
    tools/octocrux_live_check.cpp
)

target_link_libraries(octocrux_live_check PRIVATE
    octocrux_core
    Qt6::Core
)
