# 第 5 篇：进程池与 Worker — fork + 日志解析 + 写文件

## 回忆已学知识

- **进程基础**：`fork`、`_exit`、`waitpid`、`WNOHANG`
- **文件 I/O**：`open`、`write`、`close`、`mkdir`、目录结构
- **进程池**：Worker 管理、崩溃重启、优雅关闭模式
- **信号处理**：SIGCHLD 收割子进程

## 这次解决什么问题

共享内存缓冲区已经就绪，Master 在往里面写数据。现在需要创建 Worker 进程从缓冲区取数据、解析 syslog 格式、按客户端 IP 和日期写入磁盘文件。同时 Master 要管理这些 Worker：启动、崩溃重启、优雅关闭。

## 本篇要创建/修改的文件

| 操作 | 文件 | 说明 |
|------|------|------|
| 修改 | `src/worker.h` | 替换空壳，写入 worker_run 声明 |
| 修改 | `src/worker.c` | 替换空壳，实现 Worker 主循环（消费→解析→写文件） |
| 修改 | `src/log_parser.h` | 替换空壳，写入 log_parser_format 声明 |
| 修改 | `src/log_parser.c` | 替换空壳，实现 syslog PRI 提取和消息格式化 |
| 修改 | `src/file_writer.h` | 替换空壳，写入 file_writer_t 结构体和函数声明 |
| 修改 | `src/file_writer.c` | 替换空壳，实现按 IP+日期 写日志文件 |
| 修改 | `src/master.c` | 替换第 3 篇的占位函数，添加 Worker 进程池管理代码 |

## 本篇涉及的头文件

### worker.h

替换第 1 篇创建的空壳 `src/worker.h`，写入完整内容：

```c
/* src/worker.h */
#ifndef WORKER_H
#define WORKER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Worker 主循环。由 Master fork 后调用，不返回。 */
void worker_run(const config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* WORKER_H */
```

### log_parser.h

替换第 1 篇创建的空壳 `src/log_parser.h`，写入完整内容：

```c
/* src/log_parser.h */
#ifndef LOG_PARSER_H
#define LOG_PARSER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 解析 syslog 消息，返回格式化后的行 (包含换行符)。
   输出缓冲区由调用者提供，out_size 指定大小。
   返回实际写入的字节数(不含 NUL)，-1 表示错误。 */
int log_parser_format(const struct sockaddr_storage *addr,
                      uint64_t recv_timestamp,
                      const char *raw_msg, uint32_t raw_len,
                      char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* LOG_PARSER_H */
```

### file_writer.h

替换第 1 篇创建的空壳 `src/file_writer.h`，写入完整内容：

```c
/* src/file_writer.h */
#ifndef FILE_WRITER_H
#define FILE_WRITER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* FILE_WRITER_H */
```

## 进程池 vs 线程池

对于日志收集系统，为什么选进程池？

| 维度   | 进程池                               | 线程池                   |
| ------ | ------------------------------------ | ------------------------ |
| 隔离性 | 一个 Worker 崩溃不影响其他           | 一个线程崩溃整个进程退出 |
| 安全性 | 信号量/锁在进程退出时自动释放        | 线程异常退出可能留下死锁 |
| 调试   | 可以用 strace/gdb 单独 attach        | 多线程调试困难           |
| 重启   | 子进程退出→父进程 fork 新进程        | 线程无法独立重启         |
| 开销   | 创建开销大（fork+COW），但只创建一次 | 创建开销小               |

**选择进程池的核心原因**：日志写入涉及文件 I/O，如果某个日志目录的磁盘出现问题（如空间满），Worker 可能阻塞或崩溃。进程隔离保证一个 Worker 的问题不影响其他 Worker。

fork 的开销在 Linux 上很低——写时复制（COW）让父子进程共享物理页，只有真正写入时才复制。fork + 不 exec 的情况下，大部分内存都不会被复制。

## Master fork Worker 进程池

首先，在 `src/master.c` 文件头部添加 `#include "worker.h"`（因为 `fork_workers` 和 `reap_workers` 现在要调用 `worker_run`）：

```c
/* 在 src/master.c 头部已有的 include 下方添加 */
#include "worker.h"
```

在 `src/master.c` 中，替换第 3 篇的占位函数 `reset_signals`、`fork_workers`（之前是空壳），写入真实现：

