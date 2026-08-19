#!/bin/bash
# ============================================================
# ChatRoom Qt 客户端一键安装脚本（局域网测试用）
#
# 用法:
#   bash setup_qt_client.sh [服务器IP]
#   例: bash setup_qt_client.sh 10.30.0.114
#
# 说明: 只编译 Qt 客户端，不构建服务端；测试者机器无需 MySQL/Redis
# ============================================================
set -e

SERVER_IP="${1:-10.30.0.114}"
REPO_URL="${CHATROOM_REPO:-git@github.com:FRiver-hahaha/Chatroom.git}"

echo "[1/4] 安装编译依赖..."
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
    qt6-base-dev libqt6sql6-sqlite libssl-dev \
    libprotobuf-dev protobuf-compiler \
    libsqlite3-dev liburing-dev libhiredis-dev libmysqlclient-dev

echo "[2/4] 获取源码..."
if [ ! -d Chatroom ]; then
    git clone "$REPO_URL" Chatroom
fi
cd Chatroom

echo "[3/4] 编译 Qt 客户端..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target qt-client -j"$(nproc)"

echo "[4/4] 启动客户端，连接服务器 $SERVER_IP ..."
echo "以后直接运行: ./build/qt-client/qt-client -H $SERVER_IP"
exec ./build/qt-client/qt-client -H "$SERVER_IP"