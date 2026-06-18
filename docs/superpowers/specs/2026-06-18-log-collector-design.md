# Log Collector - 网络日志收集系统设计文档

## 概述

基于 Linux 平台的网络日志收集系统，使用 C99 语法开发。作为日志服务端接收远程客户端通过 TCP/UDP 推送的 syslog 格式日志，按客户端 IP 和日期分文件存储到本地磁盘。

### 涉及技术

守护进程、进程池、共享内存(mmap)、信号量、互斥锁、socket、epoll、CMake 构建系统。

---

## 架构

### 整体架构

```
┌─────────────────────────────────────────────────────────┐
│                    Master Process (守护进程)              │
│  ┌─────────────┐  ┌──────────────────────────────────┐  │
│  │  Signal      │  │  epoll Event Loop                │  │
│  │  Handler     │  │  - TCP 监听端口                   │  │
│  │  (SIGTERM,   │  │  - UDP 监听端口                   │  │
│  │   SIGHUP,    │  │  - accept TCP 新连接              │  │
│  │   SIGCHLD)   │  │  - 接收 TCP/UDP 日志数据          │  │
│  └─────────────┘  │  - 写入共享内存环形缓冲区          │  │
│                   └──────────────────────────────────┘  │
│                          │                               │
│                   ┌──────┴──────┐                        │
│                   │  共享内存    │  (mmap + mutex + sem)  │
│                   │  环形缓冲区  │                        │
│                   └──────┬──────┘                        │
│         ┌────────────────┼────────────────┐              │
│    ┌────┴────┐      ┌────┴────┐      ┌────┴────┐        │
│    │ Worker 1│      │ Worker 2│ ...  │ Worker N│        │
│    │ 读取缓冲│      │ 读取缓冲│      │ 读取缓冲│        │
│    │ 解析日志│      │ 解析日志│      │ 解析日志│        │
│    │ 写入文件│      │ 写入文件│      │ 写入文件│        │
│    └─────────┘      └─────────┘      └─────────┘        │
└─────────────────────────────────────────────────────────┘
```

### 数据流

```
远程客户端 --[TCP/UDP]--> Master(epoll) --[共享内存环形缓冲区]--> Worker --[write]--> /var/log/collector/<client_ip>/YYYY-MM-DD.log
```

### 角色分工

| 角色 | 职责 |
|------|------|
| Master | 守护进程化、epoll 事件循环、TCP/UDP 监听、接收日志数据、写入共享内存环形缓冲区、管理 Worker 进程池、信号处理 |
| Worker | 从共享内存环形缓冲区读取日志、解析 syslog 格式、按客户端 IP 和日期写入文件 |

---

## 共享内存环形缓冲区

### 内存布局

```
┌──────────────────────────────────────────────────────────┐
│                    共享内存布局 (mmap)                     │
├──────────────────────────────────────────────────────────┤
│  Header (元数据)                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ magic: uint32_t          (魔数校验)                  │  │
│  │ version: uint32_t        (版本号)                    │  │
│  │ buffer_size: uint64_t    (缓冲区总大小)              │  │
│  │ slot_size: uint64_t      (每个槽位大小)              │  │
│  │ slot_count: uint64_t     (槽位总数)                  │  │
│  │ write_pos: uint64_t      (写指针, Master使用)        │  │
│  │ read_pos: uint64_t       (读指针, Worker使用)        │  │
│  │ mutex: pthread_mutex_t   (跨进程互斥锁)              │  │
│  │ sem_free: sem_t          (空闲槽位数)                │  │
│  │ sem_used: sem_t          (已用槽位数)                │  │
│  └────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────┤
│  Slot 0 ── 固定大小槽位 (约 4KB)                           │
│  Slot 1                                                   │
│  Slot 2                                                   │
│  ...                                                      │
│  Slot N-1                                                 │
└──────────────────────────────────────────────────────────┘
```

### 槽位结构

