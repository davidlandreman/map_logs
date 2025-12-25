cmake_minimum_required(VERSION 3.16)
project(mcp_log_server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_TESTS "Build unit tests" ON)

# Find system SQLite3
find_package(SQLite3 REQUIRED)

# Find OpenSSL for HTTPS support
find_package(OpenSSL REQUIRED)

# Fetch dependencies
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# cpp-httplib
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)

# asio (standalone, no Boost)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-29-0
)

FetchContent_MakeAvailable(json httplib asio)

# asio is header-only, create interface target
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

# Main executable
add_executable(mcp_log_server
    src/main.cpp
    src/log_store.cpp
    src/udp_receiver.cpp
    src/http_server.cpp
    src/mcp_server.cpp
)

target_include_directories(mcp_log_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(mcp_log_server PRIVATE
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
    httplib::httplib
    asio
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Enable HTTPS support in cpp-httplib
target_compile_definitions(mcp_log_server PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

# Platform-specific settings
if(APPLE)
    target_compile_definitions(mcp_log_server PRIVATE _DARWIN_C_SOURCE)
endif()

if(WIN32)
    target_link_libraries(mcp_log_server PRIVATE ws2_32)
endif()

# Install
install(TARGETS mcp_log_server DESTINATION bin)

# Tests
if(BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.0
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(test_log_store
        tests/test_log_store.cpp
        src/log_store.cpp
    )

    target_include_directories(test_log_store PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(test_log_store PRIVATE
        SQLite::SQLite3
        nlohmann_json::nlohmann_json
        Catch2::Catch2WithMain
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(test_log_store)
endif()
