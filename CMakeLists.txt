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
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# ===== Glog (日志库，外部依赖，FetchContent 自动构建) =====
include(FetchContent)
FetchContent_Declare(
    glog
    GIT_REPOSITORY https://github.com/google/glog.git
    GIT_TAG v0.7.1
)
set(WITH_GFLAGS OFF CACHE BOOL "Disable gflags for glog")
set(WITH_UNWIND OFF CACHE BOOL "Disable unwinding support for glog")
FetchContent_MakeAvailable(glog)

# ===== lib: 客户端网络库（已移除 — client 为自包含单文件）=====

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
    glog::glog
)

target_compile_options(server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
)

# ===== CLI 客户端（自包含单文件，链接 proto 源）=====
add_executable(client
    client/client.cpp
    ${PROTO_SRCS}
)

target_include_directories(client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PROTO_GEN_DIR}
    ${CMAKE_BINARY_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(client PRIVATE
    ${PROTOBUF_LIBRARIES}
    OpenSSL::Crypto
    Threads::Threads
    ${SQLITE3_LIBRARIES}
)

target_compile_options(client PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# ===== Qt 客户端（需要 Qt6）=====
find_package(Qt6 QUIET COMPONENTS Widgets Network Core Sql)
if(Qt6_FOUND)
    add_subdirectory(qt-client)
    message(STATUS "Qt6 found — building qt-client")
else()
    message(STATUS "Qt6 not found — skipping qt-client")
endif()
