# Log Collector 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建基于 Linux 的网络日志收集系统，接收远程 TCP/UDP syslog 日志，按客户端 IP 和日期分文件存储。

**Architecture:** Master-Worker 进程池模式。Master 守护进程负责 epoll 事件循环、TCP/UDP 网络接收、写入共享内存环形缓冲区；Worker 进程负责从缓冲区读取、解析 syslog 格式、写入文件。跨进程同步使用 pthread 互斥锁（PTHREAD_PROCESS_SHARED）和 POSIX 命名信号量。

**Tech Stack:** C99, CMake 3.22+, GCC 11.4, Linux, pthread, POSIX semaphores (librt), epoll, mmap

---

### Task 1: 项目骨架与 CMake 构建系统

**Files:**
- Create: `CMakeLists.txt`
- Create: `include/common.h`
- Create: `src/main.c`
- Create: `conf/log-collector.conf.example`

- [ ] **Step 1: 创建顶层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.10)
project(log_collector C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")

find_package(Threads REQUIRED)

set(COMMON_SOURCES
    src/config.c
    src/shm_buffer.c
    src/log_parser.c
    src/file_writer.c
    src/signal_handler.c
    src/daemon.c
    src/master.c
    src/worker.c
)

add_executable(log_collector
    src/main.c
    ${COMMON_SOURCES}
)

target_include_directories(log_collector PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(log_collector PRIVATE
    Threads::Threads
    rt
)

install(TARGETS log_collector RUNTIME DESTINATION sbin)
install(FILES conf/log-collector.conf.example DESTINATION /etc)
```

- [ ] **Step 2: 创建公共头文件**

```c
/* include/common.h */
#ifndef COMMON_H
#define COMMON_H

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
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <syslog.h>

/* 缓冲区配置常量 */
#define SHM_NAME               "/log_collector_shm"
#define SEM_FREE_NAME          "/log_collector_sem_free"
#define SEM_USED_NAME          "/log_collector_sem_used"
#define SHM_MAGIC              0x4C434F47  /* "LCOG" */
#define SHM_VERSION            1

/* 默认配置值 */
#define DEFAULT_LISTEN_ADDR    "0.0.0.0"
#define DEFAULT_TCP_PORT       5140
#define DEFAULT_UDP_PORT       5140
#define DEFAULT_MAX_CONNS      1024
#define DEFAULT_WORKER_COUNT   4
#define DEFAULT_SLOT_SIZE      4096
#define DEFAULT_SLOT_COUNT     1024
#define DEFAULT_LOG_DIR        "/var/log/collector"
#define DEFAULT_CONF_PATH      "/etc/log-collector.conf"
#define DEFAULT_PID_FILE       "/var/run/log-collector.pid"

/* 网络常量 */
#define MAX_EVENTS             1024
#define TCP_RECV_BUF_SIZE      65536
#define UDP_RECV_BUF_SIZE      65536

/* 槽位头大小 */
#define SLOT_HEADER_SIZE (sizeof(struct sockaddr_storage) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint64_t))

/* 配置结构体 */
typedef struct {
    char     listen_addr[64];
    int      tcp_port;
    int      udp_port;
    int      max_connections;
    int      worker_count;
    uint64_t slot_size;
    uint64_t slot_count;
    char     log_dir[256];
} config_t;

/* 共享内存头部 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t buffer_size;
    uint64_t slot_size;
    uint64_t slot_count;
    uint64_t write_pos;
    uint64_t read_pos;
    pthread_mutex_t mutex;
    /* sem_t 使用命名信号量，不嵌入结构体 */
} shm_header_t;

/* 日志槽位 (柔性数组成员) */
typedef struct {
    struct sockaddr_storage client_addr;
    uint8_t  protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char     data[];
} log_slot_t;

/* 全局状态 */
extern volatile sig_atomic_t g_shutdown;
extern volatile sig_atomic_t g_sighup;
extern volatile sig_atomic_t g_sigchld;

#endif /* COMMON_H */
```

- [ ] **Step 3: 创建 main.c 入口**

```c
/* src/main.c */
#include "common.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("log_collector: starting...\n");
    /* 后续任务实现 */
    return 0;
}
```

- [ ] **Step 4: 创建示例配置文件**

```ini
# conf/log-collector.conf.example
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

- [ ] **Step 5: 构建验证**

```bash
cd /home/gem/project/log_collector
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

期望: 编译成功，生成 `log_collector` 可执行文件

- [ ] **Step 6: Commit**

```bash
cd /home/gem/project/log_collector
git add -A
git commit -m "feat: project skeleton with CMake build system and common headers"
```

---

### Task 2: 配置解析模块

**Files:**
- Create: `src/config.c`
- Create: `src/config.h`

- [ ] **Step 1: 创建 config.h**

```c
/* src/config.h */
#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

/* 加载配置：先设默认值，再读文件覆盖。返回 0 成功，-1 读文件失败(不致命，用默认值) */
int config_load(config_t *cfg, const char *path);

#endif /* CONFIG_H */
```

- [ ] **Step 2: 创建 config.c**

```c
/* src/config.c */
#include "config.h"
#include <ctype.h>

static void config_set_defaults(config_t *cfg) {
    strncpy(cfg->listen_addr, DEFAULT_LISTEN_ADDR, sizeof(cfg->listen_addr) - 1);
    cfg->tcp_port = DEFAULT_TCP_PORT;
    cfg->udp_port = DEFAULT_UDP_PORT;
    cfg->max_connections = DEFAULT_MAX_CONNS;
    cfg->worker_count = DEFAULT_WORKER_COUNT;
    cfg->slot_size = DEFAULT_SLOT_SIZE;
    cfg->slot_count = DEFAULT_SLOT_COUNT;
    strncpy(cfg->log_dir, DEFAULT_LOG_DIR, sizeof(cfg->log_dir) - 1);
}

