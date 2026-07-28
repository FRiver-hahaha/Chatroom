cmake_minimum_required(VERSION 3.16)
project(ChatroomClient VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Protobuf（客户端唯一需要的依赖）
find_package(Protobuf REQUIRED)

# 从项目根目录的 proto 文件生成 C++ 桩
set(PROTO_DIR ${CMAKE_SOURCE_DIR}/..)
set(PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_DIR}/chatroom.proto)

# 客户端可执行文件
add_executable(client
    ../client.cpp
    ${PROTO_SRCS}
)

target_include_directories(client PRIVATE
    ${CMAKE_SOURCE_DIR}/..     # 用于 #include "service/MessageType.hpp"
    ${PROTO_GEN_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
)

target_link_libraries(client PRIVATE
    ${PROTOBUF_LIBRARIES}
    pthread
)