```c
/* 子进程中重置信号处理为默认值（fork 会继承父进程的 handler） */
static void reset_signals(void) {
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT,  SIG_DFL);
    signal(SIGHUP,  SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
}

static pid_t *fork_workers(const config_t *cfg) {
    pid_t *pids = calloc((size_t)cfg->worker_count, sizeof(pid_t));
    if (!pids) return NULL;

    for (int i = 0; i < cfg->worker_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            reset_signals();
            free(pids);
            worker_run(cfg);      /* 进入 Worker 主循环，不返回 */
            _exit(0);
        } else if (pid > 0) {
            pids[i] = pid;        /* 父进程：记录子进程 PID */
            sd_journal_print(LOG_INFO, "Worker %d 已启动 (PID=%d)", i, pid);
        } else {
            sd_journal_print(LOG_ERR, "fork worker %d 失败: %s",
                             i, strerror(errno));
        }
    }
    return pids;
}
```

**为什么子进程必须重置信号处理？** fork 把父进程的信号处理函数原样复制给了子进程。但父进程的 handler 是为 Master 设计的——设置 `g_shutdown` 等标志位并依赖主循环轮询。Worker 不跑这个主循环，它阻塞在 `sem_wait` 上。如果 SIGTERM 触发了 Worker 中的 `handle_sigterm`，只会设置一个 Worker 永远不检查的全局变量。重置为 `SIG_DFL` 后，SIGTERM 就会走默认行为（终止进程），Worker 干干净净退出。

**`_exit(0)` vs `exit(0)`**：`_exit` 不刷 stdio 缓冲区、不调 atexit 函数。子进程可能继承了父进程的缓冲区数据，用 `exit` 会导致数据被写两次。

## Worker 主流程

替换第 1 篇创建的空壳 `src/worker.c`。首先写入 include：

```c
/* src/worker.c */
#include "worker.h"
#include "shm_buffer.h"
#include "log_parser.h"
#include "file_writer.h"
```

然后添加 `worker_run` 函数（在同一个文件中）：

```c
void worker_run(const config_t *cfg) {
    shm_header_t *header = NULL;
    void *slots = NULL;
    uint64_t slot_size = 0, slot_count = 0;
    file_writer_t fw;

    /* 1. 连接共享内存 */
    if (shm_connect(&header, &slots, &slot_size, &slot_count) != 0)
        _exit(1);

    /* 2. 分配接收缓冲区（大小 = slot_size，足够容纳最大数据） */
    char *buf = malloc((size_t)slot_size);
    if (!buf) { shm_disconnect(header, slots); _exit(1); }

    /* 3. 初始化文件写入器 */
    file_writer_init(&fw, cfg->log_dir);

    /* 4. 主消费循环 */
    for (;;) {
        struct sockaddr_storage addr;
        uint8_t protocol;
        uint32_t data_len;
        uint64_t timestamp;

        int ret = shm_consume(header, slots, slot_size,
                              &addr, &protocol, buf, &data_len, &timestamp);

        if (data_len == 0) break;   /* 哨兵：退出 */
        if (ret <= 0)    continue;  /* 无数据 */

        /* 4. 解析日志格式 */
        char formatted[8192];
        int written = log_parser_format(&addr, timestamp,
                                        buf, data_len, formatted, sizeof(formatted));
        if (written <= 0) continue;

        /* 5. 提取 IP 字符串 */
        char ip_str[INET6_ADDRSTRLEN];
        if (addr.ss_family == AF_INET)
            inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr,
                      ip_str, sizeof(ip_str));
        else if (addr.ss_family == AF_INET6)
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr,
                      ip_str, sizeof(ip_str));
        else
            snprintf(ip_str, sizeof(ip_str), "unknown");

        /* 6. 写入文件 */
        file_writer_write(&fw, ip_str, formatted, written);
    }

    /* 7. 清理 */
    file_writer_close(&fw);
    shm_disconnect(header, slots);
    free(buf);
    _exit(0);
}
```

数据流一目了然：

```
共享内存 → shm_consume → log_parser_format → file_writer_write → 磁盘文件
```

## 日志解析：syslog PRI

替换第 1 篇创建的空壳 `src/log_parser.c`。首先写入 include 和辅助函数：