static void trim_whitespace(char *line) {
    char *end;
    while (isspace((unsigned char)*line)) line++;
    if (*line == '\0') return;
    end = line + strlen(line) - 1;
    while (end > line && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

static int parse_int(const char *str, int *out) {
    char *endptr;
    long val = strtol(str, &endptr, 10);
    if (*endptr != '\0' || val < 0 || val > INT_MAX) return -1;
    *out = (int)val;
    return 0;
}

static int parse_uint64(const char *str, uint64_t *out) {
    char *endptr;
    unsigned long long val = strtoull(str, &endptr, 10);
    if (*endptr != '\0' || val == ULLONG_MAX) return -1;
    *out = (uint64_t)val;
    return 0;
}

static void apply_key_value(config_t *cfg, const char *key, const char *value) {
    if (strcmp(key, "listen_addr") == 0) {
        strncpy(cfg->listen_addr, value, sizeof(cfg->listen_addr) - 1);
    } else if (strcmp(key, "tcp_port") == 0) {
        parse_int(value, &cfg->tcp_port);
    } else if (strcmp(key, "udp_port") == 0) {
        parse_int(value, &cfg->udp_port);
    } else if (strcmp(key, "max_connections") == 0) {
        parse_int(value, &cfg->max_connections);
    } else if (strcmp(key, "worker_count") == 0) {
        parse_int(value, &cfg->worker_count);
    } else if (strcmp(key, "buffer_slot_size") == 0) {
        parse_uint64(value, &cfg->slot_size);
    } else if (strcmp(key, "buffer_slot_count") == 0) {
        parse_uint64(value, &cfg->slot_count);
    } else if (strcmp(key, "log_dir") == 0) {
        strncpy(cfg->log_dir, value, sizeof(cfg->log_dir) - 1);
    }
}

int config_load(config_t *cfg, const char *path) {
    FILE *fp;
    char line[512];
    char key[128], value[384];

    config_set_defaults(cfg);

    if (path == NULL) {
        path = DEFAULT_CONF_PATH;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        /* 配置文件不存在不致命，使用默认值 */
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        /* 移除换行符 */
        line[strcspn(line, "\r\n")] = '\0';
        trim_whitespace(line);

        /* 跳过空行、注释、节头 */
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }

        /* 解析 key=value */
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;

        size_t key_len = (size_t)(eq - line);
        if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
        memcpy(key, line, key_len);
        key[key_len] = '\0';
        trim_whitespace(key);

        strncpy(value, eq + 1, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        trim_whitespace(value);

        apply_key_value(cfg, key, value);
    }

    fclose(fp);
    return 0;
}
```

- [ ] **Step 3: 创建测试**

```c
/* tests/test_config.c */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "config.h"

static void test_defaults(void) {
    config_t cfg;
    int rc = config_load(&cfg, "/nonexistent/path.conf");
    assert(rc == 0);
    assert(strcmp(cfg.listen_addr, "0.0.0.0") == 0);
    assert(cfg.tcp_port == 5140);
    assert(cfg.udp_port == 5140);
    assert(cfg.max_connections == 1024);
    assert(cfg.worker_count == 4);
    assert(cfg.slot_size == 4096);
    assert(cfg.slot_count == 1024);
    assert(strcmp(cfg.log_dir, "/var/log/collector") == 0);
    printf("PASS: test_defaults\n");
}

static void test_parse_config(void) {
    const char *test_conf = "/tmp/test_log_collector.conf";
    FILE *fp = fopen(test_conf, "w");
    fprintf(fp, "[server]\n");
    fprintf(fp, "listen_addr = 192.168.1.1\n");
    fprintf(fp, "tcp_port = 9999\n");
    fprintf(fp, "udp_port = 8888\n");
    fprintf(fp, "max_connections = 512\n");
    fprintf(fp, "[worker]\n");
    fprintf(fp, "worker_count = 8\n");
    fprintf(fp, "buffer_slot_size = 8192\n");
    fprintf(fp, "buffer_slot_count = 2048\n");
    fprintf(fp, "[storage]\n");
    fprintf(fp, "log_dir = /tmp/logs\n");
    fclose(fp);

    config_t cfg;
    int rc = config_load(&cfg, test_conf);
    assert(rc == 0);
    assert(strcmp(cfg.listen_addr, "192.168.1.1") == 0);
    assert(cfg.tcp_port == 9999);
    assert(cfg.udp_port == 8888);
    assert(cfg.max_connections == 512);
    assert(cfg.worker_count == 8);
    assert(cfg.slot_size == 8192);
    assert(cfg.slot_count == 2048);
    assert(strcmp(cfg.log_dir, "/tmp/logs") == 0);
    remove(test_conf);
    printf("PASS: test_parse_config\n");
}

static void test_comments_and_blanks(void) {
    const char *test_conf = "/tmp/test_log_collector_conf2.conf";
    FILE *fp = fopen(test_conf, "w");
    fprintf(fp, "# This is a comment\n");
    fprintf(fp, "; Also a comment\n");
    fprintf(fp, "\n");
    fprintf(fp, "[server]\n");
    fprintf(fp, "tcp_port = 7777\n");
    fprintf(fp, "\n");
    fprintf(fp, "# another comment\n");
    fprintf(fp, "listen_addr = 10.0.0.1\n");
    fclose(fp);

    config_t cfg;
    config_load(&cfg, test_conf);
    assert(cfg.tcp_port == 7777);
    assert(strcmp(cfg.listen_addr, "10.0.0.1") == 0);
    remove(test_conf);
    printf("PASS: test_comments_and_blanks\n");
}

int main(void) {
    test_defaults();
    test_parse_config();
    test_comments_and_blanks();
    printf("All config tests passed!\n");
    return 0;
}
```

- [ ] **Step 4: 添加测试目标到 CMake**

在顶层 `CMakeLists.txt` 的 `install` 命令之前添加:

```cmake
# Tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

创建 `tests/CMakeLists.txt`:

```cmake
add_executable(test_config test_config.c ../src/config.c)
target_include_directories(test_config PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_config PRIVATE Threads::Threads rt)
add_test(NAME test_config COMMAND test_config)
```

- [ ] **Step 5: 构建并运行测试**

```bash
cd /home/gem/project/log_collector/build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
./tests/test_config
```

期望: 所有测试通过

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: config parser with tests"
```

---

### Task 3: 共享内存环形缓冲区

**Files:**
- Create: `src/shm_buffer.c`
- Create: `src/shm_buffer.h`
- Create: `tests/test_shm_buffer.c`

- [ ] **Step 1: 创建 shm_buffer.h**

```c
/* src/shm_buffer.h */
#ifndef SHM_BUFFER_H
#define SHM_BUFFER_H

#include "common.h"

/* 初始化共享内存 (仅 Master 调用): 创建 shm, 设置大小, 映射, 初始化头部和信号量 */
int shm_init(shm_header_t **header, void **slots, uint64_t slot_size,
             uint64_t slot_count);

/* Worker 连接已有共享内存: 打开 shm, 映射, 打开信号量 */
int shm_connect(shm_header_t **header, void **slots,
                uint64_t *slot_size, uint64_t *slot_count);

/* 生产者写入一条日志 (Master 调用) */
int shm_produce(shm_header_t *header, void *slots,
                const struct sockaddr_storage *addr, uint8_t protocol,
                const char *data, uint32_t data_len, uint64_t timestamp);

/* 消费者读取一条日志 (Worker 调用)。返回 data_len, 0 表示哨兵(退出), -1 表示无数据 */
int shm_consume(shm_header_t *header, void *slots, uint64_t slot_size,
                struct sockaddr_storage *addr, uint8_t *protocol,
                char *data, uint32_t *data_len, uint64_t *timestamp);

/* 向缓冲区发送 N 个哨兵 (通知 Worker 退出) */
void shm_send_sentinels(shm_header_t *header, int count);

/* 清理共享内存 (仅 Master 调用) */
void shm_destroy(shm_header_t *header, void *slots, uint64_t slot_count);

/* Worker 端断开连接 */
void shm_disconnect(shm_header_t *header, void *slots);

#endif /* SHM_BUFFER_H */
```

- [ ] **Step 2: 创建 shm_buffer.c**

```c
/* src/shm_buffer.c */
#include "shm_buffer.h"

int shm_init(shm_header_t **header_out, void **slots_out,
             uint64_t slot_size, uint64_t slot_count) {
    uint64_t total_size;
    size_t shm_size;
    int shm_fd;
    shm_header_t *header;
    void *slots;
    pthread_mutexattr_t mutex_attr;

    /* 计算总大小 */
    total_size = sizeof(shm_header_t) + slot_size * slot_count;
    if (total_size > (uint64_t)SIZE_MAX) {
        return -1;
    }
    shm_size = (size_t)total_size;

    /* 创建共享内存 */
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (shm_fd < 0) {
        /* 可能上次未清理，尝试先 unlink 再创建 */
        shm_unlink(SHM_NAME);
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
        if (shm_fd < 0) {
            return -1;
        }
    }

    if (ftruncate(shm_fd, (off_t)shm_size) < 0) {
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return -1;
    }

    /* mmap 映射 */
    header = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd); /* mmap 后可以关闭 fd */
    if (header == MAP_FAILED) {
        shm_unlink(SHM_NAME);
        return -1;
    }

    /* 初始化头部 */
    header->magic = SHM_MAGIC;
    header->version = SHM_VERSION;
    header->buffer_size = shm_size;
    header->slot_size = slot_size;
    header->slot_count = slot_count;
    header->write_pos = 0;
    header->read_pos = 0;

    /* 初始化跨进程互斥锁 */
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&header->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    /* 创建命名信号量 */
    sem_unlink(SEM_FREE_NAME);
    sem_unlink(SEM_USED_NAME);
    /* sem_open 在 C99 无 O_CREAT 宏跨平台问题，这里直接用 sem_open */
    sem_t *sem_free = sem_open(SEM_FREE_NAME, O_CREAT | O_EXCL, 0600, (unsigned int)slot_count);
    sem_t *sem_used = sem_open(SEM_USED_NAME, O_CREAT | O_EXCL, 0600, 0);
    if (sem_free == SEM_FAILED || sem_used == SEM_FAILED) {
        if (sem_free != SEM_FAILED) sem_close(sem_free);
        if (sem_used != SEM_FAILED) sem_close(sem_used);
        sem_unlink(SEM_FREE_NAME);
        sem_unlink(SEM_USED_NAME);
        pthread_mutex_destroy(&header->mutex);
        munmap(header, shm_size);
        shm_unlink(SHM_NAME);
        return -1;
    }
    sem_close(sem_free);
    sem_close(sem_used);

    /* 清零槽位区 */
    slots = (void *)((char *)header + sizeof(shm_header_t));
    memset(slots, 0, (size_t)(slot_size * slot_count));

    *header_out = header;
    *slots_out = slots;
    return 0;
}

static inline log_slot_t *get_slot(void *slots, uint64_t slot_size, uint64_t index) {
    return (log_slot_t *)((char *)slots + slot_size * index);
}

int shm_connect(shm_header_t **header_out, void **slots_out,
                uint64_t *slot_size, uint64_t *slot_count) {
    int shm_fd;
    shm_header_t *header;
    size_t shm_size;

    shm_fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (shm_fd < 0) return -1;

    /* 先读取头部获取大小 */
    header = mmap(NULL, sizeof(shm_header_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (header == MAP_FAILED) {
        close(shm_fd);
        return -1;
    }

    if (header->magic != SHM_MAGIC) {
        munmap(header, sizeof(shm_header_t));
        close(shm_fd);
        return -1;
    }

    shm_size = (size_t)header->buffer_size;

    /* 重新映射完整区域 */
    munmap(header, sizeof(shm_header_t));
    header = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (header == MAP_FAILED) return -1;

    *header_out = header;
    *slots_out = (void *)((char *)header + sizeof(shm_header_t));
    *slot_size = header->slot_size;
    *slot_count = header->slot_count;
    return 0;
}

int shm_produce(shm_header_t *header, void *slots,
                const struct sockaddr_storage *addr, uint8_t protocol,
                const char *data, uint32_t data_len, uint64_t timestamp) {
    sem_t *sem_free;
    log_slot_t *slot;

    sem_free = sem_open(SEM_FREE_NAME, 0);
    if (sem_free == SEM_FAILED) return -1;

    /* 非阻塞尝试获取空闲槽位，满则丢弃 */
    if (sem_trywait(sem_free) != 0) {
        sem_close(sem_free);
        return -1; /* 缓冲区满 */
    }
    sem_close(sem_free);

    pthread_mutex_lock(&header->mutex);

    slot = get_slot(slots, header->slot_size, header->write_pos);
    if (addr != NULL) {
        memcpy(&slot->client_addr, addr, sizeof(struct sockaddr_storage));
    }
    slot->protocol = protocol;
    slot->data_len = data_len;
    slot->timestamp = timestamp;
    memcpy(slot->data, data, data_len);
    slot->data[data_len] = '\0';

    header->write_pos = (header->write_pos + 1) % header->slot_count;

    pthread_mutex_unlock(&header->mutex);

    /* 通知消费者 */
    sem_t *sem_used = sem_open(SEM_USED_NAME, 0);
    if (sem_used != SEM_FAILED) {
        sem_post(sem_used);
        sem_close(sem_used);
    }

    return 0;
}

int shm_consume(shm_header_t *header, void *slots, uint64_t slot_size,
                struct sockaddr_storage *addr, uint8_t *protocol,
                char *data, uint32_t *data_len, uint64_t *timestamp) {
    log_slot_t *slot;

    sem_t *sem_used = sem_open(SEM_USED_NAME, 0);
    if (sem_used == SEM_FAILED) return -1;

    /* 阻塞等待 */
    sem_wait(sem_used);
    sem_close(sem_used);

    pthread_mutex_lock(&header->mutex);

    slot = get_slot(slots, header->slot_size, header->read_pos);

    if (slot->data_len == 0) {
        /* 哨兵 */
        header->read_pos = (header->read_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);
        /* 释放一个空闲槽位 */
        sem_t *sem_free = sem_open(SEM_FREE_NAME, 0);
        if (sem_free != SEM_FAILED) {
            sem_post(sem_free);
            sem_close(sem_free);
        }
        return 0;
    }

    memcpy(addr, &slot->client_addr, sizeof(struct sockaddr_storage));
    *protocol = slot->protocol;
    *data_len = slot->data_len;
    *timestamp = slot->timestamp;
    memcpy(data, slot->data, slot->data_len);
    data[slot->data_len] = '\0';

    header->read_pos = (header->read_pos + 1) % header->slot_count;

    pthread_mutex_unlock(&header->mutex);

    /* 释放一个空闲槽位 */
    sem_t *sem_free = sem_open(SEM_FREE_NAME, 0);
    if (sem_free != SEM_FAILED) {
        sem_post(sem_free);
        sem_close(sem_free);
    }

    return (int)slot->data_len;
}

void shm_send_sentinels(shm_header_t *header, int count) {
    int i;
    log_slot_t *slot;
    sem_t *sem_free;

    for (i = 0; i < count; i++) {
        sem_free = sem_open(SEM_FREE_NAME, 0);
        if (sem_free == SEM_FAILED) return;
        /* 阻塞直到有空槽位 */
        sem_wait(sem_free);
        sem_close(sem_free);

        pthread_mutex_lock(&header->mutex);
        slot = get_slot((void *)((char *)header + sizeof(shm_header_t)),
                        header->slot_size, header->write_pos);
        memset(slot, 0, (size_t)header->slot_size);
        slot->data_len = 0; /* 哨兵标记 */
        header->write_pos = (header->write_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);

        sem_t *sem_used = sem_open(SEM_USED_NAME, 0);
        if (sem_used != SEM_FAILED) {
            sem_post(sem_used);
            sem_close(sem_used);
        }
    }
}

void shm_destroy(shm_header_t *header, void *slots, uint64_t slot_count) {
    (void)slots;
    (void)slot_count;

    pthread_mutex_destroy(&header->mutex);
    munmap(header, (size_t)header->buffer_size);
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_FREE_NAME);
    sem_unlink(SEM_USED_NAME);
}

void shm_disconnect(shm_header_t *header, void *slots) {
    (void)slots;
    munmap(header, (size_t)header->buffer_size);
}
```

- [ ] **Step 3: 创建测试**

```c
/* tests/test_shm_buffer.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include "shm_buffer.h"

static void test_init_and_destroy(void) {
    shm_header_t *header;
    void *slots;
    int rc;

    rc = shm_init(&header, &slots, 4096, 16);
    assert(rc == 0);
    assert(header->magic == SHM_MAGIC);
    assert(header->version == SHM_VERSION);
    assert(header->slot_size == 4096);
    assert(header->slot_count == 16);
    assert(header->write_pos == 0);
    assert(header->read_pos == 0);

    shm_destroy(header, slots, 16);
    printf("PASS: test_init_and_destroy\n");
}

static void test_produce_consume_single_process(void) {
    shm_header_t *header;
    void *slots;
    int rc;
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    uint8_t protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char data[4096];

    shm_init(&header, &slots, 4096, 16);

    /* 模拟客户端地址 */
    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    sin->sin_port = htons(12345);
    inet_pton(AF_INET, "192.168.1.100", &sin->sin_addr);

    /* 写入 */
    const char *msg = "<13>Jun 18 14:32:05 myhost myapp[1234]: test message";
    rc = shm_produce(header, slots, &addr, 0, msg, (uint32_t)strlen(msg), 1655562725);
    /* 同一进程生产消费会死锁，这里改为测试连接语义 */
    (void)rc;

    shm_destroy(header, slots, 16);
    printf("PASS: test_produce_consume_single_process (init/destroy only, full test needs fork)\n");
}

static void test_fork_produce_consume(void) {
    pid_t pid;
    shm_header_t *header;
    void *slots;
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    uint8_t protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char data[4096];
    int rc;

    shm_init(&header, &slots, 4096, 16);

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        /* 子进程: 消费者 */
        shm_header_t *child_header;
        void *child_slots;
        uint64_t child_slot_size, child_slot_count;

        rc = shm_connect(&child_header, &child_slots, &child_slot_size, &child_slot_count);
        assert(rc == 0);
        assert(child_slot_size == 4096);
        assert(child_slot_count == 16);

        /* 消费数据 */
        data_len = 0;
        rc = shm_consume(child_header, child_slots, child_slot_size, &addr,
                         &protocol, data, &data_len, &timestamp);
        assert(rc > 0);
        assert(data_len == 55);
        assert(protocol == 1);
        assert(strcmp(data, "<13>test message from UDP") == 0);

        shm_disconnect(child_header, child_slots);
        _exit(0);
    } else {
        /* 父进程: 生产者 */
        usleep(50000); /* 等子进程连接好 */

        memset(&addr, 0, sizeof(addr));
        sin = (struct sockaddr_in *)&addr;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(12345);
        inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

        const char *msg = "<13>test message from UDP";
        rc = shm_produce(header, slots, &addr, 1, msg, (uint32_t)strlen(msg), 1655562725);
        assert(rc == 0);

        waitpid(pid, NULL, 0);
        shm_destroy(header, slots, 16);
    }
    printf("PASS: test_fork_produce_consume\n");
}

