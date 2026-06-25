# 第 1 篇：搭骨架 — 项目结构、CMake、配置系统

## 回忆已学知识

这个项目会把前面学的几乎所有东西都用上：

- **进程基础**：`fork`、`_exit`、`waitpid` —— Worker 进程池靠它
- **信号处理**：`signal`、`siginterrupt`、`volatile sig_atomic_t` —— 优雅关闭靠它
- **共享内存**：`shm_open`、`mmap`、`sem_t`、`pthread_mutex_t` —— 进程间通信靠它
- **Socket**：TCP/UDP、`socket`、`bind`、`listen`、`accept` —— 接收网络日志靠它
- **epoll**：`epoll_create1`、`epoll_ctl`、`epoll_wait`、EPOLLET —— 高性能 I/O 多路复用
- **线程池/进程池**：生产者消费者、并发调度 —— 架构设计靠它
- **守护进程**：systemd Type=simple、`sd_journal_print` —— 后台运行与服务管理靠它

## 先跑起来再说

做系统编程最怕什么？写了一千行代码，一编译 50 个错。

所以我们第一步不写业务逻辑。先把 CMake 配好、把 main 函数写出来、把该引的头文件引上、把核心数据结构定义好。让一个空壳程序能编译、能运行。

这就是"骨架先行"——后面每加一个模块，都是在骨架上长肉。每一篇教程对应一个模块，每完成一篇你都可以编译运行验证。

## 本篇要创建/修改的文件

| 操作 | 文件                            | 说明                                    |
| ---- | ------------------------------- | --------------------------------------- |
| 创建 | `CMakeLists.txt`                | 构建系统配置                            |
| 创建 | `include/common.h`              | 公共类型定义、头文件、常量              |
| 创建 | `src/config.h`                  | 编译期配置常量                          |
| 创建 | `src/main.c`                    | 程序入口                                |
| 创建 | `systemd/log-collector.service` | systemd 服务配置（先创建，第 2 篇用到） |
| 创建 | `src/signal_handler.c`          | 信号处理空壳（先创建，第 2 篇实现）     |
| 创建 | `src/signal_handler.h`          | 信号处理头文件                          |
| 创建 | `src/master.c`                  | Master 空壳（先创建，第 3 篇实现）      |
| 创建 | `src/master.h`                  | Master 头文件                           |
| 创建 | `src/shm_buffer.c`              | 共享内存空壳（先创建，第 4 篇实现）     |
| 创建 | `src/shm_buffer.h`              | 共享内存头文件                          |
| 创建 | `src/worker.c`                  | Worker 空壳（先创建，第 5 篇实现）      |
| 创建 | `src/worker.h`                  | Worker 头文件                           |
| 创建 | `src/log_parser.c`              | 日志解析空壳（先创建，第 5 篇实现）     |
| 创建 | `src/log_parser.h`              | 日志解析头文件                          |
| 创建 | `src/file_writer.c`             | 文件写入空壳（先创建，第 5 篇实现）     |
| 创建 | `src/file_writer.h`             | 文件写入头文件                          |

> **提示**：一次性创建所有空壳文件，让 CMake 从一开始就能编译通过：
>
> ```bash
> cd src
> for f in signal_handler.c master.c shm_buffer.c worker.c log_parser.c file_writer.c; do
>     echo '#include "common.h"' > "$f" && echo "/* TODO */" >> "$f"
> done
> for f in signal_handler.h master.h shm_buffer.h worker.h log_parser.h file_writer.h; do
>     echo "#ifndef $(echo ${f%.h} | tr '[:lower:]' '[:upper:]')_H" > "$f"
>     echo "#define $(echo ${f%.h} | tr '[:lower:]' '[:upper:]')_H" >> "$f"
>     echo '#include "common.h"' >> "$f"
>     echo "#endif" >> "$f"
> done
> # 同时创建 systemd 服务空文件（内容在第 2 篇写入）
> mkdir -p ../systemd && touch ../systemd/log-collector.service
> ```