```c
/* src/log_parser.c */
#include "log_parser.h"

static int extract_pri(const char *msg, uint32_t len, int *severity_out) {
    if (len < 3 || msg[0] != '<') return -1;

    const char *end = memchr(msg, '>', len);  /* 找闭合的 > */
    if (end == NULL) return -1;

    long pri = strtol(msg + 1, NULL, 10);
    *severity_out = (int)(pri & 0x07);   /* 低 3 位 = severity */
    return (int)(end - msg + 1);          /* 返回 PRI 部分长度 */
}
```

`pri & 0x07` 是怎么工作的？以 `<13>` 为例：

```
PRI = 13 = 0b1101
0x07  = 0b0111
13 & 7 = 0b1101 & 0b0111 = 0b0101 = 5 → notice
```

**为什么用 `memchr` 而非 `strchr`？** 日志消息可能包含二进制数据或不存在 `\0` 终止符。`memchr` 安全地限制在 `len` 范围内，不会越界。

继续在 `src/log_parser.c` 中添加 `addr_to_str` 辅助函数——将 `sockaddr_storage` 转换为 IP 字符串，支持 IPv4 和 IPv6：

```c
/*
 * addr_to_str — 将 sockaddr_storage 转换为 IP 字符串
 *
 * 支持 IPv4 (AF_INET) 和 IPv6 (AF_INET6)。
 * 未知地址族返回 "unknown"。
 */
static const char *addr_to_str(const struct sockaddr_storage *addr,
                               char *buf, size_t buf_size) {
    if (addr->ss_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
        inet_ntop(AF_INET, &sin->sin_addr, buf, (socklen_t)buf_size);
    } else if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)addr;
        inet_ntop(AF_INET6, &sin6->sin6_addr, buf, (socklen_t)buf_size);
    } else {
        snprintf(buf, buf_size, "unknown");
    }
    return buf;
}

severity 映射表：

| 值  | 标签    | 含义             |
| --- | ------- | ---------------- |
| 0   | emerg   | 系统不可用       |
| 1   | alert   | 必须立即采取行动 |
| 2   | crit    | 临界条件         |
| 3   | err     | 错误条件         |
| 4   | warning | 警告条件         |
| 5   | notice  | 正常但重要的事件 |
| 6   | info    | 信息性消息       |
| 7   | debug   | 调试级别消息     |

输出格式：`2026-06-21T14:32:05+08:00 192.168.1.100 [notice] connection timeout`

**为什么默认 severity=debug(7)？** 不是所有日志都有 `<PRI>` 前缀。普通文本消息按 debug 级别处理，保证所有消息都能被记录。

**为什么用 `localtime_r` 而非 `localtime`？** `localtime` 返回指向静态缓冲区的指针，多进程环境下不安全。`localtime_r` 是线程/进程安全版本，由调用者提供缓冲区。

### 完整格式化函数

`extract_pri` 只是解析 PRI。完整的 `log_parser_format` 还做了时间戳格式化、IP 地址转换、severity 映射、消息体拼接。继续在 `src/log_parser.c` 中添加：

```c
static const char *severity_names[] = {
    "emerg", "alert", "crit", "err",
    "warning", "notice", "info", "debug"
};

int log_parser_format(const struct sockaddr_storage *addr,
                      uint64_t recv_timestamp,
                      const char *raw_msg, uint32_t raw_len,
                      char *out, size_t out_size) {
    char ip_str[INET6_ADDRSTRLEN];
    char time_buf[32];
    time_t ts;
    struct tm tm_info;
    int severity = 7;  /* 默认 debug */
    int pri_len;
    const char *body;
    uint32_t body_len;
    int written;

    /* 时间戳 */
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
        /* 跳过 PRI 后面的前导空格 */
        while (body_len > 0 && *body == ' ') {
            body++;
            body_len--;
        }
    } else {
        body = raw_msg;
        body_len = raw_len;
    }

    /* 格式化: timestamp ip [level] message */
    written = snprintf(out, out_size, "%s %s [%s] ",
                       time_buf, ip_str, severity_names[severity]);
    if (written < 0 || (size_t)written >= out_size) return -1;

    /* 追加消息体（防溢出） */
    size_t remaining = out_size - (size_t)written;
    size_t copy_len = (size_t)body_len < remaining - 1 ? (size_t)body_len : remaining - 1;
    memcpy(out + written, body, copy_len);
    written += (int)copy_len;

    /* 确保以换行结束 */
    if (written > 0 && out[written - 1] != '\n') {
        if ((size_t)written < out_size - 1) out[written++] = '\n';
    }
    out[written] = '\0';
    return written;
}
```

## 文件存储

替换第 1 篇创建的空壳 `src/file_writer.c`。首先写入 include 和辅助函数：

```c
/* src/file_writer.c */
#include "file_writer.h"
#include <sys/stat.h>

