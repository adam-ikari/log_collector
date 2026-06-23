# Log Collector 教学教程实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 log_collector 项目生成 6 篇教学文章 + README_TUTORIAL.md + 增强源代码注释

**Architecture:** 按项目开发顺序逐步构建，每篇文章聚焦一次开发迭代。先创建目录结构和 README_TUTORIAL.md，然后逐篇撰写文章，最后增强源代码注释。

**Tech Stack:** Markdown

---

## 文件结构

```
log_collector/
├── docs/tutorial/           ← 新建目录
│   ├── 01-project-setup.md
│   ├── 02-daemon-and-signals.md
│   ├── 03-epoll-network.md
│   ├── 04-shm-ringbuffer.md
│   ├── 05-process-pool.md
│   └── 06-testing-and-debugging.md
├── README_TUTORIAL.md       ← 新建
├── src/                     ← 修改：增强注释
│   ├── main.c
│   ├── config.c
│   ├── daemon.c
│   ├── signal_handler.c
│   ├── master.c
│   ├── shm_buffer.c
│   ├── worker.c
│   ├── log_parser.c
│   └── file_writer.c
└── include/
    └── common.h             ← 修改：增强注释
```

---

### Task 1: 创建目录结构和 README_TUTORIAL.md

**Files:**
- Create: `docs/tutorial/` (目录)
- Create: `README_TUTORIAL.md`

- [ ] **Step 1: 创建目录**

```bash
mkdir -p docs/tutorial
```

- [ ] **Step 2: 编写 README_TUTORIAL.md**

```markdown
# Log Collector — Linux 网络日志收集系统

基于 C99 开发的 Linux 网络日志收集系统，作为 syslog 服务端接收远程客户端通过 TCP/UDP 推送的日志，按客户端 IP 和日期分文件存储。

## 涉及技术

守护进程、进程池、共享内存(mmap)、信号量、互斥锁、socket、epoll、CMake

## 架构

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

## 数据流

```
远程客户端 --[TCP/UDP]--> Master(epoll) --[共享内存环形缓冲区]--> Worker --[write]--> /var/log/collector/<client_ip>/YYYY-MM-DD.log
```

## 快速开始

```bash
# 编译
mkdir build && cd build
cmake .. && make

# 运行测试
ctest

# 前台运行
./log_collector -f -c ../conf/log-collector.conf.example

# 发送测试日志
echo "<13>test message" | nc -u 127.0.0.1 5140
```

## 项目结构

```
log_collector/
├── CMakeLists.txt
├── include/
│   └── common.h              # 公共类型、常量
├── src/
│   ├── main.c                # 入口
│   ├── config.c              # 配置解析
│   ├── daemon.c              # 守护进程化
│   ├── signal_handler.c      # 信号处理
│   ├── master.c              # Master 进程 (epoll + 进程池)
│   ├── worker.c              # Worker 进程
│   ├── shm_buffer.c          # 共享内存环形缓冲区
│   ├── log_parser.c          # syslog 格式解析
│   └── file_writer.c         # 文件写入
├── tests/
│   ├── test_config.c
│   ├── test_shm_buffer.c
│   ├── test_log_parser.c
│   ├── test_file_writer.c
│   └── e2e_test.sh
├── conf/
│   └── log-collector.conf.example
└── docs/
    └── tutorial/              # 教学文章
```

## 教学文章

按开发顺序阅读：

| 序号 | 文章 | 内容 |
|------|------|------|
| 1 | [搭骨架](docs/tutorial/01-project-setup.md) | CMake、common.h、配置系统 |
| 2 | [变成守护进程](docs/tutorial/02-daemon-and-signals.md) | daemonize、信号处理、main.c |
| 3 | [接收网络日志](docs/tutorial/03-epoll-network.md) | epoll + TCP/UDP |
| 4 | [共享内存环形缓冲区](docs/tutorial/04-shm-ringbuffer.md) | mmap + 信号量 + 互斥锁 |
| 5 | [进程池与 Worker](docs/tutorial/05-process-pool.md) | fork 进程池、日志解析、文件存储 |
| 6 | [串联测试与调试](docs/tutorial/06-testing-and-debugging.md) | 单元测试、E2E 测试 |
```

- [ ] **Step 3: 验证文件存在**

```bash
ls -la README_TUTORIAL.md docs/tutorial/
```

---

### Task 2: 第 1 篇 — 搭骨架

**Files:**
- Create: `docs/tutorial/01-project-setup.md`

- [ ] **Step 1: 编写文章**