static void test_sentinel(void) {
    pid_t pid;
    shm_header_t *header;
    void *slots;
    struct sockaddr_storage addr;
    uint8_t protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char data[4096];
    int rc;

    shm_init(&header, &slots, 4096, 16);

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        shm_header_t *child_header;
        void *child_slots;
        uint64_t child_slot_size, child_slot_count;

        shm_connect(&child_header, &child_slots, &child_slot_size, &child_slot_count);

        /* 应该收到哨兵 (data_len=0) */
        data_len = 1;
        rc = shm_consume(child_header, child_slots, child_slot_size, &addr,
                         &protocol, data, &data_len, &timestamp);
        assert(rc == 0);
        assert(data_len == 0);

        shm_disconnect(child_header, child_slots);
        _exit(0);
    } else {
        usleep(50000);
        shm_send_sentinels(header, 1);
        waitpid(pid, NULL, 0);
        shm_destroy(header, slots, 16);
    }
    printf("PASS: test_sentinel\n");
}

int main(void) {
    test_init_and_destroy();
    test_produce_consume_single_process();
    test_fork_produce_consume();
    test_sentinel();
    printf("All shm_buffer tests passed!\n");
    return 0;
}
```

- [ ] **Step 4: 更新 tests/CMakeLists.txt**

```cmake
add_executable(test_config test_config.c ../src/config.c)
target_include_directories(test_config PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_config PRIVATE Threads::Threads rt)
add_test(NAME test_config COMMAND test_config)

