# C++ Chatroom 完整实现计划

## Context

基于当前仓库 `/home/friver/Chatroom` 的设计文档 (`design.md`) 和[官方需求](https://plan.xiyoulinux.com/plan/plan7-chatroom)，从零构建一个完整的 C++ 聊天室。仓库目前仅有 `README.md` 和 `design.md`，无任何源代码。

**核心约束：**
- C++ (C 语言禁止)，GNU/Linux
- Epoll I/O 多路复用
- Redis 仅服务端使用（禁止 Pub/Sub，禁止客户端直连，单实例）
- CMake 构建
- 外部日志/测试库 (GLog, GTest)
- TCP 心跳不能单建线程
- 至少 3 个增强项
- 业务约束：私聊仅限好友间，移除的群成员不能再发消息，注销后重注册同名不能访问旧数据

**选定增强项（5 个）：**
1. 主从 Reactor 高性能服务器模型
2. TLS 通信加密
3. Protobuf 数据序列化
4. 验证码登录/注册/密码重置
5. 文件断点续传

---

## 项目目录结构

```
/home/friver/Chatroom/
├── CMakeLists.txt                  # 顶层构建
├── README.md
├── design.md
├── .gitignore
├── proto/                          # Protobuf 定义
│   ├── CMakeLists.txt
│   ├── common.proto                # UserInfo, ErrorResponse, Notification
│   ├── account.proto               # Register/Login/Logout/VerificationCode
│   ├── friend.proto                # AddFriend/DeleteFriend/Block/Status
│   ├── group.proto                 # Create/Dissolve/Join/Admin/Member
│   ├── chat.proto                  # SendMessage/History/Offline
│   └── file_transfer.proto         # Transfer/Chunk/Resume
├── common/                         # 公共工具库
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── logger.h                # GLog 封装
│   │   ├── config.h                # 配置文件解析
│   │   ├── utils.h                 # 字符串/时间/UUID 工具
│   │   ├── errors.h                # 错误码枚举
│   │   └── types.h                 # 公共类型定义
│   └── src/
│       ├── logger.cpp
│       ├── config.cpp
│       └── utils.cpp
├── transport/                      # 传输层
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── socket.h                # RAII socket 封装
│   │   ├── epoller.h               # Epoll 封装
│   │   ├── buffer.h                # RingBuffer 模板
│   │   ├── connection.h            # TCP 连接 + 缓冲区
│   │   ├── acceptor.h              # Master: accept + 分发
│   │   ├── reactor.h               # Slave: epoll 事件循环
│   │   ├── tcp_server.h            # 服务器编排器
│   │   ├── tcp_client.h            # 客户端连接
│   │   ├── heartbeat.h             # timerfd 心跳
│   │   └── tls_context.h           # OpenSSL 上下文
│   └── src/                        # 对应 .cpp 实现
├── protocol/                       # 协议层
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── codec.h                 # [4B len][2B type][protobuf] 编解码
│   │   ├── message.h               # 消息信封 + 类型注册表
│   │   └── handler.h               # 抽象 Handler 接口
│   └── src/
├── storage/                        # 数据访问层
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── mysql_pool.h            # MySQL 连接池 (RAII)
│   │   ├── redis_pool.h            # Redis 连接池 (hiredis)
│   │   ├── user_repo.h             # 用户 CRUD
│   │   ├── friend_repo.h           # 好友关系 CRUD
│   │   ├── group_repo.h            # 群组 + 成员 CRUD
│   │   ├── message_repo.h          # 消息 + 离线消息 CRUD
│   │   ├── file_repo.h             # 文件 CRUD
│   │   └── notification_repo.h     # 通知 CRUD
│   └── src/
├── threadpool/                     # 线程池
│   ├── CMakeLists.txt
│   ├── include/thread_pool.h
│   └── src/thread_pool.cpp
├── business/                       # 业务逻辑层
│   ├── CMakeLists.txt
│   ├── account/
│   │   ├── include/account_service.h
│   │   └── src/account_service.cpp
│   ├── friend/
│   │   ├── include/friend_service.h
│   │   └── src/friend_service.cpp
│   ├── group/
│   │   ├── include/group_service.h
│   │   └── src/group_service.cpp
│   ├── chat/
│   │   ├── include/chat_service.h
│   │   └── src/chat_service.cpp
│   ├── file_transfer/
│   │   ├── include/file_service.h
│   │   └── src/file_service.cpp
│   ├── notification/
│   │   ├── include/notification_service.h
│   │   └── src/notification_service.cpp
│   └── admin/
│       ├── include/admin_service.h
│       └── src/admin_service.cpp
├── server/                         # 服务器可执行文件
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── chat_server.h           # 组装所有组件
│   │   └── session_manager.h       # user_id -> Connection 映射
│   ├── src/
│   │   ├── chat_server.cpp
│   │   ├── session_manager.cpp
│   │   └── main.cpp
│   └── config/server.conf
├── client/                         # 客户端可执行文件
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── client_app.h            # 应用编排器
│   │   ├── client_session.h        # 会话状态
│   │   ├── ui.h                    # 终端 UI
│   │   └── input_handler.h         # 命令解析
│   ├── src/
│   │   ├── client_app.cpp
│   │   ├── client_session.cpp
│   │   ├── ui.cpp
│   │   ├── input_handler.cpp
│   │   └── main.cpp
│   └── config/client.conf
├── sql/schema.sql                  # 数据库建表
├── test/                           # GTest 测试
│   ├── CMakeLists.txt
│   ├── test_buffer.cpp
│   ├── test_codec.cpp
│   ├── test_account.cpp
│   ├── test_friend.cpp
│   ├── test_group.cpp
│   ├── test_chat.cpp
│   ├── test_heartbeat.cpp
│   ├── test_integration.cpp
│   └── fixtures/test_fixture.h
├── scripts/
│   ├── build.sh
│   └── init_db.sh
└── docs/
    ├── USER_GUIDE.md
    ├── BUILD.md
    └── ARCHITECTURE.md
```

**约 80 个源文件，预估 6000+ 行代码。**

---

## 架构概览

```
┌──────────────────────────────────────────────────────────┐
│                      Client (终端 UI)                     │
│  stdin ──► epoll ──► input_handler ──► codec ──► socket  │
│  stdout ◄── ui ◄── client_session ◄── codec ◄── socket   │
└──────────────────────────────────────────────────────────┘
                            │ TCP + TLS
                            ▼
┌──────────────────────────────────────────────────────────┐
│                      Server                               │
│  ┌──────────────────────────────────────────────────┐    │
│  │  Master Reactor (accept + 轮询分发)               │    │
│  │       │ eventfd 通知                             │    │
│  │  ┌────▼────┐  ┌─────────┐  ┌─────────┐          │    │
│  │  │ Slave 0 │  │ Slave 1 │  │ Slave N │  epoll   │    │
│  │  │ reactor │  │ reactor │  │ reactor │  循环    │    │
│  │  └────┬────┘  └────┬────┘  └────┬────┘          │    │
│  │       │            │            │                │    │
│  │       └────────────┼────────────┘                │    │
│  │                    ▼                             │    │
│  │  ┌─────────────────────────────────────────┐     │    │
│  │  │         Thread Pool (业务处理)            │     │    │
│  │  │  Handler → Service → Repository          │     │    │
│  │  └─────────────────────────────────────────┘     │    │
│  │                    │                             │    │
│  │         ┌──────────┴──────────┐                  │    │
│  │         ▼                     ▼                  │    │
│  │  ┌─────────────┐     ┌──────────────┐           │    │
│  │  │  MySQL Pool │     │  Redis Pool  │           │    │
│  │  │  (持久化)   │     │  (缓存/会话) │           │    │
│  │  └─────────────┘     └──────────────┘           │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

---

## 协议设计

### 线格式 (Wire Format)

```
+-----------+-----------+--------------------------+
| 4 bytes   | 2 bytes   | N bytes                  |
| BigEndian | BigEndian |                          |
| total_len | msg_type  | protobuf serialized body |
+-----------+-----------+--------------------------+
```

- `total_len`: 整个帧长度（含这 6 字节头）
- `msg_type`: 消息类型 ID（见下方注册表）
- `body`: 对应类型的 Protobuf 序列化数据

### 消息类型注册表

```
System (0x0001-0x00FF):
  0x0001 HeartbeatPing    0x0002 HeartbeatPong
  0x0005 ErrorResponse    0x0006 ServerNotification

Account (0x0100-0x01FF):
  0x0100 RegisterRequest       0x0101 RegisterResponse
  0x0102 LoginRequest          0x0103 LoginResponse
  0x0104 LogoutRequest         0x0105 LogoutResponse
  0x0106 VerificationCodeReq   0x0107 VerificationCodeResp

Friend (0x0200-0x02FF):
  0x0200 AddFriendRequest      0x0201 AddFriendResponse
  0x0202 DeleteFriendRequest   0x0203 DeleteFriendResponse
  0x0204 FriendListRequest     0x0205 FriendListResponse
  0x0206 BlockFriendRequest    0x0207 BlockFriendResponse
  0x020A FriendStatusNotify    (server push)

Group (0x0300-0x03FF):
  0x0300 CreateGroupRequest    0x0301 CreateGroupResponse
  0x0302 DissolveGroupRequest  0x0303 DissolveGroupResponse
  0x0304 JoinGroupRequest      0x0305 JoinGroupResponse
  0x0306 LeaveGroupRequest     0x0307 LeaveGroupResponse
  0x030C ApproveMemberRequest  0x030D ApproveMemberResponse
  0x030E RemoveMemberRequest   0x030F RemoveMemberResponse
  0x0310 SetAdminRequest       0x0311 SetAdminResponse

Chat (0x0400-0x04FF):
  0x0400 SendMessageRequest    0x0401 SendMessageResponse
  0x0402 MessageHistoryReq     0x0403 MessageHistoryResp
  0x0404 NewMessageNotify      (server push)

File Transfer (0x0500-0x05FF):
  0x0500 FileTransferRequest   0x0501 FileTransferResponse
  0x0502 FileChunkData         0x0503 FileChunkAck
  0x0504 FileTransferComplete  0x0505 FileResumeRequest
```

### Handler 分发机制

```cpp
// 抽象接口
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    virtual std::unique_ptr<google::protobuf::Message> handle(
        uint16_t msg_type,
        const google::protobuf::Message& request,
        uint64_t connection_id,
        const std::string& session_token) = 0;
};