```c
typedef struct {
    struct sockaddr_storage client_addr;  /* 客户端地址 (128 bytes) */
    uint8_t  protocol;                    /* 0=TCP, 1=UDP */
    uint32_t data_len;                    /* 实际日志长度 */
    uint64_t timestamp;                   /* 接收时间戳 (epoch秒) */
    char     data[];                      /* 柔性数组: 日志内容, 实际长度 = slot_size - header_size */
} log_slot_t;
```

### 生产者/消费者模型

```
Master (生产者):                    Worker (消费者):
  sem_wait(sem_free)                  sem_wait(sem_used)
  pthread_mutex_lock(&mutex)          pthread_mutex_lock(&mutex)
  写入 slot[write_pos]                读取 slot[read_pos]
  write_pos = (write_pos+1) % N       read_pos = (read_pos+1) % N
  pthread_mutex_unlock(&mutex)        pthread_mutex_unlock(&mutex)
  sem_post(sem_used)                  sem_post(sem_free)
```

- 互斥锁使用 `PTHREAD_PROCESS_SHARED` 属性支持跨进程
- 信号量使用 POSIX 命名信号量 (`sem_open`)，初始值在共享内存创建时设置
- 如果缓冲区满（sem_free 为 0），Master 丢弃最旧的日志（覆盖检测，不阻塞网络接收）

---

## 进程管理

### 守护进程化

```
启动 → fork() → 父进程退出
     → setsid() (创建新会话)
     → fork() → 子进程退出 (确保不是会话首进程)
     → chdir("/")
     → umask(0)
     → 关闭 stdin/stdout/stderr，重定向到 /dev/null
     → 写 PID 文件 /var/run/log-collector.pid
     → 信号处理注册
     → 初始化共享内存
     → fork Worker 进程池
     → 进入 epoll 事件循环
```

### 进程池

- Worker 数量：默认 CPU 核心数，可通过配置调整
- Master 通过 `fork()` 创建 Worker 进程
- Worker 退出时 Master 收到 `SIGCHLD`，记录并重新 fork 补充
- 限制重启频率：最多 3 次/分钟，防止频繁崩溃

### 信号处理

| 信号 | 处理 |
|------|------|
| `SIGTERM` / `SIGINT` | 优雅关闭：通知 Worker 退出 → 等待回收 → 清理共享内存 → 删除 PID 文件 → 退出 |
| `SIGHUP` | 重新加载配置文件 |
| `SIGCHLD` | Worker 退出通知，记录并重新 fork |
| `SIGPIPE` | 忽略 |

### 优雅关闭流程

```
SIGTERM →
  1. 设置 shutdown_flag = 1
  2. 关闭监听 socket (不再 accept)
  3. 向 sem_used 发送 N 个哨兵值 (data_len=0 表示退出)
  4. 等待所有 Worker 退出 (waitpid, 超时 10 秒)
  5. 超时则 SIGKILL 剩余 Worker
  6. munmap 共享内存
  7. shm_unlink 共享内存名
  8. 删除 PID 文件
  9. 退出
```

---

## 网络 I/O

### epoll 事件循环

- 使用边缘触发 (EPOLLET) 模式
- 监听 TCP socket：`EPOLLIN` 事件，accept 新连接，注册客户端 fd
- 监听 UDP socket：`EPOLLIN` 事件，直接 recvfrom 读取数据报
- TCP 客户端 fd：`EPOLLIN` | `EPOLLRDHUP`，读取数据直到 EAGAIN
- 单次 `epoll_wait` 超时：1000ms（允许定期检查 shutdown_flag）

### TCP 连接管理

- 最大连接数：可配置，默认 1024
- 每个 TCP 客户端维护接收缓冲区（栈上或堆上）
- 检测到行结束符 `\n` 时，将完整日志消息写入共享内存
- 连接断开时清理 fd 和缓冲区

### UDP 处理

- 每个 UDP 数据报作为一条独立日志消息
- 直接写入共享内存，无需缓冲

---

## 日志存储

### 目录结构