add_executable(test_shm_buffer test_shm_buffer.c ../src/shm_buffer.c)
target_include_directories(test_shm_buffer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_shm_buffer PRIVATE Threads::Threads rt)
add_test(NAME test_shm_buffer COMMAND test_shm_buffer)
```

- [ ] **Step 5: 构建并运行测试**

```bash
cd /home/gem/project/log_collector/build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
./tests/test_shm_buffer
```

期望: 所有测试通过

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: shared memory ring buffer with producer/consumer and tests"
```

---

### Task 4: Syslog 解析器

**Files:**
- Create: `src/log_parser.c`
- Create: `src/log_parser.h`
- Create: `tests/test_log_parser.c`

- [ ] **Step 1: 创建 log_parser.h**

```c
/* src/log_parser.h */
#ifndef LOG_PARSER_H
#define LOG_PARSER_H

#include "common.h"

/* 解析 syslog 消息，返回格式化后的行 (包含换行符)。
   输出缓冲区由调用者提供，out_size 指定大小。
   返回实际写入的字节数(不含 NUL)，-1 表示错误。 */
int log_parser_format(const struct sockaddr_storage *addr,
                      uint64_t recv_timestamp,
                      const char *raw_msg, uint32_t raw_len,
                      char *out, size_t out_size);

#endif /* LOG_PARSER_H */
```

- [ ] **Step 2: 创建 log_parser.c**

```c
/* src/log_parser.c */
#include "log_parser.h"

static const char *severity_names[] = {
    "emerg", "alert", "crit", "err",
    "warning", "notice", "info", "debug"
};

/* 将 sockaddr 转换为 IP 字符串 */
static const char *addr_to_str(const struct sockaddr_storage *addr,
                               char *buf, size_t buf_size) {
    if (addr->ss_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
        inet_ntop(AF_INET, &sin->sin_addr, buf, (socklen_t)buf_size);
    } else if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)addr;
        inet_ntop(AF_INET6, &sin6->sin6_addr, buf, (socklen_t)buf_size);
    } else {
        strncpy(buf, "unknown", buf_size - 1);
    }
    return buf;
}

/* 从 <PRI> 前缀中提取 severity */
static int extract_pri(const char *msg, uint32_t len, int *severity_out) {
    const char *end;
    long pri;

    if (len < 3 || msg[0] != '<') return -1;

    end = memchr(msg, '>', len);
    if (end == NULL) return -1;

    pri = strtol(msg + 1, NULL, 10);
    *severity_out = (int)(pri & 0x07);
    return (int)(end - msg + 1);
}

int log_parser_format(const struct sockaddr_storage *addr,
                      uint64_t recv_timestamp,
                      const char *raw_msg, uint32_t raw_len,
                      char *out, size_t out_size) {
    char ip_str[INET6_ADDRSTRLEN];
    char time_buf[32];
    time_t ts;
    struct tm tm_info;
    int severity = 7; /* 默认 debug */
    int pri_len;
    const char *body;
    uint32_t body_len;
    int written;

    /* 时间戳转换 */
    ts = (time_t)recv_timestamp;
    localtime_r(&ts, &tm_info);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S%z", &tm_info);

    /* IP 地址 */
    addr_to_str(addr, ip_str, sizeof(ip_str));

    /* 提取 PRI */
    pri_len = extract_pri(raw_msg, raw_len, &severity);
    if (pri_len > 0) {
        body = raw_msg + pri_len;
        body_len = raw_len - (uint32_t)pri_len;
        /* 跳过前导空格 */
        while (body_len > 0 && *body == ' ') {
            body++;
            body_len--;
        }
    } else {
        body = raw_msg;
        body_len = raw_len;
    }

    /* 格式化输出: timestamp ip [level] message */
    written = snprintf(out, out_size, "%s %s [%s] ",
                       time_buf, ip_str, severity_names[severity]);

    if (written < 0 || (size_t)written >= out_size) return -1;

    /* 追加消息体 (确保不溢出) */
    size_t remaining = out_size - (size_t)written;
    size_t copy_len = (size_t)body_len < remaining - 1 ? (size_t)body_len : remaining - 1;
    memcpy(out + written, body, copy_len);
    written += (int)copy_len;

    /* 确保以换行结束 */
    if (written > 0 && out[written - 1] != '\n') {
        if ((size_t)written < out_size - 1) {
            out[written] = '\n';
            written++;
        }
    }
    out[written] = '\0';

    return written;
}
```

- [ ] **Step 3: 创建测试**

```c
/* tests/test_log_parser.c */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "log_parser.h"

static void test_basic_syslog(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    int len;
    const char *msg = "<13>Jun 18 14:32:05 myhost myapp[1234]: connection timeout";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.100", &sin->sin_addr);

    len = log_parser_format(&addr, 1655562725, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "192.168.1.100") != NULL);
    assert(strstr(out, "[warning]") != NULL);  /* severity 5 = warning (13 & 0x07 = 5) */
    assert(strstr(out, "connection timeout") != NULL);
    assert(out[len - 1] == '\n');
    printf("PASS: test_basic_syslog\n");
}

static void test_no_pri(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    int len;
    const char *msg = "plain message without priority";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.55", &sin->sin_addr);

    len = log_parser_format(&addr, 1655562725, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[debug]") != NULL); /* 默认级别 */
    assert(strstr(out, "plain message") != NULL);
    printf("PASS: test_no_pri\n");
}

static void test_emerg_severity(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    int len;
    const char *msg = "<0>system is on fire";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[emerg]") != NULL);
    assert(strstr(out, "system is on fire") != NULL);
    printf("PASS: test_emerg_severity\n");
}

static void test_ipv6(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in6 *sin6;
    char out[1024];
    int len;
    const char *msg = "<14>test ipv6 client";

    memset(&addr, 0, sizeof(addr));
    sin6 = (struct sockaddr_in6 *)&addr;
    sin6->sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &sin6->sin6_addr);

    len = log_parser_format(&addr, 1655562725, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[info]") != NULL); /* severity 6 = info (14 & 0x07 = 6) */
    printf("PASS: test_ipv6\n");
}

int main(void) {
    test_basic_syslog();
    test_no_pri();
    test_emerg_severity();
    test_ipv6();
    printf("All log_parser tests passed!\n");
    return 0;
}
```

- [ ] **Step 4: 更新 tests/CMakeLists.txt**

在文件末尾追加:

```cmake
add_executable(test_log_parser test_log_parser.c ../src/log_parser.c)
target_include_directories(test_log_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_log_parser PRIVATE Threads::Threads rt)
add_test(NAME test_log_parser COMMAND test_log_parser)
```

- [ ] **Step 5: 构建并运行测试**

```bash
cd /home/gem/project/log_collector/build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
./tests/test_log_parser
```

期望: 所有测试通过

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: syslog parser with severity extraction and tests"
```

---

### Task 5: 文件写入模块

**Files:**
- Create: `src/file_writer.c`
- Create: `src/file_writer.h`
- Create: `tests/test_file_writer.c`

- [ ] **Step 1: 创建 file_writer.h**

```c
/* src/file_writer.h */
#ifndef FILE_WRITER_H
#define FILE_WRITER_H

#include "common.h"

/* Worker 文件写入上下文 */
typedef struct {
    int   fd;                 /* 当前文件句柄 */
    char  current_ip[INET6_ADDRSTRLEN];  /* 当前 IP */
    char  current_date[16];   /* 当前日期 YYYY-MM-DD */
    char  log_dir[256];       /* 日志根目录 */
} file_writer_t;

/* 初始化 file_writer */
void file_writer_init(file_writer_t *fw, const char *log_dir);

/* 写入一条格式化日志，自动处理目录创建和日期切换。
   返回 0 成功，-1 失败。 */
int file_writer_write(file_writer_t *fw, const char *ip,
                      const char *data, int data_len);

/* 关闭当前文件 */
void file_writer_close(file_writer_t *fw);

#endif /* FILE_WRITER_H */
```

- [ ] **Step 2: 创建 file_writer.c**

```c
/* src/file_writer.c */
#include "file_writer.h"

static void get_current_date(char *buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(buf, buf_size, "%Y-%m-%d", &tm_info);
}

static int ensure_directory(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(dir, 0755);
}

void file_writer_init(file_writer_t *fw, const char *log_dir) {
    strncpy(fw->log_dir, log_dir, sizeof(fw->log_dir) - 1);
    fw->log_dir[sizeof(fw->log_dir) - 1] = '\0';
    fw->fd = -1;
    fw->current_ip[0] = '\0';
    fw->current_date[0] = '\0';

    /* 确保日志根目录存在 */
    ensure_directory(fw->log_dir);
}