```markdown
# 第 1 篇：搭骨架 — 项目结构、CMake、配置系统

## 这一步要解决什么问题

从零开始，先把项目编译跑通。建立一个可编译的 C99 项目骨架，定义好所有核心数据结构，实现配置文件的读取。

## 怎么设计的

### CMake 构建

项目使用 CMake，指定 C99 标准，链接 pthread 和 rt（POSIX 信号量需要）。源文件按模块拆分到 `src/` 目录，公共头文件放在 `include/`。

```cmake
cmake_minimum_required(VERSION 3.10)
project(log_collector C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(log_collector
    src/main.c
    src/config.c
    # ... 后续逐步添加
)

target_include_directories(log_collector PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(log_collector PRIVATE
    Threads::Threads
    rt
)
```

### common.h — 所有类型的集中定义

`common.h` 是整个项目的"词汇表"，所有模块都通过它理解彼此的数据结构。集中管理的好处是：改一个类型，所有模块自动同步。

```c
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <syslog.h>
```

定义默认常量，所有参数都有合理默认值：

```c
#define DEFAULT_LISTEN_ADDR    "0.0.0.0"
#define DEFAULT_TCP_PORT       5140
#define DEFAULT_UDP_PORT       5140
#define DEFAULT_MAX_CONNS      1024
#define DEFAULT_WORKER_COUNT   4
#define DEFAULT_SLOT_SIZE      4096
#define DEFAULT_SLOT_COUNT     1024
#define DEFAULT_LOG_DIR        "/var/log/collector"
#define DEFAULT_CONF_PATH      "/etc/log-collector.conf"
```

### config_t — 配置结构体

```c
typedef struct {
    char     listen_addr[64];    /* 监听地址 */
    int      tcp_port;           /* TCP 端口 */
    int      udp_port;           /* UDP 端口 */
    int      max_connections;    /* 最大 TCP 连接数 */
    int      worker_count;       /* Worker 进程数 */
    uint64_t slot_size;          /* 共享内存槽位大小 */
    uint64_t slot_count;         /* 共享内存槽位数量 */
    char     log_dir[256];       /* 日志存储目录 */
} config_t;
```

### 配置解析

配置文件是 INI 风格，解析策略是"默认值 + 文件覆盖"：先填充默认值，再逐行读取文件，遇到 `key=value` 就覆盖对应字段。文件不存在时用默认值运行，不报错。

```c
int config_load(config_t *cfg, const char *path) {
    config_set_defaults(cfg);  // 先填默认值

    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 0;  // 文件不存在，用默认值

    char line[512];
    while (fgets(line, sizeof(line), fp) != NULL) {
        // 跳过空行、注释、节头
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';' || line[0] == '[')
            continue;

        // 解析 key=value
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;

        // 提取 key 和 value，去除首尾空白
        // ...
        apply_key_value(cfg, key, value);
    }
    fclose(fp);
    return 0;
}
```

`apply_key_value` 用 `strcmp` 匹配 key，调用对应的解析函数：

```c
static void apply_key_value(config_t *cfg, const char *key, const char *value) {
    if (strcmp(key, "listen_addr") == 0) {
        strncpy(cfg->listen_addr, value, sizeof(cfg->listen_addr) - 1);
    } else if (strcmp(key, "tcp_port") == 0) {
        parse_int(value, &cfg->tcp_port);
    } else if (strcmp(key, "worker_count") == 0) {
        parse_int(value, &cfg->worker_count);
    }
    // ... 其他字段
}
```

## 怎么验证

```bash
mkdir build && cd build
cmake .. && make
./log_collector -h
```

输出帮助信息，确认编译和参数解析正常。后续每完成一个模块，都可以通过编译 + 运行来验证。
```

- [ ] **Step 2: 验证文件存在**

```bash
wc -l docs/tutorial/01-project-setup.md
```

---

### Task 3: 第 2 篇 — 变成守护进程

**Files:**
- Create: `docs/tutorial/02-daemon-and-signals.md`

- [ ] **Step 1: 编写文章**

```markdown
# 第 2 篇：变成守护进程 — daemonize + 信号处理

## 这一步要解决什么问题

程序目前只能在终端前台运行，关掉终端就挂了。需要把它变成守护进程（daemon），脱离终端独立运行，同时能响应系统信号（SIGTERM 优雅关闭、SIGHUP 重载配置、SIGCHLD 收割子进程）。

## 怎么设计的

### 守护进程化：double-fork + setsid

守护进程的核心目标是脱离控制终端。标准做法是两次 fork：

```
fork() → 父进程退出
setsid() → 子进程成为新会话的首进程
fork() → 首进程退出（确保孙子进程不是会话首进程，无法重新获得控制终端）
chdir("/") → 切换到根目录（避免占用挂载点）
umask(0) → 重置文件权限掩码
关闭 stdin/stdout/stderr → 重定向到 /dev/null
写 PID 文件 → 方便外部管理
```

```c
int daemonize(const char *pid_file) {
    pid_t pid;

    // 第一次 fork
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);  // 父进程退出

    // 创建新会话，脱离原终端
    if (setsid() < 0) return -1;

    // 第二次 fork，确保不是会话首进程
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    chdir("/");
    umask(0);

    // 关闭标准 fd，重定向到 /dev/null
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup(fd);  // stdin
        dup(fd);  // stdout
        if (fd > STDERR_FILENO) close(fd);
    }

    // 写 PID 文件
    write_pid_file(pid_file);
    return 0;
}
```

### 信号处理设计

用 `sigaction` 注册信号处理函数，处理函数只做一件事：设置全局标志。主循环轮询这些标志来决定做什么。这样信号处理函数极短，不会引入重入问题。

```c
volatile sig_atomic_t g_shutdown = 0;  // SIGTERM/SIGINT
volatile sig_atomic_t g_sighup   = 0;  // SIGHUP
volatile sig_atomic_t g_sigchld  = 0;  // SIGCHLD

