cmake_minimum_required(VERSION 3.16)
project(Chatroom VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===== Protobuf =====
find_package(Protobuf REQUIRED)

set(PROTO_DIR ${CMAKE_SOURCE_DIR})
set(PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_DIR}/chatroom.proto)

# Move generated files into the dedicated directory
set_source_files_properties(
    ${PROTO_SRCS} ${PROTO_HDRS}
    PROPERTIES GENERATED TRUE
)

# ===== Other dependencies =====
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(MYSQLCLIENT REQUIRED mysqlclient)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# ===== Server executable =====
add_executable(server
    main.cpp
    network/server.cpp
    network/threadPool.cpp
    storage/StorageManager.cpp
    ${PROTO_SRCS}
)

target_include_directories(server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_GEN_DIR}
    ${CMAKE_BINARY_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${LIBURING_INCLUDE_DIRS}
    ${HIREDIS_INCLUDE_DIRS}
    ${MYSQLCLIENT_INCLUDE_DIRS}
)

target_link_libraries(server PRIVATE
    ${PROTOBUF_LIBRARIES}
    ${LIBURING_LIBRARIES}
    ${HIREDIS_LIBRARIES}
    ${MYSQLCLIENT_LIBRARIES}
    OpenSSL::Crypto
    Threads::Threads
)

target_compile_options(server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
)

# ===== Client executable =====
add_executable(client
    client/client.cpp
    ${PROTO_SRCS}
)

target_include_directories(client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_GEN_DIR}
    ${CMAKE_BINARY_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
)

target_link_libraries(client PRIVATE
    ${PROTOBUF_LIBRARIES}
    Threads::Threads
)

target_compile_options(client PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