```
/var/log/collector/
├── 192.168.1.100/
│   ├── 2026-06-18.log
│   └── 2026-06-17.log
├── 10.0.0.55/
│   └── 2026-06-18.log
└── collector.pid
```

### 存储规则

| 项目 | 规则 |
|------|------|
| 目录 | 按客户端 IP 分目录，自动创建 |
| 文件名 | `YYYY-MM-DD.log` |
| 写入方式 | Worker 打开当天文件，`write()` 追加写入 |
| 跨天处理 | Worker 检测日期变更，关闭旧文件，打开新文件 |

### Worker 文件句柄管理

```
每个 Worker 维护:
  current_ip: char[64]        ← 当前客户端 IP
  current_fd: int             ← 当前文件描述符
  current_date: char[16]      ← 当前日期 YYYY-MM-DD
  current_size: uint64_t      ← 当前文件已写入字节

收到新日志时:
  if (ip 变了 || 日期变了):
    关闭旧 fd
    打开/创建新文件 (目录自动创建)
    更新元信息
  write(fd, formatted_log, len)
```

---

## Syslog 协议解析

### 输入格式

支持 BSD syslog 风格（RFC 3164）：

```
<PRI>Mon DD HH:MM:SS hostname program[pid]: message
```

或简化通用格式：

```
<PRI>timestamp hostname program: message
```

### 输出格式

```
2026-06-18T14:32:05+08:00 192.168.1.100 hostname program[pid]: [level] message
```

### 级别映射

| 数字 (severity) | 标签 |
|-----------------|------|
| 0 | emerg |
| 1 | alert |
| 2 | crit |
| 3 | err |
| 4 | warning |
| 5 | notice |
| 6 | info |
| 7 | debug |

---

## 配置管理

### 配置文件

格式：INI 风格 key=value，路径：`/etc/log-collector.conf`

```ini
[server]
listen_addr = 0.0.0.0
tcp_port = 5140
udp_port = 5140
max_connections = 1024

[worker]
worker_count = 4
buffer_slot_size = 4096
buffer_slot_count = 1024

[storage]
log_dir = /var/log/collector
```

### 默认值

所有参数都有合理默认值，配置文件不存在时使用默认值运行。

---

## CMake 项目结构

```
log_collector/
├── CMakeLists.txt
├── src/
│   ├── main.c              # 入口，解析参数，启动守护进程
│   ├── daemon.c            # 守护进程化
│   ├── daemon.h
│   ├── config.c            # 配置解析
│   ├── config.h
│   ├── shm_buffer.c        # 共享内存环形缓冲区
│   ├── shm_buffer.h
│   ├── master.c            # Master 进程 (epoll + accept + 接收)
│   ├── master.h
│   ├── worker.c            # Worker 进程 (读取缓冲 + 解析 + 写文件)
│   ├── worker.h
│   ├── log_parser.c        # syslog 格式解析
│   ├── log_parser.h
│   ├── file_writer.c       # 文件写入
│   ├── file_writer.h
│   ├── signal_handler.c    # 信号处理
│   └── signal_handler.h
├── include/
│   └── common.h            # 公共类型定义、常量
├── tests/
│   ├── CMakeLists.txt
│   ├── test_shm_buffer.c
│   ├── test_log_parser.c
│   └── test_file_writer.c
└── conf/
    └── log-collector.conf.example
```

### 编译要求

- C 标准：C99 (`-std=c99`)
- 链接库：`pthread`、`rt` (POSIX 信号量)
- 编译器：GCC 或 Clang
- 平台：Linux only

---

## 错误处理

| 场景 | 处理方式 |
|------|----------|
| 共享内存创建失败 | 致命错误，退出 |
| Worker fork 失败 | 记录日志，重试 |
| 磁盘写满 | Worker 丢弃日志，记录警告 |
| 缓冲区满 | Master 丢弃最旧日志 |
| 配置文件解析错误 | 使用默认值，记录警告 |
| 客户端连接数超限 | 拒绝新连接，关闭 fd |
| Worker 崩溃 | Master 自动重启 |