static void handle_sigterm(int sig) {
    (void)sig;
    g_shutdown = 1;
}
```

注册信号：

```c
int signal_handlers_init(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    // SIGTERM / SIGINT → 优雅关闭
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // SIGHUP → 重载配置
    sa.sa_handler = handle_sighup;
    sigaction(SIGHUP, &sa, NULL);

    // SIGCHLD → 收割子进程（SA_NOCLDSTOP 避免收到停止信号）
    sa.sa_handler = handle_sigchld;
    sa.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    // SIGPIPE → 忽略（避免写已关闭的 socket 导致进程退出）
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    return 0;
}
```

### main.c 串联

```c
int main(int argc, char *argv[]) {
    config_t cfg;
    int foreground = 0;
    const char *conf_path = DEFAULT_CONF_PATH;

    // 解析参数：-f 前台运行，-c 指定配置，-h 帮助
    int opt;
    while ((opt = getopt(argc, argv, "fc:h")) != -1) {
        switch (opt) {
        case 'f': foreground = 1; break;
        case 'c': conf_path = optarg; break;
        case 'h': print_usage(argv[0]); return 0;
        }
    }

    config_load(&cfg, conf_path);

    if (!foreground) daemonize(DEFAULT_PID_FILE);

    signal_handlers_init();

    // 后续：初始化共享内存 → master_run
    return 0;
}
```

## 怎么验证

```bash
# 前台运行，Ctrl+C 测试信号响应
./log_collector -f -c /tmp/test.conf

# 后台运行，检查 PID 文件
./log_collector -c /tmp/test.conf
cat /var/run/log-collector.pid
kill -TERM $(cat /var/run/log-collector.pid)
```
```

- [ ] **Step 2: 验证文件存在**

```bash
wc -l docs/tutorial/02-daemon-and-signals.md
```

---

### Task 4: 第 3 篇 — 接收网络日志

**Files:**
- Create: `docs/tutorial/03-epoll-network.md`

- [ ] **Step 1: 编写文章**

```markdown
# 第 3 篇：接收网络日志 — epoll + TCP/UDP

## 这一步要解决什么问题

程序现在是守护进程了，但还不能接收网络数据。需要同时监听 TCP 和 UDP 两个端口，高效处理大量并发连接。

## 怎么设计的

### 为什么用 epoll

需要同时监听多个 fd：TCP listener、UDP listener、以及所有已连接的 TCP 客户端。epoll 是 Linux 下最高效的 I/O 多路复用机制，O(1) 的事件通知，适合高并发场景。

使用边缘触发（EPOLLET）：内核只在 fd 状态变化时通知一次，要求我们循环读取直到 EAGAIN，避免漏数据。

### 创建 TCP listener

```c
static int create_tcp_listener(const config_t *cfg) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    // SO_REUSEADDR：重启时立即复用端口，避免 TIME_WAIT 占用
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 设为非阻塞
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    // bind + listen
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg->tcp_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(fd, SOMAXCONN);

    return fd;
}
```

### 创建 UDP listener

UDP 不需要 listen，创建非阻塞 socket 直接 bind 即可。

### epoll 事件循环

```c
int epoll_fd = epoll_create1(0);

// 注册 TCP listener（边缘触发）
struct epoll_event ev;
ev.events = EPOLLIN | EPOLLET;
ev.data.fd = tcp_fd;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &ev);

// 注册 UDP listener
ev.events = EPOLLIN | EPOLLET;
ev.data.fd = udp_fd;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &ev);