## CMake

创建 `CMakeLists.txt`（项目根目录）：

```cmake
# CMakeLists.txt — 项目根目录
cmake_minimum_required(VERSION 3.10)
project(log_collector C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

add_executable(log_collector
    src/main.c
    src/file_writer.c
    src/log_parser.c
    src/master.c
    src/shm_buffer.c
    src/signal_handler.c
    src/worker.c
)

target_include_directories(log_collector PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${SYSTEMD_INCLUDE_DIRS}
)

target_link_libraries(log_collector PRIVATE
    Threads::Threads
    rt
    ${SYSTEMD_LIBRARIES}
)

install(TARGETS log_collector RUNTIME DESTINATION sbin)
install(FILES systemd/log-collector.service DESTINATION /etc/systemd/system)
```

逐行解释：

**`project(log_collector C)`** —— 明确指定 C 语言。不加这个 CMake 默认同时检测 C 和 C++ 编译器，浪费时间。

**`CMAKE_C_STANDARD 99`** —— C99。我们要的特性 C99 都有：`stdint.h`（`uint32_t`、`uint64_t` 定长整数）、`//` 单行注释、混合声明（变量可以在代码块任意位置声明）。C99 是所有 Linux 发行版 GCC 的默认底线，兼容性最好。

**`-Wall -Wextra -Wpedantic`** —— 让编译器当你的代码审查员。`-Wall` 开启大部分警告，`-Wextra` 开启更多（比如未使用的参数），`-Wpedantic` 拒绝非标准 C 的写法。警告就是潜在的 bug。

**`find_package(Threads REQUIRED)`** —— 自动找到 pthread 库。CMake 会创建导入目标 `Threads::Threads`，自动处理 `-lpthread` 和编译选项。比手写 `-lpthread` 更可靠。

**`find_package(PkgConfig REQUIRED)` + `pkg_check_modules(SYSTEMD REQUIRED libsystemd)`** —— 通过 pkg-config 找到 libsystemd。`sd_journal_print()` 需要这个库，守护进程用它写日志到 systemd journal。`${SYSTEMD_LIBRARIES}` 和 `${SYSTEMD_INCLUDE_DIRS}` 自动填充为正确的链接选项和头文件路径。

**`rt`** —— POSIX 实时扩展库。提供 `sem_init`、`sem_wait`、`sem_post`、`shm_open` 等函数。注意 glibc 2.34+ 把信号量移到了 libc 里，但链接 `rt` 保持向后兼容。

**`PRIVATE`** —— 头文件目录只在编译本项目时使用。这里只有一个 target，用 `PRIVATE` 还是 `PUBLIC` 没区别，但养成好习惯。

## 目录怎么分

```
log_collector/
├── CMakeLists.txt
├── include/
│   └── common.h          ← 所有模块共享的类型和常量
├── src/
│   ├── main.c            ← 入口，串联一切
│   ├── config.h          ← 编译期配置常量
│   ├── signal_handler.c/h← 信号处理
│   ├── shm_buffer.c/h    ← POSIX 共享内存 + 互斥锁 + 信号量
│   ├── master.c/h        ← Master：epoll 事件循环 + fork Worker 池
│   ├── worker.c/h        ← Worker：取数据→解析→写文件
│   ├── log_parser.c/h    ← syslog 格式解析
│   └── file_writer.c/h   ← 按 IP+日期 写日志文件
└── systemd/
    └── log-collector.service  ← systemd 服务配置
```

一个 `.c` 对应一个职责。这不是教条，是经验——当你在 epoll 代码里找为什么 TCP 连接断掉的时候，你不会想同时看到共享内存的逻辑混在里面。

## config.h — 编译期配置常量

创建 `src/config.h`，写入：