int file_writer_write(file_writer_t *fw, const char *ip,
                      const char *data, int data_len) {
    char today[16];
    char dir_path[320];
    char file_path[384];
    struct stat st;

    get_current_date(today, sizeof(today));

    /* 检查是否需要切换文件 */
    if (fw->fd < 0 ||
        strcmp(fw->current_ip, ip) != 0 ||
        strcmp(fw->current_date, today) != 0) {

        /* 关闭旧文件 */
        if (fw->fd >= 0) {
            close(fw->fd);
            fw->fd = -1;
        }

        /* 创建客户端目录 */
        snprintf(dir_path, sizeof(dir_path), "%s/%s", fw->log_dir, ip);
        if (ensure_directory(dir_path) < 0) return -1;

        /* 打开日志文件 */
        snprintf(file_path, sizeof(file_path), "%s/%s.log", dir_path, today);
        fw->fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fw->fd < 0) return -1;

        strncpy(fw->current_ip, ip, sizeof(fw->current_ip) - 1);
        strncpy(fw->current_date, today, sizeof(fw->current_date) - 1);
    }

    /* 写入 */
    ssize_t written = write(fw->fd, data, (size_t)data_len);
    return (written == (ssize_t)data_len) ? 0 : -1;
}

void file_writer_close(file_writer_t *fw) {
    if (fw->fd >= 0) {
        close(fw->fd);
        fw->fd = -1;
    }
}
```

- [ ] **Step 3: 创建测试**

```c
/* tests/test_file_writer.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include "file_writer.h"

static void test_write_and_date_switch(void) {
    file_writer_t fw;
    char buf[256];
    char file_path[384];
    char today[16];

    file_writer_init(&fw, "/tmp/test_log_collector_writer");

    /* 写入日志 */
    int rc = file_writer_write(&fw, "192.168.1.1", "test log entry 1\n", 18);
    assert(rc == 0);

    /* 验证文件存在 */
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(today, sizeof(today), "%Y-%m-%d", &tm_info);

    snprintf(file_path, sizeof(file_path),
             "/tmp/test_log_collector_writer/192.168.1.1/%s.log", today);

    int fd = open(file_path, O_RDONLY);
    assert(fd >= 0);
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    assert(n > 0);
    buf[n] = '\0';
    assert(strstr(buf, "test log entry 1") != NULL);
    close(fd);

    /* 写入同一 IP 的另一条 */
    rc = file_writer_write(&fw, "192.168.1.1", "test log entry 2\n", 18);
    assert(rc == 0);

    file_writer_close(&fw);

    /* 清理 */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf /tmp/test_log_collector_writer");
    system(cmd);

    printf("PASS: test_write_and_date_switch\n");
}

static void test_ip_switch(void) {
    file_writer_t fw;
    char today[16];

    file_writer_init(&fw, "/tmp/test_log_collector_writer2");

    file_writer_write(&fw, "10.0.0.1", "msg from host1\n", 15);
    file_writer_write(&fw, "10.0.0.2", "msg from host2\n", 15);

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(today, sizeof(today), "%Y-%m-%d", &tm_info);

    /* 两个目录都应该存在 */
    struct stat st;
    assert(stat("/tmp/test_log_collector_writer2/10.0.0.1", &st) == 0);
    assert(stat("/tmp/test_log_collector_writer2/10.0.0.2", &st) == 0);

    file_writer_close(&fw);
    system("rm -rf /tmp/test_log_collector_writer2");

    printf("PASS: test_ip_switch\n");
}

int main(void) {
    test_write_and_date_switch();
    test_ip_switch();
    printf("All file_writer tests passed!\n");
    return 0;
}
```

- [ ] **Step 4: 更新 tests/CMakeLists.txt**

追加:

```cmake
add_executable(test_file_writer test_file_writer.c ../src/file_writer.c)
target_include_directories(test_file_writer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_file_writer PRIVATE Threads::Threads rt)
add_test(NAME test_file_writer COMMAND test_file_writer)
```

- [ ] **Step 5: 构建并运行测试**

```bash
cd /home/gem/project/log_collector/build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
./tests/test_file_writer
```

期望: 所有测试通过

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: file writer with directory auto-creation and date switching"
```

---

### Task 6: 守护进程化

**Files:**
- Create: `src/daemon.c`
- Create: `src/daemon.h`

- [ ] **Step 1: 创建 daemon.h**

```c
/* src/daemon.h */
#ifndef DAEMON_H
#define DAEMON_H

#include "common.h"

/* 将当前进程变为守护进程。成功时不返回(父进程已退出), 失败返回 -1。 */
int daemonize(const char *pid_file);

/* 删除 PID 文件 */
void daemon_cleanup(const char *pid_file);

#endif /* DAEMON_H */
```

- [ ] **Step 2: 创建 daemon.c**

```c
/* src/daemon.c */
#include "daemon.h"

static int write_pid_file(const char *pid_file) {
    FILE *fp = fopen(pid_file, "w");
    if (fp == NULL) return -1;
    fprintf(fp, "%ld\n", (long)getpid());
    fclose(fp);
    return 0;
}

int daemonize(const char *pid_file) {
    pid_t pid;
    int fd;

    /* 第一次 fork */
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0); /* 父进程退出 */

    /* 创建新会话 */
    if (setsid() < 0) return -1;

    /* 第二次 fork (确保不是会话首进程) */
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    /* 切换工作目录 */
    if (chdir("/") < 0) return -1;

    /* 设置文件创建掩码 */
    umask(0);

    /* 关闭标准文件描述符 */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* 重定向到 /dev/null */
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        if (dup(fd) < 0 || dup(fd) < 0) {
            /* 忽略错误 */
        }
        if (fd > STDERR_FILENO) close(fd);
    }

    /* 写 PID 文件 */
    if (write_pid_file(pid_file) < 0) return -1;

    return 0;
}

void daemon_cleanup(const char *pid_file) {
    unlink(pid_file);
}
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: daemonize with double-fork and PID file management"
```

---

### Task 7: 信号处理

**Files:**
- Create: `src/signal_handler.c`
- Create: `src/signal_handler.h`

- [ ] **Step 1: 创建 signal_handler.h**

```c
/* src/signal_handler.h */
#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include "common.h"

/* 全局标志 (定义在 signal_handler.c) */
extern volatile sig_atomic_t g_shutdown;
extern volatile sig_atomic_t g_sighup;
extern volatile sig_atomic_t g_sigchld;

/* 注册所有信号处理 */
int signal_handlers_init(void);

/* 阻塞等待信号 (用于信号主循环，如 SIGTERM) */
void signal_wait_for_shutdown(void);

#endif /* SIGNAL_HANDLER_H */
```

- [ ] **Step 2: 创建 signal_handler.c**

```c
/* src/signal_handler.c */
#include "signal_handler.h"

volatile sig_atomic_t g_shutdown = 0;
volatile sig_atomic_t g_sighup = 0;
volatile sig_atomic_t g_sigchld = 0;

static void handle_sigterm(int sig) {
    (void)sig;
    g_shutdown = 1;
}

static void handle_sighup(int sig) {
    (void)sig;
    g_sighup = 1;
}

static void handle_sigchld(int sig) {
    (void)sig;
    g_sigchld = 1;
}

int signal_handlers_init(void) {
    struct sigaction sa;

    /* SIGTERM / SIGINT */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) < 0) return -1;
    if (sigaction(SIGINT, &sa, NULL) < 0) return -1;

    /* SIGHUP */
    sa.sa_handler = handle_sighup;
    if (sigaction(SIGHUP, &sa, NULL) < 0) return -1;

    /* SIGCHLD */
    sa.sa_handler = handle_sigchld;
    sa.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) return -1;

    /* SIGPIPE: 忽略 */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, NULL) < 0) return -1;

    return 0;
}

void signal_wait_for_shutdown(void) {
    sigset_t mask;
    sigemptyset(&mask);
    while (!g_shutdown) {
        sigsuspend(&mask);
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: signal handlers for SIGTERM, SIGHUP, SIGCHLD, SIGPIPE"
```

---

### Task 8: Master 进程 (epoll 网络接收)

**Files:**
- Create: `src/master.c`
- Create: `src/master.h`

- [ ] **Step 1: 创建 master.h**

```c
/* src/master.h */
#ifndef MASTER_H
#define MASTER_H

#include "common.h"

/* Master 主循环: 初始化网络, epoll 事件循环, 进程池管理。
   直到 shutdown 才返回。返回 0 正常退出, -1 错误。 */
int master_run(const config_t *cfg, shm_header_t *shm_header, void *slots);

#endif /* MASTER_H */
```

- [ ] **Step 2: 创建 master.c (带 TCP/UDP/epoll/进程池)**