// 主循环
struct epoll_event events[MAX_EVENTS];
while (!g_shutdown) {
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);  // 100ms 超时

    for (int i = 0; i < nfds; i++) {
        if (events[i].data.fd == tcp_fd) {
            // TCP accept
        } else if (events[i].data.fd == udp_fd) {
            // UDP recvfrom
        } else {
            // TCP 客户端数据
        }
    }
}
```

### TCP accept 循环

边缘触发下，一次通知可能有多个连接等待 accept，必须循环 accept 直到返回 EAGAIN：

```c
while (1) {
    int client_fd = accept4(tcp_fd, (struct sockaddr *)&addr, &addr_len, SOCK_NONBLOCK);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        break;
    }

    // 将客户端 fd 注册到 epoll
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    ev.data.ptr = &clients[slot];  // 用 ptr 传递客户端上下文
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
}
```

### TCP 数据读取

边缘触发下同样需要循环 read 直到 EAGAIN。TCP 是流式协议，需要自己拆分行：

```c
while (1) {
    ssize_t n = read(client->fd, buf + buf_len, remaining);
    if (n > 0) {
        buf_len += n;
        // 提取完整行（以 \n 结尾）
        int line_len;
        while ((line_len = extract_line(buf, buf_len)) > 0) {
            // 处理这一行日志
            // 将剩余数据移到缓冲区开头
            memmove(buf, buf + line_len, buf_len - line_len);
            buf_len -= line_len;
        }
    } else if (n == 0) {
        // 对端关闭
        break;
    } else {
        if (errno != EAGAIN) { /* 真错误 */ }
        break;  // EAGAIN：本轮读完
    }
}
```

### EPOLLIN 必须在 EPOLLRDHUP 之前处理

当客户端发送数据后立即关闭连接，epoll 可能同时返回 EPOLLIN 和 EPOLLRDHUP。必须先读数据再处理挂断，否则数据会丢失：

```c
if (events[i].events & EPOLLIN) {
    handle_tcp_data(client, ...);   // 先读
}
if (events[i].events & EPOLLRDHUP) {
    handle_tcp_close(client, ...);  // 再关
}
```

### UDP 数据读取

UDP 是数据报协议，每个 recvfrom 返回一条完整消息，同样循环直到 EAGAIN：

```c
while (1) {
    ssize_t n = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr *)&addr, &addr_len);
    if (n <= 0) break;  // EAGAIN 或错误
    buf[n] = '\0';
    // 处理这条 UDP 消息
}
```

## 怎么验证

```bash
# 启动 collector
./log_collector -f -c /tmp/test.conf &

# TCP 测试
echo "test tcp message" | nc -q1 127.0.0.1 5140

# UDP 测试
echo "test udp message" | nc -u -w1 127.0.0.1 5140
```
```

- [ ] **Step 2: 验证文件存在**

```bash
wc -l docs/tutorial/03-epoll-network.md
```

---

### Task 5: 第 4 篇 — 共享内存环形缓冲区

**Files:**
- Create: `docs/tutorial/04-shm-ringbuffer.md`

- [ ] **Step 1: 编写文章**

