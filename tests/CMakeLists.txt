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
