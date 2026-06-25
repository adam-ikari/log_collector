# Log Collector — Linux 网络日志收集系统

基于 C99 开发的 Linux 网络日志收集系统，作为 syslog 服务端接收远程客户端通过 TCP/UDP 推送的日志，按客户端 IP 和日期分文件存储。

## 涉及技术

守护进程、进程池、共享内存(mmap)、信号量、互斥锁、socket、epoll、CMake

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                    Master Process (systemd Type=simple)        │
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

## 数据流

```
远程客户端 --[TCP/UDP]--> Master(epoll) --[共享内存环形缓冲区]--> Worker --[write]--> /tmp/log_collector_test/<client_ip>/YYYY-MM-DD.log
```

## 快速开始

```bash
# 编译
mkdir build && cd build
cmake .. && make

# 前台运行
./log_collector

# 发送测试日志
echo "<13>test message" | nc -u 127.0.0.1 5140
echo "<14>TCP test"       | nc -q1 127.0.0.1 5140

# 查看日志
cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log
```

## 项目结构

```
log_collector/
├── CMakeLists.txt
├── include/
│   └── common.h              # 公共类型、常量
├── src/
│   ├── main.c                # 入口
│   ├── config.h              # 编译期配置
│   ├── signal_handler.c/h    # 信号处理
│   ├── master.c/h            # Master 进程 (epoll + 进程池)
│   ├── worker.c/h            # Worker 进程
│   ├── shm_buffer.c/h        # 共享内存环形缓冲区
│   ├── log_parser.c/h        # syslog 格式解析
│   └── file_writer.c/h       # 文件写入
├── systemd/
│   └── log-collector.service
├── tests/
│   └── e2e_test.sh
└── docs/
    └── tutorial/              # 教学文章
```

## 教学文章

按开发顺序阅读：

| 序号 | 文章                                                        | 内容                            |
| ---- | ----------------------------------------------------------- | ------------------------------- |
| 1    | [搭骨架](docs/tutorial/01-project-setup.md)                 | CMake、common.h、配置系统       |
| 2    | [变成守护进程](docs/tutorial/02-daemon-and-signals.md)      | 信号处理、systemd 服务、main.c  |
| 3    | [接收网络日志](docs/tutorial/03-epoll-network.md)           | epoll + TCP/UDP                 |
| 4    | [共享内存环形缓冲区](docs/tutorial/04-shm-ringbuffer.md)    | mmap + 信号量 + 互斥锁          |
| 5    | [进程池与 Worker](docs/tutorial/05-process-pool.md)         | fork 进程池、日志解析、文件存储 |
| 6    | [串联测试与调试](docs/tutorial/06-testing-and-debugging.md) | 命令验证 9 场景、调试备忘 |