```c
/* src/master.c */
#include "master.h"
#include "shm_buffer.h"
#include "signal_handler.h"

/* TCP 客户端连接状态 */
typedef struct {
    int  fd;
    char recv_buf[TCP_RECV_BUF_SIZE];
    int  buf_len;
} tcp_client_t;

/* 全局 actor 引用 (用于 SIGHUP 重载) */
static struct {
    int             epoll_fd;
    int             tcp_fd;
    int             udp_fd;
    tcp_client_t   *clients;
    shm_header_t   *shm_header;
    void           *slots;
    pid_t          *worker_pids;
    int             worker_count;
    config_t        config;
} master_ctx;

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int create_tcp_listener(const char *addr_str, int port) {
    int fd;
    struct sockaddr_in addr;
    int optval = 1;

    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr_str, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, SOMAXCONN) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int create_udp_listener(const char *addr_str, int port) {
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr_str, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static uint64_t get_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec;
}

/* 处理 TCP 新连接 */
static void handle_accept(master_ctx *ctx) {
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd;
    int i;
    struct epoll_event ev;

    client_fd = accept4(ctx->tcp_fd, (struct sockaddr *)&client_addr,
                        &addr_len, SOCK_NONBLOCK);
    if (client_fd < 0) return;

    /* 查找空闲槽位 */
    for (i = 0; i < ctx->config.max_connections; i++) {
        if (ctx->clients[i].fd < 0) {
            ctx->clients[i].fd = client_fd;
            ctx->clients[i].buf_len = 0;

            memset(&ev, 0, sizeof(ev));
            ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
            ev.data.ptr = &ctx->clients[i];
            if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                close(client_fd);
                ctx->clients[i].fd = -1;
            }
            return;
        }
    }

    /* 连接数满，拒绝 */
    close(client_fd);
}

/* 处理 TCP 客户端数据 */
static void handle_tcp_data(master_ctx *ctx, tcp_client_t *client) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    char *data;
    int n;
    char *newline;

    if (getpeername(client->fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        return;
    }

    while (1) {
        n = (int)read(client->fd,
                      client->recv_buf + client->buf_len,
                      (size_t)(TCP_RECV_BUF_SIZE - client->buf_len - 1));

        if (n > 0) {
            client->buf_len += n;
            client->recv_buf[client->buf_len] = '\0';

            /* 提取所有完整行 */
            data = client->recv_buf;
            while ((newline = strchr(data, '\n')) != NULL) {
                *newline = '\0';
                shm_produce(ctx->shm_header, ctx->slots, &addr, 0,
                            data, (uint32_t)strlen(data), get_timestamp());
                data = newline + 1;
            }

            /* 将剩余数据移到缓冲区开头 */
            if (data > client->recv_buf) {
                client->buf_len -= (int)(data - client->recv_buf);
                memmove(client->recv_buf, data, (size_t)(client->buf_len + 1));
            }
        } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
            /* 连接关闭或错误 */
            epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
            close(client->fd);
            client->fd = -1;
            client->buf_len = 0;
            return;
        } else {
            /* EAGAIN */
            break;
        }
    }
}

/* 处理 UDP 数据 */
static void handle_udp_data(master_ctx *ctx) {
    char buf[UDP_RECV_BUF_SIZE];
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    ssize_t n;

    while (1) {
        n = recvfrom(ctx->udp_fd, buf, sizeof(buf) - 1, 0,
                     (struct sockaddr *)&client_addr, &addr_len);
        if (n > 0) {
            buf[n] = '\0';
            /* 去除末尾换行 */
            if (buf[n - 1] == '\n') {
                buf[n - 1] = '\0';
                n--;
            }
            shm_produce(ctx->shm_header, ctx->slots, &client_addr, 1,
                        buf, (uint32_t)n, get_timestamp());
        } else {
            break; /* EAGAIN 或无数据 */
        }
    }
}

/* 回收已退出的 Worker */
static void reap_workers(master_ctx *ctx) {
    pid_t pid;
    int status;
    int i;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (i = 0; i < ctx->worker_count; i++) {
            if (ctx->worker_pids[i] == pid) {
                /* 重新 fork 补充 Worker */
                pid_t new_pid = fork();
                if (new_pid == 0) {
                    /* 子进程: 执行 Worker */
                    worker_run(&ctx->config);
                    _exit(0);
                } else if (new_pid > 0) {
                    ctx->worker_pids[i] = new_pid;
                } else {
                    /* fork 失败 */
                    ctx->worker_pids[i] = 0;
                }
                break;
            }
        }
    }
}

int master_run(const config_t *cfg, shm_header_t *shm_header, void *slots) {
    int i;
    struct epoll_event ev, events[MAX_EVENTS];

    /* 初始化 master 上下文 */
    memset(&master_ctx, 0, sizeof(master_ctx));
    master_ctx.tcp_fd = -1;
    master_ctx.udp_fd = -1;
    master_ctx.shm_header = shm_header;
    master_ctx.slots = slots;
    memcpy(&master_ctx.config, cfg, sizeof(config_t));

    /* 创建 epoll */
    master_ctx.epoll_fd = epoll_create1(0);
    if (master_ctx.epoll_fd < 0) return -1;

    /* 创建 TCP 监听 */
    master_ctx.tcp_fd = create_tcp_listener(cfg->listen_addr, cfg->tcp_port);
    if (master_ctx.tcp_fd < 0) {
        close(master_ctx.epoll_fd);
        return -1;
    }

    /* 创建 UDP 监听 */
    master_ctx.udp_fd = create_udp_listener(cfg->listen_addr, cfg->udp_port);
    if (master_ctx.udp_fd < 0) {
        close(master_ctx.tcp_fd);
        close(master_ctx.epoll_fd);
        return -1;
    }

    /* 分配客户端连接数组 */
    master_ctx.clients = calloc((size_t)cfg->max_connections, sizeof(tcp_client_t));
    if (master_ctx.clients == NULL) {
        close(master_ctx.udp_fd);
        close(master_ctx.tcp_fd);
        close(master_ctx.epoll_fd);
        return -1;
    }
    for (i = 0; i < cfg->max_connections; i++) {
        master_ctx.clients[i].fd = -1;
    }

    /* 注册 TCP 监听 fd 到 epoll */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = master_ctx.tcp_fd;
    epoll_ctl(master_ctx.epoll_fd, EPOLL_CTL_ADD, master_ctx.tcp_fd, &ev);

    /* 注册 UDP fd 到 epoll */
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = master_ctx.udp_fd;
    epoll_ctl(master_ctx.epoll_fd, EPOLL_CTL_ADD, master_ctx.udp_fd, &ev);

    /* fork Worker 进程池 */
    master_ctx.worker_count = cfg->worker_count;
    master_ctx.worker_pids = calloc((size_t)cfg->worker_count, sizeof(pid_t));
    if (master_ctx.worker_pids == NULL) {
        free(master_ctx.clients);
        close(master_ctx.udp_fd);
        close(master_ctx.tcp_fd);
        close(master_ctx.epoll_fd);
        return -1;
    }

    for (i = 0; i < cfg->worker_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            /* Worker 子进程 */
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGHUP, SIG_DFL);
            free(master_ctx.worker_pids);
            free(master_ctx.clients);
            close(master_ctx.tcp_fd);
            close(master_ctx.udp_fd);
            close(master_ctx.epoll_fd);
            worker_run(cfg);
            _exit(0);
        } else if (pid > 0) {
            master_ctx.worker_pids[i] = pid;
        }
    }

    /* 事件主循环 */
    while (!g_shutdown) {
        int nfds = epoll_wait(master_ctx.epoll_fd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) {
                nfds = 0;
            } else {
                break;
            }
        }

        for (i = 0; i < nfds; i++) {
            if (events[i].data.fd == master_ctx.tcp_fd) {
                /* 循环 accept 直到 EAGAIN (边缘触发) */
                while (1) {
                    int fd_before = -1;
                    handle_accept(&master_ctx);
                    /* 简单检测: accept 返回 EAGAIN */
                    if (fd_before == -1) break; /* TODO: proper EAGAIN loop */
                }
            } else if (events[i].data.fd == master_ctx.udp_fd) {
                handle_udp_data(&master_ctx);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                tcp_client_t *client = (tcp_client_t *)events[i].data.ptr;
                epoll_ctl(master_ctx.epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                close(client->fd);
                client->fd = -1;
                client->buf_len = 0;
            } else if (events[i].events & EPOLLIN) {
                handle_tcp_data(&master_ctx, (tcp_client_t *)events[i].data.ptr);
            }
        }

        /* 回收 Worker */
        if (g_sigchld) {
            g_sigchld = 0;
            reap_workers(&master_ctx);
        }

        /* 重载配置 */
        if (g_sighup) {
            g_sighup = 0;
        }
    }

    /* 优雅关闭 */
    syslog(LOG_INFO, "Shutting down...");

    /* 关闭监听 socket */
    close(master_ctx.tcp_fd);
    close(master_ctx.udp_fd);

    /* 发送哨兵给所有 Worker */
    shm_send_sentinels(master_ctx.shm_header, master_ctx.worker_count);

    /* 等待 Worker 退出 (10 秒超时) */
    {
        int remaining = master_ctx.worker_count;
        int timeout = 10;
        while (remaining > 0 && timeout > 0) {
            pid_t pid = waitpid(-1, NULL, WNOHANG);
            if (pid > 0) {
                remaining--;
            } else {
                sleep(1);
                timeout--;
            }
        }
        /* 超时强杀 */
        if (remaining > 0) {
            for (i = 0; i < master_ctx.worker_count; i++) {
                if (master_ctx.worker_pids[i] > 0) {
                    kill(master_ctx.worker_pids[i], SIGKILL);
                }
            }
            while (waitpid(-1, NULL, 0) > 0);
        }
    }

    /* 清理 */
    free(master_ctx.worker_pids);
    free(master_ctx.clients);
    close(master_ctx.epoll_fd);
    syslog(LOG_INFO, "Master process exited");

    return 0;
}
```