// 服务器中维护分发表
std::unordered_map<uint16_t, std::unique_ptr<IMessageHandler>> dispatcher_;
```

流程: Codec 解码 → 查 dispatcher_ → ThreadPool 执行 → Response 入队 → Epoll EPOLLOUT 发送

---

## 数据库设计

### MySQL 表结构

**users** — 用户表（软删除）:
```sql
CREATE TABLE users (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,  -- bcrypt/scrypt
    email VARCHAR(128),
    nickname VARCHAR(128) DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at TIMESTAMP NULL DEFAULT NULL,
    UNIQUE KEY uk_username (username)
);
```

**friendships** — 好友关系（双向）:
```sql
CREATE TABLE friendships (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    friend_id BIGINT UNSIGNED NOT NULL,
    status TINYINT DEFAULT 0,  -- 0=pending, 1=accepted, 2=blocked
    UNIQUE KEY uk_friendship (user_id, friend_id),
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (friend_id) REFERENCES users(id)
);
```

**chat_groups** + **group_members** — 群组 + 成员:
```sql
CREATE TABLE chat_groups (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    description TEXT DEFAULT '',
    owner_id BIGINT UNSIGNED NOT NULL,
    deleted_at TIMESTAMP NULL DEFAULT NULL,
    FOREIGN KEY (owner_id) REFERENCES users(id)
);

