friver@FRiverscomputer ~/C/build (main)> ./server                        (base) 
[MySQLPool] Initialized with 8 connections
[StorageManager] Redis connection failed: Connection refused
[Main] 数据库连接失败，将以 mock 模式运行
(init)初始化线程池,使用 4 个线程
(worker)工作线程
(worker)工作线程
(worker)工作线程
(worker)工作线程
[Server] io_uring ready: SQ=2048, CQ=4096, SQPOLL=on
[Server] Listening on port 8080
[Server] Started on port 8080
========================================
  ChatRoom 服务器已启动
  端口: 8080
  按 Ctrl+C 停止
========================================
[Server] New client: fd=13
[Callback] 新连接 fd=13
[Callback] 连接 fd=13 已关闭 (user=)
[Server] Connection 13 closed, remaining: 0
[Server] New client: fd=13
[Callback] 新连接 fd=13
[Server] New client: fd=14
[Callback] 新连接 fd=14
[Server] Recv 50 bytes from fd 14
[线程池] 处理 46 字节来自 fd 14
[Pipeline] 收到消息: REGISTER_REQ 来自 fd=14 user=
[MessageDispatcher] Dispatching: REGISTER_REQ


river@FRiverscomputer ~/C/build (main)> ./client                        (base) 
╔══════════════════════════════════════════╗
║       ChatRoom 命令行测试客户端           ║
╚══════════════════════════════════════════╝
  服务端: 127.0.0.1:8080
  用法: ./client [host] [port]

[信息] 已连接到 127.0.0.1:8080

========== 主菜单 (未登录) ==========
  1. 登录
  2. 注册
  0. 退出
> 2
  用户名: 1203987745@qq.com
  密码: Jiayang012
  昵称: ljy


[错误] 未收到注册响应

========== 主菜单 (未登录) ==========
  1. 登录
  2. 注册
  0. 退出
>

这是我的错误日志，我注册了之后，服务端和客户端都显示没有成功，但是数据库上显示我已经成功了，有信息，然后我重启之后登录也是一样不成功。