```markdown
# 第 4 篇：共享内存环形缓冲区 — mmap + 信号量 + 互斥锁

## 这一步要解决什么问题

Master 收到了日志数据，但需要交给 Worker 进程去解析和写入磁盘。多个进程之间如何高效传递数据？答案是共享内存。

## 怎么设计的

### 为什么用 mmap 文件后备

POSIX 共享内存通常用 `shm_open`，但它依赖 `/dev/shm`。使用 `mmap` 映射普通文件更通用，不依赖 tmpfs。

```c
int shm_fd = open("/tmp/log_collector_shm", O_CREAT | O_RDWR, 0600);
ftruncate(shm_fd, total_size);  // 扩展到需要的大小
void *header = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
close(shm_fd);  // mmap 后可以关闭 fd，映射仍然有效
```

### 内存布局

```
┌────────────────────────────────────────────┐
│  Header (元数据)                            │
│  ┌──────────────────────────────────────┐  │
│  │ magic, version, buffer_size          │  │
│  │ slot_size, slot_count                │  │
│  │ write_pos, read_pos                  │  │
│  │ pthread_mutex_t mutex                │  │
│  │ sem_t sem_free  (空闲槽位计数)        │  │
│  │ sem_t sem_used  (已用槽位计数)        │  │
│  └──────────────────────────────────────┘  │
├────────────────────────────────────────────┤
│  Slot 0 (固定大小，约 4KB)                  │
│  Slot 1                                    │
│  ...                                       │
│  Slot N-1                                  │
└────────────────────────────────────────────┘
```

每个槽位结构：

```c
typedef struct {
    struct sockaddr_storage client_addr;  // 客户端地址
    uint8_t  protocol;                    // 0=TCP, 1=UDP
    uint32_t data_len;                    // 日志长度
    uint64_t timestamp;                   // 接收时间戳
    char     data[];                      // 日志内容
} log_slot_t;
```

### 环形队列

用 `write_pos` 和 `read_pos` 两个指针实现环形队列，通过模运算循环：

```c
header->write_pos = (header->write_pos + 1) % header->slot_count;
header->read_pos  = (header->read_pos + 1)  % header->slot_count;
```

### 生产者消费者模型

用两个信号量控制：

- `sem_free`：空闲槽位计数，初始值 = slot_count（全部空闲）
- `sem_used`：已用槽位计数，初始值 = 0（没有数据）

**生产者（Master）写入：**

```c
int shm_produce(header, slots, addr, protocol, data, data_len, timestamp) {
    // 非阻塞尝试获取空闲槽位，满则丢弃
    if (sem_trywait(&header->sem_free) != 0) {
        return -1;  // 缓冲区满，丢弃此条日志
    }

    pthread_mutex_lock(&header->mutex);

    log_slot_t *slot = get_slot(slots, header->slot_size, header->write_pos);
    memcpy(&slot->client_addr, addr, sizeof(struct sockaddr_storage));
    slot->protocol = protocol;
    slot->data_len = data_len;
    slot->timestamp = timestamp;
    memcpy(slot->data, data, data_len);
    header->write_pos = (header->write_pos + 1) % header->slot_count;

    pthread_mutex_unlock(&header->mutex);
    sem_post(&header->sem_used);  // 通知消费者

    return 0;
}
```

**消费者（Worker）读取：**

```c
int shm_consume(header, slots, slot_size, addr, protocol, data, data_len, timestamp) {
    sem_wait(&header->sem_used);  // 阻塞等待数据

    pthread_mutex_lock(&header->mutex);

    log_slot_t *slot = get_slot(slots, header->slot_size, header->read_pos);

    // 哨兵检测
    if (slot->data_len == 0) {
        header->read_pos = (header->read_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);
        sem_post(&header->sem_free);
        *data_len = 0;
        return 0;
    }

    memcpy(addr, &slot->client_addr, sizeof(struct sockaddr_storage));
    *protocol = slot->protocol;
    *data_len = slot->data_len;
    *timestamp = slot->timestamp;
    memcpy(data, slot->data, slot->data_len);
    header->read_pos = (header->read_pos + 1) % header->slot_count;

    pthread_mutex_unlock(&header->mutex);
    sem_post(&header->sem_free);  // 释放一个空闲槽位

    return slot->data_len;
}
```

### 跨进程同步

互斥锁和信号量都需要支持跨进程：

```c
// 互斥锁
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
pthread_mutex_init(&header->mutex, &attr);

// 信号量（嵌入共享内存，pshared=1）
sem_init(&header->sem_free, 1, slot_count);  // 初始值 = 全部空闲
sem_init(&header->sem_used, 1, 0);            // 初始值 = 0
```

### Worker 如何连接

Worker 进程通过 `shm_connect` 连接到 Master 已创建的共享内存：

```c
int shm_connect(header_out, slots_out, slot_size, slot_count) {
    int shm_fd = open("/tmp/log_collector_shm", O_RDWR);
    // 先读取 header 获取总大小
    shm_header_t *header = mmap(NULL, sizeof(shm_header_t), ..., shm_fd, 0);
    // 验证魔数
    if (header->magic != SHM_MAGIC) return -1;
    // 重新映射完整区域
    size_t total = header->buffer_size;
    munmap(header, sizeof(shm_header_t));
    header = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    *header_out = header;
    *slots_out = (char *)header + sizeof(shm_header_t);
    return 0;
}
```

### 哨兵机制

关闭时 Master 向缓冲区发送 N 个 `data_len=0` 的哨兵消息，每个 Worker 收到哨兵后退出消费循环。

## 怎么验证

单元测试通过 fork 创建父子进程，父进程生产、子进程消费，验证数据正确传递。
```

- [ ] **Step 2: 验证文件存在**

```bash
wc -l docs/tutorial/04-shm-ringbuffer.md
```

---

### Task 6: 第 5 篇 — 进程池与 Worker

**Files:**
- Create: `docs/tutorial/05-process-pool.md`

- [ ] **Step 1: 编写文章**