```c
/* src/config.h — 编译期配置（教学项目不需要配置文件） */
#ifndef CONFIG_H
#define CONFIG_H

#define CFG_LISTEN_ADDR       "0.0.0.0"
#define CFG_TCP_PORT          5140
#define CFG_UDP_PORT          5140
#define CFG_MAX_CONNECTIONS   1024
#define CFG_WORKER_COUNT      4
#define CFG_SLOT_SIZE         4096
#define CFG_SLOT_COUNT        1024
#define CFG_LOG_DIR           "/tmp/log_collector_test"

#endif
```

教学项目不需要配置文件解析——编译期常量足够。生产环境中这些值通常来自 YAML/TOML 配置文件或命令行参数。

## common.h — 整个项目的"词汇表"

创建 `include/common.h`，写入以下完整内容：

```c
/* include/common.h */
#ifndef COMMON_H
#define COMMON_H

/*
 * common.h — 整个项目的"词汇表"
 *
 * 所有模块共享的类型定义和常量都在这里。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>        /* fork, read, write, close, getpid */
#include <errno.h>         /* errno, EAGAIN, EINTR */
#include <time.h>          /* time_t, localtime_r, strftime */
#include <signal.h>        /* signal, sig_atomic_t, SIGTERM... */
#include <sys/types.h>     /* pid_t, ssize_t */
#include <sys/stat.h>      /* stat, mkdir, umask */
#include <sys/socket.h>    /* socket, bind, listen, accept */
#include <sys/wait.h>      /* waitpid, WNOHANG */
#include <sys/epoll.h>     /* epoll_create1, epoll_ctl, epoll_wait */
#include <sys/mman.h>      /* mmap, munmap, MAP_SHARED, MAP_FAILED */
#include <sys/time.h>      /* gettimeofday */
#include <arpa/inet.h>     /* htons, inet_pton, inet_ntop */
#include <netinet/in.h>    /* sockaddr_in, sockaddr_in6, INADDR_ANY */
#include <fcntl.h>         /* open, O_RDWR, O_CREAT, O_NONBLOCK */
#include <pthread.h>       /* pthread_mutex_t, PTHREAD_PROCESS_SHARED */
#include <semaphore.h>     /* sem_t, sem_init, sem_wait, sem_post */
#include <limits.h>        /* INT_MAX */
#include <systemd/sd-journal.h>  /* sd_journal_print */

#ifdef __cplusplus
extern "C" {
#endif

/* ── 共享内存常量 ───────────────────────────── */

#define SHM_NAME     "/log_collector_shm"
#define SHM_MAGIC    0x4C434F47   /* "LCOG" 四个字母的 ASCII */
#define SHM_VERSION  1

/* ── 网络常量 ───────────────────────────────── */

#define MAX_EVENTS         1024
#define TCP_RECV_BUF_SIZE  65536
#define UDP_RECV_BUF_SIZE  65536

/* ── 配置结构体 ────────────────────────────── */

typedef struct {
    char     listen_addr[64];    /* 监听地址，默认 0.0.0.0 */
    int      tcp_port;           /* TCP 端口 */
    int      udp_port;           /* UDP 端口 */
    int      max_connections;    /* 最大 TCP 连接数 */
    int      worker_count;       /* Worker 进程数 */
    uint64_t slot_size;          /* 共享内存槽位大小（字节） */
    uint64_t slot_count;         /* 共享内存槽位数量 */
    char     log_dir[256];       /* 日志存储根目录 */
} config_t;

/*
 * 共享内存头部 — 存储在所有进程间共享的元数据
 *
 * magic/version  — 校验共享内存是否由本程序创建
 * buffer_size    — mmap 映射的总字节数
 * slot_size      — 每个槽位的字节数
 * slot_count     — 槽位总数，环形队列容量
 * write_pos      — Master 写入位置（生产者指针）
 * read_pos       — Worker 读取位置（消费者指针）
 * mutex          — 跨进程互斥锁，保护 write_pos/read_pos 的并发访问
 * sem_free       — 空闲槽位计数信号量（初始 = slot_count）
 * sem_used       — 已用槽位计数信号量（初始 = 0）
 */
typedef struct {
    uint32_t magic;               /* 魔数：0x4C434F47 = "LCOG" */
    uint32_t version;
    uint64_t buffer_size;         /* mmap 映射的总字节数 */
    uint64_t slot_size;           /* 每个槽位的字节数 */
    uint64_t slot_count;          /* 槽位总数 */
    uint64_t write_pos;           /* 生产者写入位置 */
    uint64_t read_pos;            /* 消费者读取位置 */
    pthread_mutex_t mutex;        /* 保护 write_pos/read_pos */
    sem_t  sem_free;              /* 空闲槽位计数 */
    sem_t  sem_used;              /* 已用槽位计数 */
} shm_header_t;

/*
 * 日志槽位 — 环形缓冲区中的每个条目
 *
 * client_addr — 客户端地址（IPv4/IPv6 通用，128 字节）
 * protocol    — 0=TCP, 1=UDP
 * data_len    — 日志内容实际长度
 * timestamp   — 接收时间（epoch 秒）
 * data        — 日志正文（最大 4096 字节，与槽位大小对齐）
 */
typedef struct {
    struct sockaddr_storage client_addr;
    uint8_t  protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char     data[4096];
} log_slot_t;

/* ── 全局信号标志 ──────────────────────────── */

extern volatile sig_atomic_t g_shutdown;
extern volatile sig_atomic_t g_sighup;
extern volatile sig_atomic_t g_sigchld;

#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */
```