Wait — master.c 中 accept 循环检查 EAGAIN 的逻辑有问题。让我修正:

- [ ] **Step 2 (修正版): 创建 master.c**

```c
/* src/master.c */
#include "master.h"
#include "shm_buffer.h"
#include "signal_handler.h"
#include "worker.h"

/* TCP 客户端连接状态 */
typedef struct {
    int  fd;
    char recv_buf[TCP_RECV_BUF_SIZE];
    int  buf_len;
} tcp_client_t;

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int create_tcp_listener(const char *addr_str, int port) {
    int fd;
    struct sockaddr_in addr;
    int optval = 1;

    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr_str, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, SOMAXCONN) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int create_udp_listener(const char *addr_str, int port) {
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr_str, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static uint64_t get_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec;
}

int master_run(const config_t *cfg, shm_header_t *shm_header, void *slots) {
    int epoll_fd, tcp_fd, udp_fd;
    tcp_client_t *clients;
    pid_t *worker_pids;
    int i, nfds;
    struct epoll_event ev, events[MAX_EVENTS];

    /* 分配客户端连接数组 */
    clients = calloc((size_t)cfg->max_connections, sizeof(tcp_client_t));
    if (clients == NULL) return -1;
    for (i = 0; i < cfg->max_connections; i++) {
        clients[i].fd = -1;
    }

    /* 创建 epoll */
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        free(clients);
        return -1;
    }

    /* 创建 TCP/UDP socket */
    tcp_fd = create_tcp_listener(cfg->listen_addr, cfg->tcp_port);
    if (tcp_fd < 0) {
        syslog(LOG_ERR, "Failed to create TCP listener on %s:%d",
               cfg->listen_addr, cfg->tcp_port);
        free(clients);
        close(epoll_fd);
        return -1;
    }

    udp_fd = create_udp_listener(cfg->listen_addr, cfg->udp_port);
    if (udp_fd < 0) {
        syslog(LOG_ERR, "Failed to create UDP listener on %s:%d",
               cfg->listen_addr, cfg->udp_port);
        free(clients);
        close(tcp_fd);
        close(epoll_fd);
        return -1;
    }

    /* 注册 TCP/UDP 到 epoll */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = tcp_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &ev);

    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = udp_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &ev);

    /* fork Worker 进程池 */
    worker_pids = calloc((size_t)cfg->worker_count, sizeof(pid_t));
    if (worker_pids == NULL) {
        free(clients);
        close(udp_fd);
        close(tcp_fd);
        close(epoll_fd);
        return -1;
    }

    for (i = 0; i < cfg->worker_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            /* Worker 子进程 */
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGHUP, SIG_DFL);
            free(worker_pids);
            free(clients);
            close(tcp_fd);
            close(udp_fd);
            close(epoll_fd);
            worker_run(cfg);
            _exit(0);
        } else if (pid > 0) {
            worker_pids[i] = pid;
        }
    }

    syslog(LOG_INFO, "Master started with %d workers on TCP:%d UDP:%d",
           cfg->worker_count, cfg->tcp_port, cfg->udp_port);

    /* 事件主循环 */
    while (!g_shutdown) {
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (i = 0; i < nfds; i++) {
            if (events[i].data.fd == tcp_fd) {
                /* 边缘触发: 循环 accept 直到 EAGAIN */
                while (1) {
                    struct sockaddr_storage client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    int client_fd = accept4(tcp_fd,
                                            (struct sockaddr *)&client_addr,
                                            &addr_len, SOCK_NONBLOCK);
                    if (client_fd < 0) break; /* EAGAIN 或错误 */

                    /* 找空闲槽位 */
                    int slot = -1;
                    for (int j = 0; j < cfg->max_connections; j++) {
                        if (clients[j].fd < 0) { slot = j; break; }
                    }

                    if (slot >= 0) {
                        clients[slot].fd = client_fd;
                        clients[slot].buf_len = 0;
                        memset(&ev, 0, sizeof(ev));
                        ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                        ev.data.ptr = &clients[slot];
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                    } else {
                        close(client_fd); /* 连接数满 */
                    }
                }
            } else if (events[i].data.fd == udp_fd) {
                /* 边缘触发: 循环读取 */
                while (1) {
                    char buf[UDP_RECV_BUF_SIZE];
                    struct sockaddr_storage client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    ssize_t n = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0,
                                         (struct sockaddr *)&client_addr,
                                         &addr_len);
                    if (n <= 0) break;
                    buf[n] = '\0';
                    if (buf[n - 1] == '\n') { buf[n - 1] = '\0'; n--; }
                    shm_produce(shm_header, slots, &client_addr, 1,
                                buf, (uint32_t)n, get_timestamp());
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                tcp_client_t *client = (tcp_client_t *)events[i].data.ptr;
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                close(client->fd);
                client->fd = -1;
            } else if (events[i].events & EPOLLIN) {
                tcp_client_t *client = (tcp_client_t *)events[i].data.ptr;
                struct sockaddr_storage addr;
                socklen_t addr_len = sizeof(addr);

                if (getpeername(client->fd, (struct sockaddr *)&addr,
                                &addr_len) < 0) continue;

                /* 边缘触发: 循环读取 */
                while (1) {
                    char *buf = client->recv_buf + client->buf_len;
                    size_t remain = (size_t)(TCP_RECV_BUF_SIZE - client->buf_len - 1);
                    ssize_t n = read(client->fd, buf, remain);
                    if (n > 0) {
                        client->buf_len += (int)n;
                        client->recv_buf[client->buf_len] = '\0';

                        /* 提取完整行 */
                        char *data = client->recv_buf;
                        char *nl;
                        while ((nl = strchr(data, '\n')) != NULL) {
                            *nl = '\0';
                            shm_produce(shm_header, slots, &addr, 0,
                                        data, (uint32_t)(nl - data),
                                        get_timestamp());
                            data = nl + 1;
                        }
                        /* 移动剩余数据 */
                        if (data > client->recv_buf) {
                            int remaining = client->buf_len -
                                            (int)(data - client->recv_buf);
                            memmove(client->recv_buf, data,
                                    (size_t)(remaining + 1));
                            client->buf_len = remaining;
                        }
                    } else if (n == 0) {
                        /* 连接关闭 */
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                        close(client->fd);
                        client->fd = -1;
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        /* 错误 */
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
                        close(client->fd);
                        client->fd = -1;
                        break;
                    }
                }
            }
        }

        /* 回收 Worker */
        if (g_sigchld) {
            g_sigchld = 0;
            while (1) {
                pid_t pid = waitpid(-1, NULL, WNOHANG);
                if (pid <= 0) break;
                for (int j = 0; j < cfg->worker_count; j++) {
                    if (worker_pids[j] == pid) {
                        pid_t new_pid = fork();
                        if (new_pid == 0) {
                            free(worker_pids);
                            free(clients);
                            close(tcp_fd);
                            close(udp_fd);
                            close(epoll_fd);
                            worker_run(cfg);
                            _exit(0);
                        } else if (new_pid > 0) {
                            worker_pids[j] = new_pid;
                        }
                        break;
                    }
                }
            }
        }

        /* 重载配置 (SIGHUP) */
        if (g_sighup) {
            g_sighup = 0;
            syslog(LOG_INFO, "SIGHUP received, config reload not yet implemented");
        }
    }

    /* === 优雅关闭 === */
    syslog(LOG_INFO, "Shutting down gracefully...");

    /* 关闭监听 */
    close(tcp_fd);
    close(udp_fd);

    /* 发送哨兵 */
    shm_send_sentinels(shm_header, cfg->worker_count);

    /* 等待 Worker 退出 (10s 超时) */
    {
        int remaining = cfg->worker_count;
        int timeout_sec = 10;
        while (remaining > 0 && timeout_sec > 0) {
            pid_t pid = waitpid(-1, NULL, WNOHANG);
            if (pid > 0) {
                remaining--;
            } else {
                sleep(1);
                timeout_sec--;
            }
        }
        /* 强杀剩余 */
        for (int j = 0; j < cfg->worker_count; j++) {
            if (worker_pids[j] > 0) {
                kill(worker_pids[j], SIGKILL);
            }
        }
        while (waitpid(-1, NULL, 0) > 0);
    }

    /* 清理客户端连接 */
    for (i = 0; i < cfg->max_connections; i++) {
        if (clients[i].fd >= 0) close(clients[i].fd);
    }

    free(worker_pids);
    free(clients);
    close(epoll_fd);
    syslog(LOG_INFO, "Master process exited");
    return 0;
}
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: master process with epoll TCP/UDP event loop and worker pool management"
```

---

### Task 9: Worker 进程

**Files:**
- Create: `src/worker.c`
- Create: `src/worker.h`