```markdown
# 第 5 篇：进程池与 Worker — fork + 日志写入

## 这一步要解决什么问题

共享内存缓冲区已经就绪，现在需要创建 Worker 进程从缓冲区取数据，解析 syslog 格式，按客户端 IP 和日期写入文件。同时 Master 需要管理这些 Worker：启动、崩溃重启、优雅关闭。

## 怎么设计的

### Master fork Worker 进程池

```c
static pid_t *fork_workers(const config_t *cfg) {
    pid_t *pids = calloc(cfg->worker_count, sizeof(pid_t));

    for (int i = 0; i < cfg->worker_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程：重置信号处理为默认
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGHUP, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
            signal(SIGPIPE, SIG_DFL);

            free(pids);
            worker_run(cfg);  // 进入 Worker 主循环
            _exit(0);
        } else if (pid > 0) {
            pids[i] = pid;
        }
    }
    return pids;
}
```

子进程必须重置信号处理，因为 fork 会继承父进程的信号处理函数，而 Worker 不需要响应这些信号。

### Worker 主流程

```c
void worker_run(const config_t *cfg) {
    // 1. 连接共享内存
    shm_header_t *header;
    void *slots;
    uint64_t slot_size, slot_count;
    shm_connect(&header, &slots, &slot_size, &slot_count);

    // 2. 初始化文件写入器
    file_writer_t fw;
    file_writer_init(&fw, cfg->log_dir);

    // 3. 消费循环
    for (;;) {
        struct sockaddr_storage addr;
        uint8_t protocol;
        uint32_t data_len;
        uint64_t timestamp;
        char buf[4096];

        shm_consume(header, slots, slot_size, &addr, &protocol,
                    buf, &data_len, &timestamp);

        // 哨兵：退出
        if (data_len == 0) break;

        // 4. 解析日志格式
        char formatted[8192];
        log_parser_format(&addr, timestamp, buf, data_len,
                          formatted, sizeof(formatted));

        // 5. 提取 IP 字符串
        char ip_str[INET6_ADDRSTRLEN];
        if (addr.ss_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr,
                      ip_str, sizeof(ip_str));
        } else if (addr.ss_family == AF_INET6) {
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr,
                      ip_str, sizeof(ip_str));
        }

        // 6. 写入文件
        file_writer_write(&fw, ip_str, formatted, written);
    }

    file_writer_close(&fw);
    shm_disconnect(header, slots);
    _exit(0);
}
```

### 日志解析

syslog 格式以 `<PRI>` 开头，PRI = facility × 8 + severity。severity 取低 3 位：

```c
static int extract_pri(const char *msg, uint32_t len, int *severity_out) {
    if (len < 3 || msg[0] != '<') return -1;
    const char *end = memchr(msg, '>', len);
    if (end == NULL) return -1;
    long pri = strtol(msg + 1, NULL, 10);
    *severity_out = (int)(pri & 0x07);  // 低 3 位 = severity
    return (int)(end - msg + 1);         // 返回 PRI 长度
}
```

severity 映射：

| 值 | 标签 |
|----|------|
| 0 | emerg |
| 1 | alert |
| 2 | crit |
| 3 | err |
| 4 | warning |
| 5 | notice |
| 6 | info |
| 7 | debug |

输出格式：`2026-06-18T14:32:05+08:00 192.168.1.100 [notice] connection timeout`

### 文件存储

按客户端 IP 分目录，按日期分文件：

```
/var/log/collector/
├── 192.168.1.100/
│   ├── 2026-06-18.log
│   └── 2026-06-17.log
└── 10.0.0.55/
    └── 2026-06-18.log
```

`file_writer_write` 检测 IP 或日期变化时自动切换文件：

```c
int file_writer_write(file_writer_t *fw, const char *ip,
                      const char *data, int data_len) {
    char today[16];
    get_current_date(today, sizeof(today));

    // IP 或日期变了 → 关闭旧文件，打开新文件
    if (fw->fd < 0 ||
        strcmp(fw->current_ip, ip) != 0 ||
        strcmp(fw->current_date, today) != 0) {

        if (fw->fd >= 0) close(fw->fd);

        // 创建目录（递归创建父目录）
        char dir_path[320];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", fw->log_dir, ip);
        ensure_directory(dir_path);

        // 打开日志文件
        char file_path[384];
        snprintf(file_path, sizeof(file_path), "%s/%s.log", dir_path, today);
        fw->fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);

        strncpy(fw->current_ip, ip, sizeof(fw->current_ip) - 1);
        strncpy(fw->current_date, today, sizeof(fw->current_date) - 1);
    }

    write(fw->fd, data, data_len);
    return 0;
}
```

目录创建需要递归：`mkdir("/var/log/collector/192.168.1.100")` 时，如果 `/var/log/collector` 不存在，`mkdir` 会失败。需要先创建父目录：

```c
static int ensure_directory(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;

    // 递归创建父目录
    char parent[320];
    strncpy(parent, dir, sizeof(parent) - 1);
    char *slash = strrchr(parent, '/');
    if (slash != NULL && slash != parent) {
        *slash = '\0';
        ensure_directory(parent);
    }
    return mkdir(dir, 0755);
}
```

### Worker 崩溃自动重启

Master 通过 SIGCHLD 检测 Worker 退出，用 `waitpid WNOHANG` 收割并立即 fork 新 Worker：

```c
static void reap_workers(pid_t *pids, int count, const config_t *cfg) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < count; i++) {
            if (pids[i] == pid) {
                // 重启这个 Worker
                pid_t new_pid = fork();
                if (new_pid == 0) {
                    worker_run(cfg);
                    _exit(0);
                } else if (new_pid > 0) {
                    pids[i] = new_pid;
                }
                break;
            }
        }
    }
}
```

### 优雅关闭

```
SIGTERM →
  1. 关闭监听 socket（不再 accept 新连接）
  2. 向缓冲区发送 N 个哨兵（data_len=0）
  3. wait_workers：等待 Worker 退出（超时 10 秒）
  4. 超时则 SIGKILL 剩余 Worker
  5. 清理共享内存、删除 PID 文件