> **学习技巧**：不需要记住每个头文件。写代码时 `man 2 函数名` 会告诉你要 `#include` 什么。

`#ifdef __cplusplus` 只有 C++ 编译器定义，C 编译器不受影响。项目中除 `config.h`（纯宏定义，不需要）外的 7 个头文件都做了这个处理。

## 主函数：把零件串起来

创建 `src/main.c`，写入：

```c
/* src/main.c — 程序入口：串联所有模块 */
#include "common.h"
#include "config.h"
#include "signal_handler.h"
#include "shm_buffer.h"
#include "master.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "用法: %s [-h]\n", prog);
    fprintf(stderr, "  -h   显示帮助\n");
}

/*
 * 启动流程：
 *   解析参数 → 注册信号 → 创建共享内存 → Master 主循环 → 清理
 *
 * 守护进程化由 systemd Type=simple 负责，程序本身只做前台运行。
 */
int main(int argc, char *argv[]) {
    config_t cfg = {
        .listen_addr     = CFG_LISTEN_ADDR,
        .tcp_port        = CFG_TCP_PORT,
        .udp_port        = CFG_UDP_PORT,
        .max_connections = CFG_MAX_CONNECTIONS,
        .worker_count    = CFG_WORKER_COUNT,
        .slot_size       = CFG_SLOT_SIZE,
        .slot_count      = CFG_SLOT_COUNT,
        .log_dir         = CFG_LOG_DIR,
    };
    shm_header_t *shm_header = NULL;
    void *slots = NULL;
    int opt, rc;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    sd_journal_print(LOG_INFO, "Log Collector 启动 (TCP:%d UDP:%d workers:%d)",
                     cfg.tcp_port, cfg.udp_port, cfg.worker_count);

    if (signal_handlers_init() < 0) {
        sd_journal_print(LOG_ERR, "信号注册失败");
        return 1;
    }

    if (shm_init(&shm_header, &slots, cfg.slot_size, cfg.slot_count) < 0) {
        sd_journal_print(LOG_ERR, "共享内存初始化失败");
        return 1;
    }

    rc = master_run(&cfg, shm_header, slots);

    sd_journal_print(LOG_INFO, "Log Collector 已退出 (rc=%d)", rc);

    shm_destroy(shm_header, slots, cfg.slot_count);
    return rc;
}
```

