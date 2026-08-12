## 前言

设计服务器的过程中难免会遇到一些思路上的问题，比如不知道某个函数的作用，昨天写了一个功能，第二天忘记了怎么写，或者某个类它和其他类的耦合关系等等。由于先在聊天室的开发已经接近尾声，当前网络层，存储层，业务层几乎完成，前端页面也基本完整，正在测试阶段。而考虑到正在进行代码开发，下面将包含了当前聊天室在代码层面的可使用资源，以及完整交互函数流程图。

## 当前已有的资源

### 结构体

1. Connection: 从网络层视角，来记录客户端的连接信息
2. Message: 在业务层方面，定义了一个发送的消息
3. UserSession: 面向存储层，定义了用户的当前状态，为将来查询数据库提供当前用户的状态信息
4. QueryResult: 保存查询结果，记录查询者，确定业务消息是什么
5. FriendInfo,GroupInfo,GroupMember,MessageHistory,FileInfo: 记录当前业务信息，配合QueryResult
6. ConnInfo: 记录连接数据库的用户信息
7. TransferInfo: 描述文件传输任务的信息

### 类

1. Server: 描述了当前服务端的网络层架构，使用io_uring
2. ThreadPool: 线程池，用来处理on_recv_接收到的业务处理信息
3. MessageParser: 消息解析器，用来进行消息的解析
4. DatabaseQueryer: 数据库查询器，依据用户状态和消息类型查询数据，并且返回当前查询结果
5. MessageDispatcher: 消息分发器，用来进行消息的分发
7. MySQLConnectionPool: 数据库连接池，提前建立好连接，供用户进行快速查询
8. StorageManager: 在存储层真正查询数据库的工具

### 枚举

1. MessageType: 定义消息类型
2. MessageFlag：标注当前消息的依赖
3. SessionState: 定义当前用户的信息，配合结构体UserSession