```

## 怎么验证

```bash
# 检查 Worker 进程
pgrep -P $(cat /var/run/log-collector.pid)

# 发送日志并检查文件
echo "<13>test message" | nc -u 127.0.0.1 5140
cat /var/log/collector/127.0.0.1/$(date +%Y-%m-%d).log
```
```

- [ ] **Step 2: 验证文件存在**

```bash
wc -l docs/tutorial/05-process-pool.md
```

---

### Task 7: 第 6 篇 — 串联测试与调试

**Files:**
- Create: `docs/tutorial/06-testing-and-debugging.md`

- [ ] **Step 1: 编写文章**

```markdown
# 第 6 篇：串联测试与调试

## 这一步要解决什么问题

所有模块都写完了，需要验证整个系统能正常工作。通过单元测试验证各模块独立正确性，通过端到端测试验证整体行为。

## 怎么设计的

### 单元测试

CMake + CTest 管理测试。每个测试是独立的可执行文件，链接对应的源文件。

```cmake
enable_testing()
add_subdirectory(tests)

# tests/CMakeLists.txt
add_executable(test_config test_config.c ../src/config.c)
target_link_libraries(test_config PRIVATE Threads::Threads rt)
add_test(NAME test_config COMMAND test_config)
```

**test_config（3 个用例）：**
- 默认值：配置文件不存在时使用默认值
- 解析：读取 INI 文件，验证所有字段正确覆盖
- 注释和空行：跳过 `#`、`;` 注释和空行

**test_shm_buffer（4 个用例）：**
- 初始化/销毁：验证魔数、版本、大小
- fork 跨进程生产消费：父进程写入，子进程读取，验证数据一致
- 哨兵：发送 data_len=0，消费者正确识别并退出

**test_log_parser（4 个用例）：**
- 基本 syslog：`<13>...` 解析出 severity=notice
- 无 PRI：默认 severity=debug
- emerg 级别：`<0>...` 解析出 severity=emerg
- IPv6：验证 IPv6 地址提取

**test_file_writer（2 个用例）：**
- 写入并验证文件内容
- IP 切换：不同 IP 创建不同目录

### E2E 测试

Bash 脚本，用 `nc` 模拟客户端发送日志，验证完整数据流。

**测试场景（9 个）：**

1. **启动和优雅关闭**：启动 collector → kill -TERM → 验证进程退出
2. **UDP 单消息**：发送一条 UDP 日志，验证文件内容、severity、IP
3. **TCP 单消息**：发送一条 TCP 日志，验证文件内容
4. **批量混合**：10 TCP + 10 UDP，验证消息数 ≥ 18
5. **多 IP 隔离**：验证不同客户端 IP 的目录结构
6. **severity 全级别**：发送 PRI 0-7，验证所有级别正确映射
7. **高吞吐压力**：1000 条消息，验证接收率 ≥ 90%
8. **Worker 崩溃恢复**：kill -9 一个 Worker，验证自动重启且系统正常
9. **无 PRI 默认级别**：无 `<PRI>` 前缀的消息默认 debug

### 测试中的关键细节

**TCP 端口验证：** 发送 TCP 消息前用 `ss -tlnp` 确认端口已监听，避免竞态。

**进程清理：** 测试前后用 `pkill` 清理残留进程，正则需精确匹配二进制名避免误杀脚本：
```bash
pkill -9 -f "(^|/)log_collector[[:space:]]"
fuser -k 5140/tcp 2>/dev/null
fuser -k 5140/udp 2>/dev/null
```

**Worker PID 提取：** `pgrep -P` 输出多行，用 `head -1` 取第一个：
```bash
FIRST_WORKER=$(pgrep -P $COLLECTOR_PID | head -1)
```

## 怎么验证

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make
ctest --output-on-failure

# E2E 测试
cd ..
bash tests/e2e_test.sh
```

期望输出：13 个单元测试全部 PASS，24 个 E2E 断言全部 PASS。
```

- [ ] **Step 2: 验证文件存在**

```bash
wc -l docs/tutorial/06-testing-and-debugging.md
```

---

### Task 8: 增强源代码注释

