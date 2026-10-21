# Definition of unit tests for OctoCRUX

# Define test_printf_parser executable
add_executable(test_printf_parser
    test_printf_parser.cpp
)

target_link_libraries(test_printf_parser PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

# Pass the project root so tests can locate tests/fixtures reliably
target_compile_definitions(test_printf_parser PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_printf_parser COMMAND test_printf_parser)

# Define test_aliases_parser executable
add_executable(test_aliases_parser
    test_aliases_parser.cpp
)

target_link_libraries(test_aliases_parser PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_aliases_parser PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_aliases_parser COMMAND test_aliases_parser)

# Define test_dependency_parser executable
add_executable(test_dependency_parser
    test_dependency_parser.cpp
)

target_link_libraries(test_dependency_parser PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_dependency_parser PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_dependency_parser COMMAND test_dependency_parser)

# Define test_crux_adapter executable
add_executable(test_crux_adapter
    test_crux_adapter.cpp
)

target_link_libraries(test_crux_adapter PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_crux_adapter PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_crux_adapter COMMAND test_crux_adapter)

# Define test_package_model executable
add_executable(test_package_model
    test_package_model.cpp
)

target_link_libraries(test_package_model PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_package_model PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_package_model COMMAND test_package_model)

# Define test_mainwindow executable
add_executable(test_mainwindow
    test_mainwindow.cpp
)

target_link_libraries(test_mainwindow PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Widgets
    Qt6::Test
)

target_compile_definitions(test_mainwindow PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_mainwindow COMMAND test_mainwindow)

# Define test_transaction_planner executable
add_executable(test_transaction_planner
    test_transaction_planner.cpp
)

target_link_libraries(test_transaction_planner PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_transaction_planner PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_transaction_planner COMMAND test_transaction_planner)

# Define test_build_log_analyzer executable
add_executable(test_build_log_analyzer
    test_build_log_analyzer.cpp
)

target_link_libraries(test_build_log_analyzer PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_build_log_analyzer PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_build_log_analyzer COMMAND test_build_log_analyzer)

# Define test_privilege_helper executable
add_executable(test_privilege_helper
    test_privilege_helper.cpp
)

target_link_libraries(test_privilege_helper PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_privilege_helper PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_privilege_helper COMMAND test_privilege_helper)

# Define test_crux_process_runner executable
add_executable(test_crux_process_runner
    test_crux_process_runner.cpp
)

target_link_libraries(test_crux_process_runner PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_crux_process_runner PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_crux_process_runner COMMAND test_crux_process_runner)

# Define test_live_transaction_execution executable
add_executable(test_live_transaction_execution
    test_live_transaction_execution.cpp
)

target_link_libraries(test_live_transaction_execution PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_live_transaction_execution PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_live_transaction_execution COMMAND test_live_transaction_execution)

# Runs a simulated build for ~36 s; excluded from routine runs with 'ctest -LE slow'
set_tests_properties(test_live_transaction_execution PROPERTIES LABELS slow)

# Define test_sync_schedule executable (notifier scheduling and shared settings)
add_executable(test_sync_schedule
    test_sync_schedule.cpp
)

target_link_libraries(test_sync_schedule PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME test_sync_schedule COMMAND test_sync_schedule)

# Define test_notifier executable (notifier logic without a tray)
add_executable(test_notifier
    test_notifier.cpp
)

target_link_libraries(test_notifier PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_notifier PRIVATE
    OCTOCRUX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME test_notifier COMMAND test_notifier)

# Define test_work_dir_cleanup executable (leftover pkgmk work directories after a Cancel)
add_executable(test_work_dir_cleanup
    test_work_dir_cleanup.cpp
)

target_link_libraries(test_work_dir_cleanup PRIVATE
    octocrux_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME test_work_dir_cleanup COMMAND test_work_dir_cleanup)