/* 递归创建目录 */
static int ensure_directory(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;

    char parent[320];
    snprintf(parent, sizeof(parent), "%s", dir);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
        if (ensure_directory(parent) < 0) return -1;
    }
    return mkdir(dir, 0755);
}

/* 获取当前日期字符串 YYYY-MM-DD */
static void get_current_date(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(buf, size, "%Y-%m-%d", &tm_info);
}
```

然后添加 `file_writer_init`、`file_writer_close`：

```c
void file_writer_init(file_writer_t *fw, const char *log_dir) {
    snprintf(fw->log_dir, sizeof(fw->log_dir), "%s", log_dir);
    fw->fd = -1;
    fw->current_ip[0] = '\0';
    fw->current_date[0] = '\0';
    ensure_directory(fw->log_dir);
}

void file_writer_close(file_writer_t *fw) {
    if (fw->fd >= 0) { close(fw->fd); fw->fd = -1; }
}
```

按 IP 分目录 + 按日期分文件：

```
/tmp/log_collector_test/
├── 192.168.1.100/
│   ├── 2026-06-21.log
│   └── 2026-06-20.log
└── 10.0.0.55/
    └── 2026-06-21.log
```

**为什么按 IP 分目录？** 方便运维人员查找特定来源的日志，也方便配合 logrotate 按目录配置不同的保留策略。

**为什么按日期分文件？** 自然的时间边界，文件名 `YYYY-MM-DD.log` 天然按字典序排列，方便归档和清理。

继续在 `src/file_writer.c` 中添加 `file_writer_write`：

```c
int file_writer_write(file_writer_t *fw, const char *ip,
                      const char *data, int data_len) {
    char today[16];
    get_current_date(today, sizeof(today));

    /* IP 或日期变了 → 关闭旧文件，打开新文件 */
    if (fw->fd < 0 || strcmp(fw->current_ip, ip) ||
        strcmp(fw->current_date, today)) {

        if (fw->fd >= 0) close(fw->fd);

        /* 创建目录 <log_dir>/<ip> */
        char dir_path[320];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", fw->log_dir, ip);
        if (ensure_directory(dir_path) < 0) return -1;

        /* 打开/创建日志文件 */
        char file_path[384];
        snprintf(file_path, sizeof(file_path), "%s/%s.log", dir_path, today);
        fw->fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fw->fd < 0) return -1;

        snprintf(fw->current_ip, sizeof(fw->current_ip), "%s", ip);
        snprintf(fw->current_date, sizeof(fw->current_date), "%s", today);
    }

    const char *ptr = data;
    size_t remaining = (size_t)data_len;
    while (remaining > 0) {
        ssize_t written = write(fw->fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;  /* 被信号中断，重试 */
            return -1;
        }
        ptr += written;
        remaining -= (size_t)written;
    }
    return 0;
}
```

**为什么用循环 `write` 而非单次调用？** `write()` 可能只写入部分数据（称为"短写入"，short write）。虽然对普通文件很少发生，但在磁盘满、配额超限、或信号中断（`EINTR`）时可能出现。循环写入 + `EINTR` 重试是健壮的做法。

递归过程示例——`ensure_directory` 创建 `/var/log/collector/192.168.1.100`：

```
ensure_directory("/var/log/collector/192.168.1.100")
  → stat() 返回 ENOENT
  → parent = "/var/log/collector"
  → ensure_directory("/var/log/collector")
    → stat() 返回 ENOENT
    → parent = "/var/log"
    → ensure_directory("/var/log") → 已存在，是目录 → return 0
    → mkdir("/var/log/collector") → return 0
  → mkdir("/var/log/collector/192.168.1.100") → return 0