CREATE TABLE group_members (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    group_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    role TINYINT DEFAULT 0,     -- 0=member, 1=admin, 2=owner
    status TINYINT DEFAULT 0,   -- 0=pending, 1=active, 2=removed, 3=banned
    UNIQUE KEY uk_group_user (group_id, user_id)
);
```

**messages** + **offline_messages** — 聊天消息:
```sql
CREATE TABLE messages (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    sender_id BIGINT UNSIGNED NOT NULL,
    receiver_id BIGINT UNSIGNED DEFAULT NULL,  -- 私聊目标
    group_id BIGINT UNSIGNED DEFAULT NULL,      -- 群聊目标
    message_type TINYINT DEFAULT 0,             -- 0=text,1=image,2=file
    content TEXT NOT NULL,
    file_id BIGINT UNSIGNED DEFAULT NULL,
    created_at TIMESTAMP(3) DEFAULT CURRENT_TIMESTAMP(3)
);

CREATE TABLE offline_messages (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    message_id BIGINT UNSIGNED NOT NULL,
    receiver_id BIGINT UNSIGNED NOT NULL,
    delivered TINYINT DEFAULT 0
);
```

**files** — 文件传输:
```sql
CREATE TABLE files (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    uploader_id BIGINT UNSIGNED NOT NULL,
    file_name VARCHAR(255) NOT NULL,
    file_size BIGINT UNSIGNED NOT NULL,
    file_hash VARCHAR(64) NOT NULL,           -- SHA-256
    storage_path VARCHAR(512) NOT NULL,
    total_chunks INT UNSIGNED NOT NULL,
    uploaded_chunks INT UNSIGNED DEFAULT 0,
    status TINYINT DEFAULT 0                  -- 0=uploading,1=complete
);
```

**notifications** — 通知:
```sql
CREATE TABLE notifications (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    type TINYINT NOT NULL,
    content TEXT NOT NULL,
    related_id BIGINT UNSIGNED DEFAULT NULL,
    is_read TINYINT DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### Redis Key 设计

```
session:{token}              → user_id (STRING, TTL 24h)
user:sessions:{user_id}      → Set of tokens (SET)
user:online:{user_id}        → "1" (STRING, TTL 30s, 心跳刷新)
friend:list:{user_id}        → Set of friend IDs (SET)
friend:blocked:{user_id}     → Set of blocked IDs (SET)
group:members:{group_id}     → Hash {user_id: role} (HASH)
user:groups:{user_id}        → Set of group IDs (SET)
verify:{email_or_phone}      → code (STRING, TTL 300s)
file:transfer:{file_id}      → Hash {chunks, status}
```

---

## 实施阶段与时间线

| 阶段 | 内容 | 天数 | 累计 |
|------|------|------|------|
| **P0** | 项目初始化：CMake, proto 编译, common 库, SQL schema, .gitignore, build.sh | 2 | 2 |
| **P1** | 传输层：Socket RAII, RingBuffer, Epoller, Connection, Reactor, Acceptor | 4 | 6 |
| **P2** | 协议层：Codec (粘包处理), Message 工厂, Handler 抽象接口 | 3 | 9 |
| **P3** | 存储层：MySQL 连接池, Redis 连接池, 7 个 Repository | 3 | 12 |
| **P4** | 线程池：ThreadPool (packaged_task + future) | 1 | 13 |
| **P5** | 账号模块：注册/登录/登出/验证码/会话管理 + Handler 接入 | 4 | 17 |
| **P6** | 好友模块：添加/删除/屏蔽/在线状态推送 + 非好友私聊拦截 | 3 | 20 |
| **P7** | 聊天模块 + 心跳：私聊/群聊/历史/离线消息, timerfd 心跳 | 5 | 25 |
| **P8** | 群组模块：创建/解散/加入/审批/管理员/踢人/权限校验 | 4 | 29 |
| **P9** | 文件传输：发送/接收/断点续传/离线文件/SHA-256 校验 | 4 | 33 |
| **P10** | 主从 Reactor 升级：Master accept + 多 Slave epoll + eventfd 分发 | 2 | 35 |
| **P11** | TLS 加密：OpenSSL 集成, 非阻塞握手, 自签证书 | 2 | 37 |
| **P12** | 客户端：TCP 连接, stdin+socket epoll, 终端 UI, 命令解析 | 5 | 42 |
| **P13** | 测试 + 加固：单元测试, 集成测试, 边界测试, 崩溃防护 | 5 | 47 |
| **P14** | 文档：用户手册, 构建指南, 架构文档 | 3 | 50 |

**总计：约 50 个工作日 (10 周，单人开发)**

### 里程碑

| 里程碑 | 天数 | 验证标准 |
|--------|------|----------|
| **MVP** | Day 17 | 客户端可注册、登录、获取会话令牌 |
| **Alpha** | Day 25 | 两个客户端可实时收发消息 |
| **Beta** | Day 37 | 功能完整 (TLS + 文件 + 群组 + 主从) |
| **RC** | Day 47 | 全部测试通过，边界情况处理完毕 |
| **Release** | Day 50 | 文档齐全，可交付 |

---

## 关键设计模式

| 模式 | 应用位置 | 说明 |
|------|----------|------|
| **RAII** | Socket, MysqlPool, RedisPool | 防止资源泄漏 |
| **Reactor** | reactor.h/cpp | epoll 事件循环，每个 Slave 一个实例 |
| **Strategy** | IMessageHandler 接口 | 每种消息类型一个 Handler |
| **Observer** | SessionManager → 好友状态推送 | 解耦事件生产者和消费者 |
| **Connection Pool** | MysqlPool, RedisPool | RAII Guard 自动归还 |
| **Thread Pool** | ThreadPool | 业务逻辑从 Reactor 线程卸载 |
| **Ring Buffer** | RingBuffer<T> | 高效的半包组装和输出缓冲 |
| **Singleton** | Config | 全局唯一配置源 |

### 线程安全模型

```
Reactor 线程 (每个 Slave):
  - 独占 epoll 实例和 Connection map
  - 调用 on_readable() / on_writable()
  - 其他线程不直接操作 fd 或缓冲区
  - 将业务任务提交给 ThreadPool

ThreadPool Worker:
  - 执行业务逻辑 (Handler → Service → Repo)
  - 不直接写 Connection 缓冲区
  - 返回 Response → Reactor 入队输出

SessionManager (shared_mutex 保护):
  - shared_ptr<Connection> (Reactor 持有强引用)
  - weak_ptr<Connection> (SessionManager 持有弱引用)
  - 自动检测过期 weak_ptr 并清理
```

---

## 错误处理原则

- 服务端**绝不因非法输入崩溃**（硬性要求）
- 所有 Handler 在处理前验证所有字段
- 所有 Protobuf 反序列化包裹在 try-catch 中
- 所有网络读写处理 EAGAIN/EWOULDBLOCK
- MySQL/Redis 操作失败自动重试 (最多 3 次)
- 业务错误使用错误码返回，异常仅用于真正异常情况
- `std::optional` 用于可空返回值

---

## 依赖库

```bash
# Debian/Ubuntu 系统包
sudo apt install -y \
    protobuf-compiler libprotobuf-dev \
    libssl-dev \
    libgtest-dev libgmock-dev \
    libgoogle-glog-dev \
    libmysqlclient-dev \
    libhiredis-dev \
    cmake g++
```

---

## 验证策略

### 单元测试 (GTest)
- `test_buffer.cpp` — RingBuffer 边界/满/空
- `test_codec.cpp` — 编解码/半包/超长/往返
- `test_account.cpp` — 注册/登录/注销用户重新注册拒绝
- `test_friend.cpp` — 非好友私聊拦截/在线状态变更
- `test_group.cpp` — 被踢成员发消息拦截/创建者不可被踢
- `test_chat.cpp` — 离线消息投递/历史分页
- `test_heartbeat.cpp` — Ping/Pong/超时断开

### 集成测试
- 单客户端：注册 → 登录 → 发消息 → 登出 全流程
- 双客户端：同时在线实时消息交换
- 离线投递：A 在线发消息给离线 B，B 登录后收到
- 群聊：3 人入群发消息，被踢成员收不到
- 文件：发送 → 断点续传 → SHA-256 校验
- 心跳重连：模拟网络中断后恢复

### 崩溃防护测试
- 畸形 Protobuf / 错误消息类型 / 超大消息
- 过期/伪造 Session Token
- 并发好友请求竞态
- 群组解散时成员正在聊天
- 已注销用户名重新注册
- 被踢成员发消息

---

## 实施顺序

依赖关系决定了实施顺序（后面的依赖前面的）：

```
P0 (基础) ──► P1 (传输) ──► P2 (协议) ──► P5 (账号) ──► P6 (好友) ──► P7 (聊天) ──► P8 (群组) ──► P9 (文件)
              \              \              \              \              \
               P3 (存储) ─────┴──────────────┴──────────────┴──────────────┴──► P10 (主从) ──► P11 (TLS) ──► P13 (测试) ──► P14 (文档)
               P4 (线程池) ──┘                                                          
                                                                        P12 (客户端，P2 后即可并行开始)
```

**可并行化的工作：**
- P2 (协议) 和 P3 (存储) 可并行
- P4 (线程池) 独立，可在 P5 前任意时间完成
- P12 (客户端) 在 P2 完成后即可并行开发
- 单元测试应与对应模块同步编写（而非全部留到 P13）

---

## 关键文件（优先级最高）

1. `transport/include/reactor.h` — 服务端心脏：epoll 事件循环、连接生命周期、心跳定时器
2. `protocol/include/codec.h` — 消息组帧引擎：TCP 流重组、半包处理
3. `business/chat/include/chat_service.h` — 最复杂的业务模块：联动所有其它模块
4. `storage/include/user_repo.h` — 所有数据访问的基础：软删除、重注册安全检查
5. `client/include/client_app.h` — 客户端编排器：stdin+socket epoll 循环