**Files:**
- Modify: `include/common.h`
- Modify: `src/main.c`
- Modify: `src/config.c`
- Modify: `src/daemon.c`
- Modify: `src/signal_handler.c`
- Modify: `src/master.c`
- Modify: `src/shm_buffer.c`
- Modify: `src/worker.c`
- Modify: `src/log_parser.c`
- Modify: `src/file_writer.c`

- [ ] **Step 1: 增强 common.h 注释**

在关键结构体和常量上方添加教学性注释：

```c
/*
 * 共享内存头部 — 存储在所有进程间共享的元数据
 *
 * 布局说明：
 *   magic/version  — 校验共享内存是否由本程序创建
 *   buffer_size    — mmap 映射的总字节数
 *   slot_size      — 每个槽位的字节数（含 log_slot_t 头部 + data 柔性数组）
 *   slot_count     — 槽位总数，环形队列容量
 *   write_pos      — Master 写入位置（生产者指针）
 *   read_pos       — Worker 读取位置（消费者指针）
 *   mutex          — 跨进程互斥锁，保护 write_pos/read_pos 的并发访问
 *   sem_free       — 空闲槽位计数信号量（初始 = slot_count）
 *   sem_used       — 已用槽位计数信号量（初始 = 0）
 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t buffer_size;
    uint64_t slot_size;
    uint64_t slot_count;
    uint64_t write_pos;
    uint64_t read_pos;
    pthread_mutex_t mutex;
    sem_t  sem_free;
    sem_t  sem_used;
} shm_header_t;

/*
 * 日志槽位 — 环形缓冲区中的每个条目
 *
 * client_addr — 客户端地址（IPv4/IPv6 通用，128 字节）
 * protocol    — 0=TCP, 1=UDP
 * data_len    — 日志内容实际长度
 * timestamp   — 接收时间（epoch 秒）
 * data[]      — 柔性数组，实际长度由 slot_size 决定
 */
typedef struct {
    struct sockaddr_storage client_addr;
    uint8_t  protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char     data[];
} log_slot_t;
```

- [ ] **Step 2: 增强 main.c 注释**

在 main 函数各阶段添加注释说明启动流程：

```c
int main(int argc, char *argv[]) {
    // 阶段 1：解析命令行参数
    // 阶段 2：加载配置（默认值 + 文件覆盖）
    // 阶段 3：守护进程化（double-fork + setsid）
    // 阶段 4：注册信号处理（SIGTERM/SIGINT/SIGHUP/SIGCHLD/SIGPIPE）
    // 阶段 5：初始化共享内存（mmap + 信号量 + 互斥锁）
    // 阶段 6：启动 Master 事件循环（epoll + Worker 进程池）
    // 阶段 7：清理资源
}
```

- [ ] **Step 3: 增强 config.c 注释**

在 `config_load` 和 `apply_key_value` 上添加注释说明解析策略。

- [ ] **Step 4: 增强 daemon.c 注释**

在 `daemonize` 的每个步骤添加注释说明 double-fork 流程。

- [ ] **Step 5: 增强 signal_handler.c 注释**

说明为什么信号处理函数只设置标志位、为什么用 `volatile sig_atomic_t`。

- [ ] **Step 6: 增强 master.c 注释**

在关键函数上添加注释：epoll 事件循环、TCP accept 循环、边缘触发读取、EPOLLIN/RDHUP 处理顺序、进程池管理、优雅关闭流程。

- [ ] **Step 7: 增强 shm_buffer.c 注释**

在 `shm_init`、`shm_produce`、`shm_consume`、`shm_send_sentinels` 上添加注释说明生产者消费者模型和哨兵机制。

- [ ] **Step 8: 增强 worker.c 注释**

说明 Worker 的完整处理流程：连接共享内存 → 消费循环 → 解析 → 写文件。

- [ ] **Step 9: 增强 log_parser.c 注释**

说明 PRI 提取算法和 severity 映射。

- [ ] **Step 10: 增强 file_writer.c 注释**

说明文件切换逻辑和递归目录创建。

- [ ] **Step 11: 验证编译**

```bash
cd build && cmake .. && make
```

---

### Task 9: 最终验证

- [ ] **Step 1: 验证所有文件存在**

```bash
ls -la README_TUTORIAL.md
ls -la docs/tutorial/01-project-setup.md
ls -la docs/tutorial/02-daemon-and-signals.md
ls -la docs/tutorial/03-epoll-network.md
ls -la docs/tutorial/04-shm-ringbuffer.md
ls -la docs/tutorial/05-process-pool.md
ls -la docs/tutorial/06-testing-and-debugging.md
```

- [ ] **Step 2: 验证项目仍可编译和测试通过**

```bash
cd build && cmake .. && make && ctest --output-on-failure
```
