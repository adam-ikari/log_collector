# 第 5 篇：进程池与 Worker — fork + 日志解析 + 写文件

## 回忆已学知识

- **进程基础**：`fork`、`_exit`、`waitpid`、`WNOHANG`
- **文件 I/O**：`open`、`write`、`close`、`mkdir`、目录结构
- **进程池**：Worker 管理、崩溃重启、优雅关闭模式
- **信号处理**：SIGCHLD 收割子进程

## 这次解决什么问题

共享内存缓冲区已经就绪，Master 在往里面写数据。现在需要创建 Worker 进程从缓冲区取数据、解析 syslog 格式、按客户端 IP 和日期写入磁盘文件。同时 Master 要管理这些 Worker：启动、崩溃重启、优雅关闭。

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
    pid_t *pids = calloc(cfg->worker_count, sizeof(pid_t));
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
        }
    }
    return pids;
}
```

**为什么子进程必须重置信号处理？** fork 把父进程的信号处理函数原样复制给了子进程。但父进程的 handler 是为 Master 设计的——设置 `g_shutdown` 等标志位并依赖主循环轮询。Worker 不跑这个主循环，它阻塞在 `sem_wait` 上。如果 SIGTERM 触发了 Worker 中的 `handle_sigterm`，只会设置一个 Worker 永远不检查的全局变量。重置为 `SIG_DFL` 后，SIGTERM 就会走默认行为（终止进程），Worker 干干净净退出。

**`_exit(0)` vs `exit(0)`**：`_exit` 不刷 stdio 缓冲区、不调 atexit 函数。子进程可能继承了父进程的缓冲区数据，用 `exit` 会导致数据被写两次。

## Worker 主流程

```c
void worker_run(const config_t *cfg) {
    shm_header_t *header;
    void *slots;
    uint64_t slot_size, slot_count;
    file_writer_t fw;

    /* 1. 连接共享内存 */
    shm_connect(&header, &slots, &slot_size, &slot_count);

    /* 2. 初始化文件写入器 */
    file_writer_init(&fw, cfg->log_dir);

    /* 3. 主消费循环 */
    for (;;) {
        struct sockaddr_storage addr;
        uint8_t protocol;
        uint32_t data_len;
        uint64_t timestamp;
        char buf[8192];

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
    _exit(0);
}
```

数据流一目了然：

```
共享内存 → shm_consume → log_parser_format → file_writer_write → 磁盘文件
```

## 日志解析：syslog PRI

syslog 消息以 `<PRI>` 开头，PRI = facility × 8 + severity。我们只关心 severity（低 3 位）：

```c
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

## 文件存储

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
        ensure_directory(dir_path);

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

`ensure_directory` 递归创建目录——因为 `mkdir` 不会自动创建父目录：

**为什么用循环 `write` 而非单次调用？** `write()` 可能只写入部分数据（称为"短写入"，short write）。虽然对普通文件很少发生，但在磁盘满、配额超限、或信号中断（`EINTR`）时可能出现。循环写入 + `EINTR` 重试是健壮的做法。

```c
static int ensure_directory(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;

    /* 递归创建父目录 */
    char parent[320];
    snprintf(parent, sizeof(parent), "%s", dir);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
        if (ensure_directory(parent) < 0) return -1;
    }
    return mkdir(dir, 0755);
}
```

递归过程示例——创建 `/var/log/collector/192.168.1.100`：

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

Master 通过 SIGCHLD 检测 Worker 退出，用 `waitpid WNOHANG` 非阻塞收割并立即 fork 新 Worker：

```c
static void reap_workers(pid_t *pids, int count, const config_t *cfg) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < count; i++) {
            if (pids[i] == pid) {
                pid_t new_pid = fork();
                if (new_pid == 0) {
                    reset_signals();
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

`waitpid(-1, &status, WNOHANG)`：等任意子进程，非阻塞。有子进程退出时收割它并立即 fork 新进程替换。`WNOHANG` 确保不阻塞 epoll 事件循环。

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
  └── 5. 回到 main：shm_destroy + daemon_cleanup
```

**为什么需要 10 秒超时？** Worker 可能在写入日志文件时阻塞（比如磁盘 I/O 挂起）。10 秒超时保证 Master 最终能退出，不会无限等待。超时后 `SIGKILL` 强制终止（`SIGKILL` 不可捕获，内核直接终止进程）。

## 怎么验证

```bash
$ ./log_collector -f &
$ sleep 1

# 检查进程树：Master + 4 Worker
$ ps --forest -o pid,ppid,cmd $(pgrep log_collector | tr '\n' ' ')
    PID    PPID CMD
2618473 2618467 ./log_collector -f
2618475 2618473  \_ ./log_collector -f
2618476 2618473  \_ ./log_collector -f
2618477 2618473  \_ ./log_collector -f
2618478 2618473  \_ ./log_collector -f
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

**fork 后要重置信号**：子进程继承了父进程的 handler，但它不跑主循环，不会检查 `g_shutdown`。重置为 `SIG_DFL` 让 Worker 收到信号时走默认行为。

**进程池隔离崩溃**：一个 Worker 死了不影响其他。`reap_workers` 通过 SIGCHLD + waitpid(WNOHANG) 检测退出并自动补上。

**syslog PRI 取低 3 位**：`pri & 0x07` 就是 severity。没 PRI 前缀的消息默认为 debug 级别。

**文件按 IP+日期组织**：自然边界，方便 logrotate 按目录管理保留策略。`ensure_directory` 递归创建，因为 `mkdir` 不会自动创建父目录。

**优雅关闭是分层的**：先关网络（不再进新数据），再发哨兵（让 Worker 处理完剩下的），最后清资源。

下一篇把所有模块串起来，验证整个系统真的在工作。
