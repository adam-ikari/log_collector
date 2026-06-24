# Log Collector — Linux 日志收集系统

基于 C99 开发的 Linux 网络日志收集系统，作为 syslog 服务端接收远程客户端通过 TCP/UDP 推送的日志，按客户端 IP 和日期分文件存储。

本项目是一个教学项目，综合运用了 Linux 系统编程的核心技术：守护进程、进程池、POSIX 共享内存、信号量、互斥锁、epoll、socket。

## 涉及技术

| 技术 | 对应模块 | 说明 |
|------|---------|------|
| 守护进程 | `daemon.c` | double-fork + setsid 脱离终端 |
| 信号处理 | `signal_handler.c` | signal() + siginterrupt() 优雅关闭 |
| POSIX 共享内存 | `shm_buffer.c` | shm_open + mmap + 环形缓冲区 |
| 信号量 | `shm_buffer.c` | sem_t 实现生产者消费者 |
| 互斥锁 | `shm_buffer.c` | PTHREAD_PROCESS_SHARED 跨进程锁 |
| epoll | `master.c` | EPOLLET 边缘触发 I/O 多路复用 |
| TCP/UDP Socket | `master.c` | 非阻塞 socket + accept + 行缓冲 |
| 进程池 | `master.c` / `worker.c` | fork Worker 池 + 崩溃自动重启 |
| syslog 解析 | `log_parser.c` | PRI 提取 + severity 映射 |
| 文件 I/O | `file_writer.c` | 按 IP 分目录 + 按日期分文件 |
| C/C++ 兼容 | `common.h` 头文件 | extern "C" + 无柔性数组 |

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                    Master Process (守护进程)              │
│  ┌─────────────┐  ┌──────────────────────────────────┐  │
│  │  Signal      │  │  epoll Event Loop（EPOLLET）     │  │
│  │  Handler     │  │  - TCP/UDP 监听 5140 端口        │  │
│  │  (SIGTERM,   │  │  - accept 新连接                   │  │
│  │   SIGHUP,    │  │  - TCP 行缓冲拆分                 │  │
│  │   SIGCHLD)   │  │  - UDP 数据报接收                 │  │
│  └─────────────┘  │  - shm_produce → 共享内存         │  │
│                   └──────────────────────────────────┘  │
│                          │                               │
│                   ┌──────┴──────┐                        │
│                   │  POSIX 共享内存                     │
│                   │  shm_open + mmap                    │
│                   │  环形缓冲区                          │
│                   │  mutex + sem_free + sem_used        │
│                   └──────┬──────┘                        │
│         ┌────────────────┼────────────────┐              │
│    ┌────┴────┐      ┌────┴────┐      ┌────┴────┐        │
│    │ Worker 1│      │ Worker 2│ ...  │ Worker 4│        │
│    │ shm_consume   │ shm_consume   │ shm_consume  │
│    │ 解析 syslog   │ 解析 syslog   │ 解析 syslog  │
│    │ 写日志文件    │ 写日志文件    │ 写日志文件    │
│    └─────────┘      └─────────┘      └─────────┘        │
└─────────────────────────────────────────────────────────┘
```

## 数据流

```
远程客户端 --[TCP/UDP]--> Master(epoll) --[shm_produce]--> 共享内存环形缓冲区 --[shm_consume]--> Worker --[write]--> /tmp/log_collector_test/<client_ip>/YYYY-MM-DD.log
```

## 快速开始

```bash
# 安装系统依赖（Ubuntu/Debian）
sudo apt-get install -y build-essential cmake pkg-config libsystemd-dev

# 编译
mkdir build && cd build
cmake .. && make

# 前台运行
./log_collector -f

# 发送测试日志
echo "<13>test message" | nc -u 127.0.0.1 5140
echo "<14>TCP test"       | nc -q1 127.0.0.1 5140

# 查看日志
cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log

# 运行 E2E 测试
cd .. && bash tests/e2e_test.sh
```

## 项目结构

```
log_collector/
├── CMakeLists.txt
├── include/
│   └── common.h              # 公共类型定义、常量、extern "C"
├── src/
│   ├── main.c                # 入口：配置初始化、启动流程串联
│   ├── config.h              # 编译期配置（改参数改这里）
│   ├── daemon.c/h            # 守护进程化（double-fork + setsid）
│   ├── signal_handler.c/h    # 信号处理（signal + siginterrupt）
│   ├── master.c/h            # Master：epoll 事件循环 + Worker 进程池
│   ├── worker.c/h            # Worker：消费日志、解析、写文件
│   ├── shm_buffer.c/h        # POSIX 共享内存环形缓冲区
│   ├── log_parser.c/h        # syslog PRI 解析
│   └── file_writer.c/h       # 按 IP+日期 写日志文件
└── tests/
    └── e2e_test.sh            # 端到端集成测试（9 场景 / 24 断言）
```

## 配置

所有配置项在 `src/config.h` 中定义为编译期常量：

```c
#define CFG_LISTEN_ADDR       "0.0.0.0"
#define CFG_TCP_PORT          5140
#define CFG_UDP_PORT          5140
#define CFG_MAX_CONNECTIONS   1024
#define CFG_WORKER_COUNT      4
#define CFG_SLOT_SIZE         4096
#define CFG_SLOT_COUNT        1024
#define CFG_LOG_DIR           "/tmp/log_collector_test"
#define CFG_PID_FILE          "/tmp/log-collector.pid"
```

修改后重新 `make` 即可生效。

## 教学文章

按开发顺序阅读，每篇对应一个模块：

| # | 文章 | 内容 | 涉及技术 |
|---|------|------|---------|
| 1 | [搭骨架](docs/tutorial/01-project-setup.md) | CMake、common.h、配置系统 | CMake、C99 指定初始化器 |
| 2 | [守护进程 + 信号](docs/tutorial/02-daemon-and-signals.md) | double-fork、signal()、siginterrupt() | fork、setsid、信号处理 |
| 3 | [epoll + TCP/UDP](docs/tutorial/03-epoll-network.md) | EPOLLET 边缘触发、行缓冲、UDP 数据报 | epoll、非阻塞 Socket |
| 4 | [POSIX 共享内存](docs/tutorial/04-shm-ringbuffer.md) | shm_open + mmap + mutex + semaphore | 共享内存、互斥锁、信号量 |
| 5 | [进程池与 Worker](docs/tutorial/05-process-pool.md) | fork 进程池、syslog 解析、文件存储 | 进程池、文件 I/O |
| 6 | [测试与调试](docs/tutorial/06-testing-and-debugging.md) | 单元测试、E2E、strace、GDB | CTest、strace、GDB 多进程 |

## 测试

```bash
# E2E 测试（9 场景 / 24 断言）
bash tests/e2e_test.sh
```

## C/C++ 兼容

所有头文件均包含 `extern "C"` 宏，C++ 编译器可直接链接。数据结构使用固定大小数组 `char data[4096]`，无需指针算术。

## 许可证

教学用途，无许可证限制。