这就是整个程序的骨架。`config_t` 用 C99 的**指定初始化器**（designated initializer）直接初始化，省掉了逐字段赋值的啰嗦代码。

启动流程的 5 个步骤，每一步对应一个模块：

| 步骤 | 操作            | 对应模块                                   |
| ---- | --------------- | ------------------------------------------ |
| 1    | 解析命令行参数  | getopt                                     |
| 2    | 注册信号处理    | signal_handler.c — signal + siginterrupt   |
| 3    | 创建共享内存    | shm_buffer.c — shm_open + mmap + 锁/信号量 |
| 4    | Master 事件循环 | master.c — epoll + fork Worker 池          |
| 5    | 清理资源        | shm_destroy                                |

## 系统依赖安装

在开始写代码之前，先把编译和运行需要的库装好。

### Ubuntu/Debian

```bash
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libsystemd-dev
```

### Fedora/RHEL

```bash
sudo dnf install -y \
    gcc gcc-c++ \
    cmake \
    pkg-config \
    systemd-devel
```

### Arch Linux

```bash
sudo pacman -S --noconfirm \
    base-devel \
    cmake \
    pkg-config \
    systemd
```

### 各依赖的用途

| 包名                               | 提供什么                 | 为什么需要                                    |
| ---------------------------------- | ------------------------ | --------------------------------------------- |
| `build-essential` / `base-devel`   | gcc, make, libc          | 编译 C 程序的基础工具链                       |
| `cmake`                            | cmake, ctest             | 构建系统和测试运行器                          |
| `pkg-config`                       | pkg-config               | CMake 通过它找到 libsystemd 的路径            |
| `libsystemd-dev` / `systemd-devel` | libsystemd, sd-journal.h | `sd_journal_print()` 写日志到 systemd journal |

> **注意**：`libsystemd-dev` 是编译时依赖（提供头文件和 `.so` 符号链接）。运行时只需要 `libsystemd0`（Ubuntu 默认已安装）。如果目标机器不装 systemd（比如 Docker 容器里），可以去掉 systemd 依赖改用 `fprintf(stderr, ...)`，但本教程假设运行在带 systemd 的 Linux 上。

### 验证安装

```bash
# 确认 gcc 可用
gcc --version | head -1

# 确认 cmake 可用
cmake --version | head -1

# 确认 pkg-config 能找到 libsystemd
pkg-config --modversion libsystemd
```

三条命令都有输出、没有报错，就可以继续了。

## 怎么验证

```bash
$ cd log_collector
$ mkdir build && cd build
$ cmake .. && make
-- The C compiler identification is GNU 11.4.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Looking for pthread.h
-- Looking for pthread.h - found
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Found PkgConfig: /usr/bin/pkg-config (found version "0.29.2")
-- Checking for module 'libsystemd'
--   Found libsystemd, version 249
-- Configuring done
-- Generating done
-- Build files have been written to: /home/gem/project/log_collector/build
[ 11%] Building C object CMakeFiles/log_collector.dir/src/main.c.o
[ 22%] Building C object CMakeFiles/log_collector.dir/src/file_writer.c.o
[ 33%] Building C object CMakeFiles/log_collector.dir/src/log_parser.c.o
[ 44%] Building C object CMakeFiles/log_collector.dir/src/master.c.o
[ 55%] Building C object CMakeFiles/log_collector.dir/src/shm_buffer.c.o
[ 66%] Building C object CMakeFiles/log_collector.dir/src/signal_handler.c.o
[ 77%] Building C object CMakeFiles/log_collector.dir/src/worker.c.o
[ 88%] Linking C executable log_collector
[100%] Built target log_collector

$ ./log_collector -h
用法: ./log_collector [-h]
  -h   显示帮助
```

编译零错误。`siginterrupt` 的 deprecation warning 是正常的——第 2 篇会解释为什么不用 `sigaction`。帮助信息正常——骨架搭好了。接下来每完成一个模块，我们都会跑一遍编译+验证，确保每一步都是可工作的。
