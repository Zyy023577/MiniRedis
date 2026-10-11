cmake_minimum_required(VERSION 3.16)
project(MiniRedisStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(miniredis_core
    src/core/command_processor.cpp
    src/core/key_value_store.cpp
    src/core/metrics.cpp
    src/api/admin_api.cpp
    src/network/socket.cpp
    src/network/tcp_client.cpp
    src/network/tcp_server.cpp
    src/persistence/aof_persistence.cpp
)
target_include_directories(miniredis_core PUBLIC include)
target_link_libraries(miniredis_core PUBLIC Threads::Threads)

if (WIN32)
    target_link_libraries(miniredis_core PUBLIC ws2_32)
endif()

if (MSVC)
    target_compile_options(miniredis_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(miniredis_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(miniredis_core_tests tests/core_tests.cpp)
target_link_libraries(miniredis_core_tests PRIVATE miniredis_core)

add_executable(miniredis_server src/server_main.cpp)
target_link_libraries(miniredis_server PRIVATE miniredis_core)

add_executable(miniredis_cli src/cli_main.cpp)
target_link_libraries(miniredis_cli PRIVATE miniredis_core)

add_executable(miniredis_benchmark src/benchmark_main.cpp)
target_link_libraries(miniredis_benchmark PRIVATE miniredis_core)

add_executable(miniredis_tcp_tests tests/tcp_integration_tests.cpp)
target_link_libraries(miniredis_tcp_tests PRIVATE miniredis_core)

add_executable(miniredis_aof_tests tests/aof_integration_tests.cpp)
target_link_libraries(miniredis_aof_tests PRIVATE miniredis_core)

add_executable(miniredis_admin_api_tests tests/admin_api_tests.cpp)
target_link_libraries(miniredis_admin_api_tests PRIVATE miniredis_core)

enable_testing()
add_test(NAME miniredis_core_tests COMMAND miniredis_core_tests)
add_test(NAME miniredis_tcp_tests COMMAND miniredis_tcp_tests)
add_test(NAME miniredis_aof_tests COMMAND miniredis_aof_tests)
add_test(NAME miniredis_admin_api_tests COMMAND miniredis_admin_api_tests)