- [ ] **Step 1: 创建 worker.h**

```c
/* src/worker.h */
#ifndef WORKER_H
#define WORKER_H

#include "common.h"

/* Worker 主循环。由 Master fork 后调用，不返回。 */
void worker_run(const config_t *cfg);

#endif /* WORKER_H */
```

- [ ] **Step 2: 创建 worker.c**

```c
/* src/worker.c */
#include "worker.h"
#include "shm_buffer.h"
#include "log_parser.h"
#include "file_writer.h"
#include "signal_handler.h"

void worker_run(const config_t *cfg) {
    shm_header_t *shm_header;
    void *slots;
    uint64_t slot_size, slot_count;
    file_writer_t fw;
    struct sockaddr_storage addr;
    uint8_t protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char raw_data[65536];
    char formatted[65536];
    int rc;

    /* 连接共享内存 */
    if (shm_connect(&shm_header, &slots, &slot_size, &slot_count) < 0) {
        syslog(LOG_ERR, "Worker %d: failed to connect shared memory", getpid());
        _exit(1);
    }

    /* 初始化文件写入 */
    file_writer_init(&fw, cfg->log_dir);

    /* 主循环 */
    while (1) {
        data_len = 0;
        memset(raw_data, 0, sizeof(raw_data));

        rc = shm_consume(shm_header, slots, slot_size, &addr,
                         &protocol, raw_data, &data_len, &timestamp);

        if (rc < 0) {
            /* 错误 */
            syslog(LOG_WARNING, "Worker %d: shm_consume error", getpid());
            continue;
        }

        if (data_len == 0) {
            /* 哨兵: 退出 */
            break;
        }

        /* 解析并格式化 */
        rc = log_parser_format(&addr, timestamp, raw_data, data_len,
                               formatted, sizeof(formatted));
        if (rc < 0) {
            syslog(LOG_WARNING, "Worker %d: log_parser_format error", getpid());
            continue;
        }

        /* 提取 IP 字符串 */
        char ip_str[INET6_ADDRSTRLEN];
        if (addr.ss_family == AF_INET) {
            inet_ntop(AF_INET,
                      &((struct sockaddr_in *)&addr)->sin_addr,
                      ip_str, sizeof(ip_str));
        } else if (addr.ss_family == AF_INET6) {
            inet_ntop(AF_INET6,
                      &((struct sockaddr_in6 *)&addr)->sin6_addr,
                      ip_str, sizeof(ip_str));
        } else {
            strncpy(ip_str, "unknown", sizeof(ip_str) - 1);
        }

        /* 写入文件 */
        if (file_writer_write(&fw, ip_str, formatted, rc) < 0) {
            syslog(LOG_WARNING, "Worker %d: file write error (disk full?)",
                   getpid());
        }
    }

    /* 清理 */
    file_writer_close(&fw);
    shm_disconnect(shm_header, slots);
}
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: worker process with shared memory consumption, parsing, and file writing"
```

---

### Task 10: 集成 main.c 及系统测试

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: 更新 main.c 集成所有模块**

```c
/* src/main.c */
#include "common.h"
#include "config.h"
#include "daemon.h"
#include "shm_buffer.h"
#include "signal_handler.h"
#include "master.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-f] [-c <config>]\n", prog);
    fprintf(stderr, "  -f          Run in foreground (do not daemonize)\n");
    fprintf(stderr, "  -c <path>   Config file path (default: %s)\n",
            DEFAULT_CONF_PATH);
    fprintf(stderr, "  -h          Show this help\n");
}

int main(int argc, char *argv[]) {
    config_t cfg;
    shm_header_t *shm_header;
    void *slots;
    int foreground = 0;
    const char *conf_path = DEFAULT_CONF_PATH;
    int opt;

    /* 解析命令行参数 */
    while ((opt = getopt(argc, argv, "fc:h")) != -1) {
        switch (opt) {
        case 'f':
            foreground = 1;
            break;
        case 'c':
            conf_path = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* 加载配置 */
    config_load(&cfg, conf_path);
    syslog(LOG_INFO, "Configuration loaded: tcp=%d udp=%d workers=%d log_dir=%s",
           cfg.tcp_port, cfg.udp_port, cfg.worker_count, cfg.log_dir);

    /* 守护进程化 */
    if (!foreground) {
        if (daemonize(DEFAULT_PID_FILE) < 0) {
            fprintf(stderr, "Failed to daemonize\n");
            return 1;
        }
    }

    /* 初始化信号处理 */
    signal_handlers_init();

    /* 初始化共享内存 */
    if (shm_init(&shm_header, &slots, cfg.slot_size, cfg.slot_count) < 0) {
        syslog(LOG_ERR, "Failed to initialize shared memory");
        if (!foreground) daemon_cleanup(DEFAULT_PID_FILE);
        return 1;
    }
    syslog(LOG_INFO, "Shared memory initialized: %lu slots x %lu bytes",
           (unsigned long)cfg.slot_count, (unsigned long)cfg.slot_size);

    /* 启动 Master (包含 Worker 进程池) */
    int rc = master_run(&cfg, shm_header, slots);

    /* 清理 */
    shm_destroy(shm_header, slots, cfg.slot_count);
    if (!foreground) daemon_cleanup(DEFAULT_PID_FILE);

    syslog(LOG_INFO, "Log collector exited with code %d", rc);
    return rc;
}
```

- [ ] **Step 2: 构建完整项目**

```bash
cd /home/gem/project/log_collector/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

期望: 编译成功，无警告

- [ ] **Step 3: 运行所有单元测试**

```bash
cd /home/gem/project/log_collector/build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
ctest --output-on-failure
```

期望: 所有测试通过

- [ ] **Step 4: 手动集成测试 — 前台模式启动**

```bash
# 终端 1: 启动服务 (前台模式)
./build/log_collector -f

# 终端 2: 用 logger 发送测试日志
logger -n 127.0.0.1 -P 5140 -T "test tcp message from $$"
logger -n 127.0.0.1 -P 5140 -d "test udp message from $$"

# 检查日志输出
cat /var/log/collector/127.0.0.1/$(date +%Y-%m-%d).log
```

期望: 日志文件中包含测试消息

- [ ] **Step 5: 使用 nc 测试 TCP**

```bash
echo "<13>Jun 18 14:32:05 testhost testapp[42]: hello world" | nc -N 127.0.0.1 5140
cat /var/log/collector/127.0.0.1/$(date +%Y-%m-%d).log
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: integrate main entry point with CLI args and full lifecycle"
```

---

### Task 11: 最终集成与清理

**Files:**
- Modify: `CMakeLists.txt`
- Create: `README.md`

- [ ] **Step 1: 确保 CMakeLists.txt 完整**

最终 `CMakeLists.txt` 应为:

```cmake
cmake_minimum_required(VERSION 3.10)
project(log_collector C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")

find_package(Threads REQUIRED)

set(COMMON_SOURCES
    src/config.c
    src/shm_buffer.c
    src/log_parser.c
    src/file_writer.c
    src/signal_handler.c
    src/daemon.c
    src/master.c
    src/worker.c
)

add_executable(log_collector
    src/main.c
    ${COMMON_SOURCES}
)

target_include_directories(log_collector PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(log_collector PRIVATE
    Threads::Threads
    rt
)

install(TARGETS log_collector RUNTIME DESTINATION sbin)
install(FILES conf/log-collector.conf.example DESTINATION /etc)

# Tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: 创建 README**

```markdown
# Log Collector

Linux 网络日志收集系统，接收远程 TCP/UDP syslog 日志并按客户端 IP 和日期分文件存储。

## 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 安装

```bash
sudo make install
```

## 配置

编辑 `/etc/log-collector.conf`:

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

## 运行

```bash
# 后台运行
sudo log_collector

# 前台运行 (调试)
log_collector -f

# 指定配置文件
log_collector -c /path/to/config.conf
```

## 测试

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
ctest --output-on-failure
```
```

- [ ] **Step 3: 最终构建和测试**

```bash
cd /home/gem/project/log_collector
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j$(nproc)
ctest --output-on-failure
```

期望: 所有测试通过，Release 构建成功

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "docs: add README and finalize project structure"
```

---

## 依赖关系

```
Task 1 (骨架) ──┬──→ Task 2 (配置)
                │
                ├──→ Task 3 (共享内存)
                │
                ├──→ Task 4 (日志解析)
                │
                ├──→ Task 5 (文件写入)
                │
                ├──→ Task 6 (守护进程)
                │
                ├──→ Task 7 (信号处理)
                │
                └──→ Task 8 (Master) ──→ Task 9 (Worker) ──→ Task 10 (集成) ──→ Task 11 (清理)
```

Task 1-7 可并行实现 (不相互依赖)。Task 9 依赖 Task 3+4+5 (Worker 需要 shm_buffer, log_parser, file_writer)。Task 8 依赖 Task 3+7+9 (Master 需要 shm_buffer, signal_handler, worker.h)。Task 10 依赖所有前置任务 (Task 1-9)。
