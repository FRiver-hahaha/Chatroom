-- ============================================================
-- ChatRoom 数据库 Schema (MySQL)
-- 使用方法: mysql -u chatroom -p'Ch@tRoom2026.Dev' chatroom < schema.sql
-- ============================================================

CREATE DATABASE IF NOT EXISTS chatroom
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE chatroom;

-- ============================================================
-- 1. 用户表
-- ============================================================
CREATE TABLE IF NOT EXISTS users (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(64)   NOT NULL UNIQUE,
    password_hash VARCHAR(256)  NOT NULL COMMENT 'SHA-256 哈希',
    salt          VARCHAR(64)   NOT NULL,
    nickname      VARCHAR(128)  NOT NULL,
    email         VARCHAR(256)  DEFAULT NULL,
    phone         VARCHAR(32)   DEFAULT NULL,
    avatar_url    VARCHAR(512)  DEFAULT NULL,
    status        TINYINT       DEFAULT 1 COMMENT '0=inactive, 1=active, 2=banned',
    created_at    TIMESTAMP     DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP     DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_username (username),
    INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- 2. 好友关系表（双向：一行表示一条关系）
-- ============================================================
CREATE TABLE IF NOT EXISTS friendships (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id     BIGINT UNSIGNED NOT NULL,
    friend_id   BIGINT UNSIGNED NOT NULL,
    is_blocked  BOOLEAN         DEFAULT FALSE,
    created_at  TIMESTAMP       DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id)   REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (friend_id) REFERENCES users(id) ON DELETE CASCADE,
    UNIQUE KEY unique_friendship (user_id, friend_id),
    INDEX idx_user (user_id),
    INDEX idx_friend (friend_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- 3. 群组表
-- ============================================================
CREATE TABLE IF NOT EXISTS chat_groups (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    group_name   VARCHAR(128) NOT NULL,
    description  TEXT         DEFAULT NULL,
    owner_id     BIGINT UNSIGNED NOT NULL,
    is_public    BOOLEAN      DEFAULT TRUE,
    member_count INT          DEFAULT 1,
    created_at   TIMESTAMP    DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_owner (owner_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- 4. 群组成员表
-- ============================================================
CREATE TABLE IF NOT EXISTS group_members (
    id        BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    group_id  BIGINT UNSIGNED NOT NULL,
    user_id   BIGINT UNSIGNED NOT NULL,
    role      ENUM('owner', 'admin', 'member') DEFAULT 'member',
    joined_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (group_id) REFERENCES chat_groups(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id)  REFERENCES users(id) ON DELETE CASCADE,
    UNIQUE KEY unique_member (group_id, user_id),
    INDEX idx_group (group_id),
    INDEX idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- 5. 消息表（Protobuf 序列化存储在 body 字段）
-- ============================================================
CREATE TABLE IF NOT EXISTS messages (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    sender_id    BIGINT UNSIGNED NOT NULL,
    target_id    BIGINT UNSIGNED DEFAULT NULL COMMENT '私聊接收者 user_id',
    group_id     BIGINT UNSIGNED DEFAULT NULL COMMENT '群聊 group_id',
    message_type INT             NOT NULL COMMENT 'MessageType 枚举值',
    body         BLOB            DEFAULT NULL COMMENT 'Protobuf 序列化的消息体',
    is_read      BOOLEAN         DEFAULT FALSE,
    created_at   TIMESTAMP       DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_sender      (sender_id),
    INDEX idx_target      (target_id),
    INDEX idx_group       (group_id),
    INDEX idx_created_at  (created_at),
    INDEX idx_sender_target (sender_id, target_id),
    INDEX idx_group_time  (group_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- 6. 文件表
-- ============================================================
CREATE TABLE IF NOT EXISTS files (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    file_name   VARCHAR(256)   NOT NULL,
    file_size   BIGINT UNSIGNED NOT NULL,
    file_path   VARCHAR(512)   NOT NULL COMMENT '服务端存储路径',
    mime_type   VARCHAR(128)   DEFAULT NULL,
    uploader_id BIGINT UNSIGNED NOT NULL,
    target_id   BIGINT UNSIGNED DEFAULT NULL COMMENT '接收者 user_id 或 group_id',
    is_group_file BOOLEAN       DEFAULT FALSE,
    created_at  TIMESTAMP      DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (uploader_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_uploader (uploader_id),
    INDEX idx_target (target_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
