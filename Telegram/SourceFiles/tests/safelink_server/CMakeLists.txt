cmake_minimum_required(VERSION 3.16)
project(safelink_server_tests LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
enable_testing()
add_executable(safelink_server_tests
    safelink_server_tests.cpp
    ../../mtproto/safelink_server.cpp
)
target_compile_features(safelink_server_tests PRIVATE cxx_std_20)
target_include_directories(safelink_server_tests PRIVATE ../..)
target_link_libraries(safelink_server_tests PRIVATE Qt6::Core Qt6::Network)
add_test(NAME safelink_server_tests COMMAND safelink_server_tests)
