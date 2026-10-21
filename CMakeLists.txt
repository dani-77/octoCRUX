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
    src/core/sync_schedule.h
    src/core/sync_schedule.cpp
    src/core/update_summary.h
    src/core/update_summary.cpp
    src/core/octocrux_settings.h
    src/core/octocrux_settings.cpp
    src/notifier/notifier_core.h
    src/notifier/notifier_core.cpp
    src/notifier/unattended_sync.h
    src/notifier/unattended_sync.cpp
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
    src/ui/transaction_preview_dialog.h
    src/ui/transaction_preview_dialog.cpp
    src/ui/settings_dialog.h
    src/ui/settings_dialog.cpp
)

target_link_libraries(octocrux_core PUBLIC
    Qt6::Core
    Qt6::Widgets
)

# Single source of the version shown by both executables (--version).
target_compile_definitions(octocrux_core PUBLIC OCTOCRUX_VERSION="${PROJECT_VERSION}")

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

# System tray notifier: read-only update count; ports -u only when the user asks
add_executable(octocrux-notifier
    src/notifier/main.cpp
    src/notifier/notifier_tray.h
    src/notifier/notifier_tray.cpp
    src/notifier/notifier_options_dialog.h
    src/notifier/notifier_options_dialog.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(octocrux-notifier PRIVATE
    octocrux_core
    Qt6::Widgets
    Qt6::Network
)

# Until packaging (Phase 5a) installs it, the options dialog takes the polkit
# action definition from the source tree.
target_compile_definitions(octocrux-notifier PRIVATE
    OCTOCRUX_SOURCE_POLICY="${CMAKE_SOURCE_DIR}/contrib/polkit/org.octocrux.ports-sync.policy"
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

# Installation (Phase 5a). Paths follow GNUInstallDirs; the CRUX port uses
# CMAKE_INSTALL_PREFIX=/usr. Tests and octocrux_live_check (a developer
# diagnostic) are not installed.
include(GNUInstallDirs)

install(TARGETS octocrux octocrux-notifier
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES data/octocrux.desktop data/octocrux-notifier.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES data/icons/octocrux.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

# The polkit action only defines org.octocrux.ports-sync (auth_admin by itself);
# the password-free rule is installed per user, on opt-in, by the notifier.
install(FILES contrib/polkit/org.octocrux.ports-sync.policy
    DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions
)

# CRUX packages carry no documentation; the port turns this off.
option(OCTOCRUX_INSTALL_DOCS "Install README, LICENSE and the sudoers example under the doc directory" ON)
if(OCTOCRUX_INSTALL_DOCS)
    install(FILES README.md LICENSE contrib/sudoers.d/octocrux-ports-sync.example
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )
endif()
