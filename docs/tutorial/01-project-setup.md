# 第 1 篇：搭骨架 — 项目结构、CMake、配置系统

## 回忆已学知识

这个项目会把前面学的几乎所有东西都用上：

- **进程基础**：`fork`、`_exit`、`waitpid` —— Worker 进程池靠它
- **信号处理**：`signal`、`siginterrupt`、`volatile sig_atomic_t` —— 优雅关闭靠它
- **共享内存**：`shm_open`、`mmap`、`sem_t`、`pthread_mutex_t` —— 进程间通信靠它
- **Socket**：TCP/UDP、`socket`、`bind`、`listen`、`accept` —— 接收网络日志靠它
- **epoll**：`epoll_create1`、`epoll_ctl`、`epoll_wait`、EPOLLET —— 高性能 I/O 多路复用
- **线程池/进程池**：生产者消费者、并发调度 —— 架构设计靠它
- **守护进程**：double-fork、`setsid` —— 后台运行靠它

## 先跑起来再说

做系统编程最怕什么？写了一千行代码，一编译 50 个错。

所以我们第一步不写业务逻辑。先把 CMake 配好、把 main 函数写出来、把该引的头文件引上、把核心数据结构定义好。让一个空壳程序能编译、能运行。

这就是"骨架先行"——后面每加一个模块，都是在骨架上长肉。每一篇教程对应一个模块，每完成一篇你都可以编译运行验证。

## CMake

```cmake
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
    src/daemon.c
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

**`install(...)`** —— 安装规则。`cmake --install build` 会把可执行文件装到 `/usr/local/sbin/`，systemd service 文件装到 `/etc/systemd/system/`。

## 目录怎么分

```
log_collector/
├── CMakeLists.txt
├── include/
│   └── common.h          ← 所有模块共享的类型和常量
├── src/
│   ├── main.c            ← 入口，串联一切
│   ├── config.h          ← 编译期配置常量
│   ├── daemon.c/h        ← 守护进程化
│   ├── signal_handler.c/h← 信号处理
│   ├── shm_buffer.c/h    ← POSIX 共享内存 + 互斥锁 + 信号量
│   ├── master.c/h        ← Master：epoll 事件循环 + fork Worker 池
│   ├── worker.c/h        ← Worker：取数据→解析→写文件
│   ├── log_parser.c/h    ← syslog 格式解析
│   └── file_writer.c/h   ← 按 IP+日期 写日志文件
├── systemd/
│   └── log-collector.service  ← systemd 服务配置
└── tests/
    ├── CMakeLists.txt
    ├── test_log_parser.c
    ├── test_file_writer.c
    ├── test_shm_buffer.c
    └── e2e_test.sh
```

一个 `.c` 对应一个职责。这不是教条，是经验——当你在 epoll 代码里找为什么 TCP 连接断掉的时候，你不会想同时看到共享内存的逻辑混在里面。

## common.h — 整个项目的"词汇表"

```c
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
```

> **学习技巧**：不需要记住每个头文件。写代码时 `man 2 函数名` 会告诉你要 `#include` 什么。

## 三个核心数据结构

### config_t — 程序的所有可调参数

```c
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
```

教学项目不需要配置文件。参数直接写在 `config.h` 里，改完重新编译：

```c
/* src/config.h */
#define CFG_LISTEN_ADDR       "0.0.0.0"
#define CFG_TCP_PORT          5140
#define CFG_UDP_PORT          5140
#define CFG_MAX_CONNECTIONS   1024
#define CFG_WORKER_COUNT      4
#define CFG_SLOT_SIZE         4096
#define CFG_SLOT_COUNT        1024
#define CFG_LOG_DIR           "/tmp/log_collector_test"
#define CFG_PID_FILE          "/var/run/log-collector.pid"
```

每个默认值的考量：

- **5140 端口**：syslog 标准端口，与 rsyslog/syslog-ng 兼容
- **4 个 Worker**：IO 密集型任务，Worker 数量不需要等于 CPU 核数。4 个足够处理每秒数千条日志
- **4096 字节槽位**：一页内存的大小（x86 MMU 页面），与页对齐减少 TLB 缺失
- **1024 个槽位**：4096 × 1024 = 4MB 共享内存，对现代系统来说很小，但足够缓冲高峰期日志
- **`/tmp/log_collector_test`**：测试友好，不需要 root 权限

### shm_header_t — 共享内存的元数据

```c
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
```

这里把互斥锁和信号量的知识组合起来了。第 4 篇会深入讲为什么这样设计。

`SHM_MAGIC = 0x4C434F47` 是 "LCOG" 四个字母的 ASCII 码拼成的 32 位整数（L=0x4C, C=0x43, O=0x4F, G=0x47）。Worker 连接共享内存时先检查魔数，防止连到错误的共享内存对象。

### log_slot_t — 槽位头部

```c
typedef struct {
    struct sockaddr_storage client_addr;  /* 客户端地址（IPv4/IPv6 通用） */
    uint8_t  protocol;                    /* 0=TCP, 1=UDP */
    uint32_t data_len;                    /* 日志实际长度 */
    uint64_t timestamp;                   /* 接收时间（epoch 秒） */
    char     data[4096];                  /* 日志正文 */
} log_slot_t;
```

