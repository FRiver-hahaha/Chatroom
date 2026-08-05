# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

Build outputs: `build/server` and `build/client`.

CMake exports `compile_commands.json` to the build directory (symlink or copy to repo root for IDE support).

### Dependencies

- **Protobuf** (protobuf) — message serialization
- **liburing** — io_uring async I/O
- **hiredis** — Redis client
- **mysqlclient** (libmysqlclient) — MySQL client
- **OpenSSL** (libssl / libcrypto) — SHA-256 hashing, TLS (planned)

The two targets have different dependency sets: `server` links all of the above; `client` only needs Protobuf, OpenSSL, and pthreads.

## Project Architecture

This is a C++17 chatroom application: a **Proactor-model TCP server** using `io_uring` + a **CLI test client**. Communication uses a **4-byte big-endian length prefix** followed by a **Protobuf `ChatMessage`**.

### Layer stack

```
main.cpp          — entry point, wires callbacks, starts Server
network/          — io_uring event loop, thread pool, connection management
service/          — Protobuf ↔ internal Message parsing, dispatch to business handlers
storage/          — MySQL (via connection pool) + Redis (session/online/caching)
client/           — standalone CLI client with interactive menus
```

### Message lifecycle

1. `Server` reads raw bytes via io_uring → accumulates in `Connection::recv_buffer`
2. Frame decoder strips the 4-byte length prefix, extracts complete Protobuf payloads
3. Complete frames are dispatched to the **thread pool** (`network/threadPool.hpp`)
4. Worker thread calls `MessageParser::parse()` → Protobuf → internal `Message` struct
5. `Connection::db_queryer_->query()` runs the business logic (validates permissions, hits MySQL/Redis)
6. `Connection::dispatcher_->dispatch()` serializes the response and sends via `send_to_async()`

### Message type numbering convention

- **Request** = odd number, **Response** = even number (request + 1)
- Ranges: `1–99` account, `100–199` friend, `200–299` group, `300–399` chat, `420–439` file transfer

### Key classes and their roles

| Class | File | Role |
|---|---|---|
| `Server` | `network/server.hpp` | io_uring event loop, accept/recv/send/timeout handling, user_id→fd mapping |
| `Connection` | `network/server.hpp` | Per-connection state: fd, recv buffer (frame decoder), send queue, session info, owns `DatabaseQueryer` + `MessageDispatcher` |
| `ThreadPool` | `network/threadPool.hpp` | Generic thread pool; business logic runs here, not in the io_uring thread |
| `MessageParser` | `service/MessageParser.hpp` | Pure-static: `parse()` Protobuf → `Message`, `serialize_response()` `Message` + `QueryResult` → Protobuf response |
| `MessageDispatcher` | `service/MessageDispatcher.hpp` | Routes by type range, handles notifications (friend online, group events, file transfer notify) |
| `DatabaseQueryer` | `storage/DatabaseQueryer.hpp` | Business logic: validates permissions, calls `StorageManager`, returns `QueryResult` |
| `StorageManager` | `storage/StorageManager.hpp` | All DB/Redis operations; owns `MySQLConnectionPool` + `redisContext*` |
| `MySQLConnectionPool` | `storage/StorageManager.hpp` | Blocking connection pool (8 connections), auto-reconnects on `mysql_ping` failure |

### io_uring design

- Uses **SQPOLL** mode (`IORING_SETUP_SQPOLL`) with 1-second idle timeout
- Tags: `ACCEPT_TAG` for multishot accept, `TIMEOUT_TAG` for 30-second heartbeat timer, `WAKEUP_TAG` for eventfd cross-thread wake
- `send_to_async()` (called from thread pool workers) pushes to a `pending_sends_` queue and writes to `eventfd` to wake the io_uring loop, which then flushes pending sends in `flush_pending_sends()`
- Heartbeat: 30s interval via `io_uring_prep_timeout`, connections timed out at 90s idle (`HEARTBEAT_TIMEOUT`)

### Storage: MySQL + Redis split

- **MySQL**: Users, friendships, chat_groups, group_members, messages tables — persistent structured data
- **Redis**: Sessions (`chatroom:session:<uid>` hash), tokens (`chatroom:token:<token>` → uid), online set (`chatroom:online`), offline messages (`chatroom:offline_msg:<uid>` list), group join requests, file transfer chunk tracking
- Soft-delete for users (`status=3`) and groups (`member_count=0`) — IDs are reused on re-registration/re-creation

### File transfer (420–439)

- Chunked upload (64KB chunks), SHA-256 hash verification per chunk
- Resume support: chunks tracked in Redis sets (`chatroom:transfer:<id>:sender_chunks` / `receiver_chunks`)
- Chunk data stored on disk under `/tmp/chatroom_files/transfer_<id>/chunk_<seq>`
- Flow: A sends `FILE_SEND_REQ` → server notifies B (`FILE_TRANSFER_NOTIFY`) → B accepts → B pulls chunks via `FILE_RECEIVE_CHUNK_REQ`

## Running

```bash
# Server (needs MySQL + Redis running)
./build/server
# Default port: 8080. Hardcoded DB creds in main.cpp:4-5.

# CLI client
./build/client [host] [port]
# Interactive menu-driven: login → friend/group/chat/file sub-menus.

# DB inspection tool
./scripts/dump_db.sh          # all users
./scripts/dump_db.sh <uid>    # detailed view for one user
```

## Development notes

- **Protobuf regeneration**: The `.proto` file is `chatroom.proto` at the repo root. Generated `.pb.h`/`.pb.cc` land in `build/generated/`. After editing the proto, reconfigure CMake or just rebuild — CMake tracks the dependency.
- **No dedicated logging library**: Logs go to `std::cout`/`std::cerr` prefixed with `[ClassName]`. The `log()` free functions in `threadPool.cpp` are thread-safe but rarely used elsewhere.
- **`Connection` needs both `db_queryer_` and `dispatcher_`** to be non-null before processing messages; they are initialized in `Connection::init_business()` which is called from `Server::handle_accept()` only if `storage_` is set.
- **Thread safety**: `send_to_async()` and `flush_pending_sends()` share `pending_sends_` via mutex. `user_to_fd_` has its own mutex. Redis operations are serialized with `redis_mutex_`.
- **Client is self-contained**: `client/client.cpp` is a single ~1500-line file with its own frame decoder, Protobuf handling, and menu UI. It links against the same proto sources.
- **No tests currently exist** — verifying changes means running the server and interacting via the CLI client.

## Working Guidelines
- When you are executing a task and find that any execution details are unclear, you must ask me questions rather than making assumptions or decisions on your own.

- If there are still unclear execution details after I respond, you need to follow up with further questions until you understand all the details.

- You may review any code you need to understand before asking questions, and only ask me after you have understood the code logic.

- Please minimize comments in the code you generate, and do not modify the comments I have originally written.