```

## Worker 崩溃自动重启

Master 通过 SIGCHLD 检测 Worker 退出，用 `waitpid WNOHANG` 非阻塞收割并立即 fork 新 Worker。

在 `src/master.c` 中，替换第 3 篇的占位函数 `reap_workers`（之前是空壳）：

```c
static void reap_workers(pid_t *pids, int count, const config_t *cfg) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < count; i++) {
            if (pids[i] == pid) {
                sd_journal_print(LOG_WARNING, "Worker %d 异常退出 (PID=%d), 正在重启...", i, pid);
                pid_t new_pid = fork();
                if (new_pid == 0) {
                    reset_signals();
                    worker_run(cfg);
                    _exit(0);
                } else if (new_pid > 0) {
                    pids[i] = new_pid;
                    sd_journal_print(LOG_INFO, "Worker %d 已重启 (新PID=%d)", i, new_pid);
                } else {
                    sd_journal_print(LOG_ERR, "重启 Worker %d 失败: %s",
                                     i, strerror(errno));
                }
                break;
            }
        }
    }
}
```

`waitpid(-1, &status, WNOHANG)`：等任意子进程，非阻塞。有子进程退出时收割它并立即 fork 新进程替换。`WNOHANG` 确保不阻塞 epoll 事件循环。

在 `src/master.c` 中，替换第 3 篇的占位函数 `wait_workers`（之前是空壳）：

```c
/* 等待所有 worker 退出（带 10 秒超时） */
static void wait_workers(pid_t *pids, int count) {
    for (int waited = 0; waited < 10; waited++) {
        int alive = 0;
        for (int i = 0; i < count; i++) {
            if (pids[i] <= 0) continue;
            int status;
            if (waitpid(pids[i], &status, WNOHANG) > 0)
                pids[i] = -1;
            else
                alive++;
        }
        if (alive == 0) break;
        sleep(1);
    }
    /* 超时强制终止 */
    for (int i = 0; i < count; i++) {
        if (pids[i] > 0) {
            kill(pids[i], SIGKILL);
            waitpid(pids[i], NULL, 0);
        }
    }
}
```

## 优雅关闭

```
SIGTERM → g_shutdown = 1
  │
  ▼
Master 主循环 break
  ├── 1. 关闭 tcp_fd / udp_fd（不再接收新数据）
  ├── 2. shm_send_sentinels（向缓冲区发 N 个 data_len=0）
  ├── 3. wait_workers（等 Worker 退出，最多 10 秒）
  │      ├── 每秒轮询 waitpid(WNOHANG)
  │      └── 超时 → kill -9 强制终止
  ├── 4. 关闭所有 TCP 客户端连接
  └── 5. 回到 main：shm_destroy 清理共享内存
```

**为什么需要 10 秒超时？** Worker 可能在写入日志文件时阻塞（比如磁盘 I/O 挂起）。10 秒超时保证 Master 最终能退出，不会无限等待。超时后 `SIGKILL` 强制终止（`SIGKILL` 不可捕获，内核直接终止进程）。

## 怎么验证

```bash
$ ./log_collector &
$ sleep 1

# 检查进程树：Master + 4 Worker
$ ps --forest -o pid,ppid,cmd $(pgrep log_collector | tr '\n' ' ')
    PID    PPID CMD
2618473 2618467 ./log_collector
2618475 2618473  \_ ./log_collector
2618476 2618473  \_ ./log_collector
2618477 2618473  \_ ./log_collector
2618478 2618473  \_ ./log_collector
# Master (PID=2618473) 是父进程，4 个 Worker 是子进程

$ pgrep -c log_collector
5
# Master + 4 Worker = 5 个进程

# 发送日志并检查文件
$ echo "<13>test message from worker test" | nc -u -w1 127.0.0.1 5140
$ sleep 0.5
$ cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log
2026-06-24T08:39:31+0800 127.0.0.1 [notice] test message from worker test
# <13> → PRI=13 → severity=13&7=5 → [notice] ✓

# 测试 Worker 崩溃恢复
$ kill -9 2618478    # 杀掉一个 Worker
$ sleep 2
$ pgrep -c log_collector
5
# 仍然是 5 个进程（Master 检测到 SIGCHLD，自动 fork 新 Worker）

# 测试优雅关闭
$ kill -TERM 2618473  # 向 Master 发 SIGTERM
$ sleep 2
$ pgrep log_collector
# 无输出——所有进程已退出
```

## 你现在应该理解的

**fork 后要重置信号**：子进程继承了父进程的 handler，但不跑主循环，不会检查 `g_shutdown`。重置为 `SIG_DFL`。

**进程池隔离崩溃**：一个 Worker 死了不影响其他，Master 自动 fork 新进程补上。

**syslog PRI 取低 3 位**：`pri & 0x07` 就是 severity。没 PRI 前缀的消息默认为 debug。

**文件按 IP+日期组织**：`ensure_directory` 递归创建目录，因为 `mkdir` 不会自动创建父目录。