`data[4096]` 是固定大小数组，与槽位大小对齐（`CFG_SLOT_SIZE = 4096`）。C 和 C++ 都支持固定大小数组，代码直观：`slot->data` 就是日志内容，不需要指针算术。

## C/C++ 兼容：为什么以及怎么做

### 为什么要兼容 C++

这个项目用 C 写，但代码可能被 C++ 项目引用，或者学生想把某个模块（比如共享内存环形缓冲区）copy 到自己的 C++ 项目里。如果不做兼容处理，C++ 编译器会报错：

**问题：函数名被"改编"（name mangling）**

C++ 支持函数重载——同一个函数名可以有不同的参数类型。为了实现这个，C++ 编译器会把函数名"改编"成包含参数类型信息的符号。比如 `shm_init` 在 C++ 编译后可能变成 `_Z8shm_initPP13shm_header_tPPvmm`（这个长长的字符串编码了函数名和所有参数类型）。

但 C 不支持重载，C 编译器不改编函数名——`shm_init` 编译后就是 `shm_init`。

问题来了：如果 C++ 代码调用 C 编译的函数，C++ 编译器按改编后的名字去找符号 `_Z8shm_init...`，但 C 编译的 `.o` 文件里符号是 `shm_init`，链接器找不到，报 `undefined reference`。

### 怎么解决

**`extern "C"` 解决函数名改编问题**

在所有头文件的函数声明前后包裹 `extern "C"`：

```c
/* src/shm_buffer.h */
#ifndef SHM_BUFFER_H
#define SHM_BUFFER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

int shm_init(shm_header_t **header, void **slots, ...);
int shm_connect(shm_header_t **header, void **slots, ...);
int shm_produce(shm_header_t *header, void *slots, ...);
int shm_consume(shm_header_t *header, void *slots, ...);
void shm_send_sentinels(shm_header_t *header, int count);
void shm_destroy(shm_header_t *header, void *slots, uint64_t slot_count);
void shm_disconnect(shm_header_t *header, void *slots);

#ifdef __cplusplus
}
#endif

#endif /* SHM_BUFFER_H */
```

`#ifdef __cplusplus` 是关键——这个宏只有 C++ 编译器会定义。C 编译器看不到 `extern "C"` 块，不受影响。C++ 编译器看到后，告诉链接器"这些函数用 C 的命名方式，别改编"。

项目中所有 8 个头文件都做了这个处理：`common.h`、`daemon.h`、`file_writer.h`、`log_parser.h`、`master.h`、`shm_buffer.h`、`signal_handler.h`、`worker.h`。

## 主函数：把零件串起来

```c
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
    int foreground = 0, opt, rc;

    /* 1. 解析参数：只认 -f（前台）和 -h（帮助） */
    while ((opt = getopt(argc, argv, "fh")) != -1) {
        switch (opt) {
        case 'f': foreground = 1; break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    /* 2. 变成守护进程（-f 跳过） */
    if (!foreground && daemonize(CFG_PID_FILE) < 0) {
        fprintf(stderr, "变成守护进程失败\n");
        return 1;
    }

    /* 3. 注册信号 */
    if (signal_handlers_init() < 0) {
        sd_journal_print(LOG_ERR, "信号注册失败");
        if (!foreground) daemon_cleanup(CFG_PID_FILE);
        return 1;
    }

    /* 4. 创建共享内存 */
    if (shm_init(&shm_header, &slots, cfg.slot_size, cfg.slot_count) < 0) {
        sd_journal_print(LOG_ERR, "共享内存初始化失败");
        if (!foreground) daemon_cleanup(CFG_PID_FILE);
        return 1;
    }

    /* 5. Master 主循环（fork Worker + epoll） */
    rc = master_run(&cfg, shm_header, slots);

    /* 6. 清理 */
    shm_destroy(shm_header, slots, cfg.slot_count);
    if (!foreground) daemon_cleanup(CFG_PID_FILE);
    return rc;
}
```

这就是整个程序的骨架。`config_t` 用 C99 的**指定初始化器**（designated initializer）直接初始化，省掉了逐字段赋值的啰嗦代码。

启动流程的 6 个步骤，每一步对应一个模块：

| 步骤 | 操作 | 对应模块 |
|------|------|---------|
| 1 | 解析命令行参数 | getopt |
| 2 | 守护进程化 | daemon.c — double-fork + setsid |
| 3 | 注册信号处理 | signal_handler.c — signal + siginterrupt |
| 4 | 创建共享内存 | shm_buffer.c — shm_open + mmap + 锁/信号量 |
| 5 | Master 事件循环 | master.c — epoll + fork Worker 池 |
| 6 | 清理资源 | shm_unlink + 删除 PID 文件 |

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

| 包名 | 提供什么 | 为什么需要 |
|------|---------|-----------|
| `build-essential` / `base-devel` | gcc, make, libc | 编译 C 程序的基础工具链 |
| `cmake` | cmake, ctest | 构建系统和测试运行器 |
| `pkg-config` | pkg-config | CMake 通过它找到 libsystemd 的路径 |
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
cd log_collector
mkdir build && cd build
cmake .. && make
./log_collector -h
```

输出帮助信息，编译零错误——搞定。接下来每完成一个模块，我们都会跑一遍编译+测试，确保每一步都是可工作的。

> **一个建议**：每读完一篇教程，去看对应的源文件。教程告诉你"为什么这么写"，源代码告诉你"具体怎么写"。两者对着看，效果翻倍。
