# 第 3 篇：接收网络日志 — epoll + TCP/UDP

## 回忆已学知识

- **Socket**：`socket`、`bind`、`listen`、`accept`、TCP 字节流 vs UDP 数据报
- **epoll**：`epoll_create1`、`epoll_ctl`、`epoll_wait`、水平触发 vs 边缘触发
- **进程基础**：`fork`、`waitpid`（SIGCHLD 收割）
- **信号处理**：信号标志在主循环中被检查

## 这次解决什么问题

程序现在是守护进程了，但还收不到数据。需要同时监听 TCP 和 UDP 两个端口，高效处理大量并发连接。

前面学过 Socket 和 epoll，这里把它们组合起来：用 epoll 管理 TCP listener、UDP listener、以及所有已连接的 TCP 客户端。

## 本篇要创建/修改的文件

| 操作 | 文件 | 说明 |
|------|------|------|
| 修改 | `src/master.h` | 替换空壳，写入 master_run 声明 |
| 修改 | `src/master.c` | 替换空壳，实现 epoll 事件循环、TCP/UDP 监听、行缓冲 |
| 依赖 | `src/signal_handler.h` | 第 2 篇已完成，使用 g_shutdown 等标志 |

## master.h

替换第 1 篇创建的空壳 `src/master.h`，写入完整内容：

```c
/* src/master.h */
#ifndef MASTER_H
#define MASTER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Master 主循环: 初始化网络, epoll 事件循环, 进程池管理。
   直到 shutdown 才返回。返回 0 正常退出, -1 错误。 */
int master_run(const config_t *cfg, shm_header_t *shm_header, void *slots);

#ifdef __cplusplus
}
#endif

#endif /* MASTER_H */
```

## epoll 复习

epoll 是 Linux 的高性能 I/O 多路复用机制。内核用红黑树维护所有注册的 fd，用双向链表维护就绪的 fd。`epoll_wait` 直接从就绪链表取数据，不需要遍历所有 fd。

| 机制   | 时间复杂度 | 最大 fd 数 | 工作原理                            |
| ------ | ---------- | ---------- | ----------------------------------- |
| select | O(n)       | 1024       | 每次传入整个 fd_set，内核遍历       |
| poll   | O(n)       | 无限制     | 传入 pollfd 数组，内核遍历          |
| epoll  | O(1)       | 无限制     | 内核维护就绪链表，只返回有事件的 fd |

### 水平触发 vs 边缘触发

**水平触发（LT，默认）**：只要 fd 可读，每次 `epoll_wait` 都返回该事件。可以只读一部分，下次还会通知。

**边缘触发（ET，EPOLLET）**：只在 fd 状态**变化**时通知一次。必须循环读取直到返回 EAGAIN，否则剩余数据可能永远不会触发新通知。

想象一个水桶：

- 水平触发 = 水位报警器，只要水位超过警戒线就一直响
- 边缘触发 = 水位变化传感器，只在超过警戒线的瞬间响一次

**我们选边缘触发**：日志收集系统可能面对数千个 TCP 连接，减少不必要的 `epoll_wait` 唤醒次数能显著降低 CPU 使用率。代价是代码复杂一点——每个 fd 必须循环读写直到 EAGAIN。

> **本篇所有代码都在 `src/master.c` 中。** 完整的 master.c 结构如下，后续各节分别对应其中一部分：
>
> ```
> master.c 文件结构:
> ├── 辅助函数: get_timestamp, set_nonblock
> ├── 创建监听: create_tcp_listener, create_udp_listener
> ├── TCP 客户端管理: tcp_client_t 结构体, handle_tcp_close
> ├── TCP 行缓冲:   extract_line, handle_tcp_data
> ├── UDP 数据读取: handle_udp_data
> ├── Worker 管理:  reset_signals, fork_workers, reap_workers, wait_workers
> │   (第 5 篇详细讲解，本篇先放空壳占位)
> └── 主循环:      master_run (epoll 事件循环)
> ```
>
> 创建文件时这样安排：
> ```bash
> touch src/master.c    # 本篇所有代码都在这里
> touch src/master.h    # 函数声明（上面已给出）
> ```
>
> **注意**：本篇代码调用了 `shm_produce`、`shm_send_sentinels` 等函数，这些在第 4 篇才实现。编译会报链接错误是正常的——完成第 4 篇后就能正常编译。这就是"骨架先行"策略：先把所有模块的接口定义好，逐步填充实现。
>
> **占位函数**：`reap_workers`、`fork_workers`、`wait_workers` 等函数本篇会用到但第 5 篇才实现。先在文件开头放几个空壳占位：
>
> ```c
> /* 占位：第 5 篇实现（进程池管理） */
> static void reap_workers(pid_t *pids, int count, const config_t *cfg) {
>     (void)pids; (void)count; (void)cfg;
> }
> static pid_t *fork_workers(const config_t *cfg) { (void)cfg; return NULL; }
> static void wait_workers(pid_t *pids, int count) {
>     (void)pids; (void)count;
> }
> ```
>
> 这样 `master_run` 里的 `reap_workers` 调用就不会报编译错误。第 5 篇完成后再替换为真实现。

## 创建监听 Socket

替换第 1 篇创建的空壳 `src/master.c`。首先写入文件头部的 include 和辅助函数，然后添加 TCP listener：

```c
/* src/master.c */
#include "master.h"
#include "shm_buffer.h"
#include "signal_handler.h"

/* 辅助函数：获取当前时间戳（epoch 秒） */
static uint64_t get_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec;
}

/* 辅助函数：设置 fd 为非阻塞 */
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* 辅助函数：解析监听地址，支持 0.0.0.0 和 * 表示 INADDR_ANY */
static int resolve_addr(const char *str, struct sockaddr_in *addr) {
    if (strcmp(str, "0.0.0.0") == 0 || strcmp(str, "*") == 0) {
        addr->sin_addr.s_addr = INADDR_ANY;
        return 0;
    }
    return inet_pton(AF_INET, str, &addr->sin_addr) <= 0 ? -1 : 0;
}

/* ── 占位函数（第 5 篇实现） ── */
static void reap_workers(pid_t *pids, int count, const config_t *cfg) {
    (void)pids; (void)count; (void)cfg;
}
static pid_t *fork_workers(const config_t *cfg) { (void)cfg; return NULL; }
static void wait_workers(pid_t *pids, int count) {
    (void)pids; (void)count;
}

/* ── 创建 TCP/UDP 监听 socket ── */

static int create_tcp_listener(const config_t *cfg) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket TCP"); return -1; }

    /* SO_REUSEADDR：重启时不用等 60 秒 TIME_WAIT */
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* 非阻塞：epoll 边缘触发的前提 */
    if (set_nonblock(fd) < 0) { perror("fcntl TCP"); close(fd); return -1; }

    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_port = htons((uint16_t)cfg->tcp_port) };
    if (resolve_addr(cfg->listen_addr, &addr) < 0) {
        fprintf(stderr, "Invalid listen_addr: %s\n", cfg->listen_addr);
        close(fd); return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind TCP"); close(fd); return -1;
    }
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}
```

**`SO_REUSEADDR`**：没有它，你 `kill` 掉程序后马上重启会报 "Address already in use"。因为 TCP 连接关闭后有 60 秒的 TIME_WAIT 状态，端口还被占着。`SO_REUSEADDR` 允许你在 TIME_WAIT 期间重新 bind。

**`O_NONBLOCK`**：非阻塞是 epoll 的前提。如果 `accept` 阻塞了，整个事件循环就卡住了，所有连接都受影响。

**`htons(port)`**：Host TO Network Short。x86 是小端字节序，网络是大端。5140 = 0x1414，刚好对称。但别被巧合迷惑——端口 8080 在小端是 `0x901F`，大端是 `0x1F90`，不转就错了。

UDP 更简单——不需要 `listen`，非阻塞 socket + `bind` 就能收数据。继续在 `src/master.c` 中添加：

```c
static int create_udp_listener(const config_t *cfg) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);  /* SOCK_DGRAM = UDP */
    if (fd < 0) { perror("socket UDP"); return -1; }

    /* 非阻塞 */
    if (set_nonblock(fd) < 0) { perror("fcntl UDP"); close(fd); return -1; }

    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_port = htons((uint16_t)cfg->udp_port) };
    if (resolve_addr(cfg->listen_addr, &addr) < 0) {
        fprintf(stderr, "Invalid listen_addr: %s\n", cfg->listen_addr);
        close(fd); return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind UDP"); close(fd); return -1;
    }
    return fd;
}
```

## epoll 事件循环

继续在 `src/master.c` 中添加 `master_run` 函数。这是整个 Master 的核心——epoll 事件循环：

```c
int master_run(const config_t *cfg, shm_header_t *header, void *slots) {
    int tcp_fd = -1, udp_fd = -1, epoll_fd = -1;
    tcp_client_t *clients = NULL;
    pid_t *worker_pids = NULL;
    struct epoll_event ev, events[MAX_EVENTS];
    int ret = -1;

    /* 1. 创建监听 socket */
    if ((tcp_fd = create_tcp_listener(cfg)) < 0) goto cleanup;
    if ((udp_fd = create_udp_listener(cfg)) < 0) goto cleanup;

    /* 2. 创建 epoll 实例 */
    if ((epoll_fd = epoll_create1(0)) < 0) {
        perror("epoll_create1"); goto cleanup;
    }

    /* 注册 TCP/UDP listener（边缘触发） */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = tcp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &ev) < 0) {
        perror("epoll_ctl TCP"); goto cleanup;
    }
    ev.data.fd = udp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &ev) < 0) {
        perror("epoll_ctl UDP"); goto cleanup;
    }

    /* 3. 分配 TCP 客户端状态数组 */
    clients = calloc((size_t)cfg->max_connections, sizeof(tcp_client_t));
    if (!clients) { perror("calloc clients"); goto cleanup; }
    for (int i = 0; i < cfg->max_connections; i++) clients[i].fd = -1;

    /* 4. 派生 Worker 进程池 */
    if (!(worker_pids = fork_workers(cfg))) {
        perror("fork_workers"); goto cleanup;
    }

    /* 5. epoll 事件主循环 */
    while (!g_shutdown) {
        if (g_sigchld) { g_sigchld = 0; reap_workers(worker_pids, cfg->worker_count, cfg); }
        if (g_sighup)  { g_sighup  = 0; /* TODO: 配置热重载 */ }

        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait"); break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == tcp_fd) {
                /* TCP accept 循环（边缘触发） */
                while (1) {
                    struct sockaddr_storage addr;
                    socklen_t addr_len = sizeof(addr);
                    int client_fd = accept(tcp_fd,
                        (struct sockaddr *)&addr, &addr_len);
                    if (client_fd < 0) break;
                    set_nonblock(client_fd);

                    /* 找空闲槽位 */
                    int slot;
                    for (slot = 0; slot < cfg->max_connections; slot++)
                        if (clients[slot].fd < 0) break;
                    if (slot == cfg->max_connections) {
                        close(client_fd); continue;
                    }
                    clients[slot].fd = client_fd;
                    clients[slot].buf_len = 0;

                    /* 注册到 epoll */
                    memset(&ev, 0, sizeof(ev));
                    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                    ev.data.ptr = &clients[slot];
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                        close(client_fd); clients[slot].fd = -1;
                    }
                }
            } else if (events[i].data.fd == udp_fd) {
                handle_udp_data(udp_fd, header, slots);
            } else {
                tcp_client_t *client = (tcp_client_t *)events[i].data.ptr;
                if (events[i].events & EPOLLIN)
                    handle_tcp_data(client, epoll_fd, header, slots);
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                    handle_tcp_close(client, epoll_fd);
            }
        }
    }

    ret = 0;

cleanup:
    /* 6. 优雅关闭 */
    sd_journal_print(LOG_INFO, "收到 shutdown 信号，开始优雅关闭...");
    if (tcp_fd >= 0) close(tcp_fd);
    if (udp_fd >= 0) close(udp_fd);
    if (header) shm_send_sentinels(header, cfg->worker_count);
    if (worker_pids) {
        wait_workers(worker_pids, cfg->worker_count);
        sd_journal_print(LOG_INFO, "所有 Worker 已退出");
        free(worker_pids);
    }
    if (clients) {
        for (int i = 0; i < cfg->max_connections; i++)
            if (clients[i].fd >= 0) close(clients[i].fd);
        free(clients);
    }
    if (epoll_fd >= 0) close(epoll_fd);
    return ret;
}
```

**为什么 `epoll_wait` 超时 100ms？** 这是一个平衡：

- 太短（1ms）：`epoll_wait` 频繁返回 0，CPU 空转
- 太长（5s）：`g_shutdown` 被设置后最多等 5 秒才响应
- 100ms：用户感知不到延迟，CPU 几乎不空转

**`ev.data`** 是一个 union，可以存 `fd` 也可以存 `void *ptr`：

- 对于 listener socket：存 fd（通过 fd 区分 TCP/UDP listener）
- 对于客户端连接：存指针（指向 `tcp_client_t` 结构体，避免额外的 fd→状态查找）

TCP 客户端状态结构体，在 `src/master.c` 中 `master_run` 函数之前添加：

```c
/* TCP 客户端状态 */
typedef struct {
    int  fd;
    char recv_buf[TCP_RECV_BUF_SIZE];
    int  buf_len;
} tcp_client_t;
```

## TCP accept：边缘触发必须循环

```c
while (1) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int client_fd = accept(tcp_fd, (struct sockaddr *)&addr, &addr_len);
    if (client_fd < 0) break; /* EAGAIN 或错误 */

    /* 设为非阻塞 */
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    /* 找空闲槽位存客户端状态 */
    int slot;
    for (slot = 0; slot < max_connections; slot++)
        if (clients[slot].fd < 0) break;
    if (slot == max_connections) {
        close(client_fd);   /* 连接数满，拒绝 */
        continue;
    }
    clients[slot].fd = client_fd;
    clients[slot].buf_len = 0;

    /* 注册到 epoll */
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    ev.data.ptr = &clients[slot];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
        close(client_fd); clients[slot].fd = -1;
    }
}
```

**`accept` + `fcntl` 两步走**：`accept` 返回的 fd 默认是阻塞的，必须手动设为非阻塞。两步操作之间没有竞态条件——fd 还没注册到 epoll，不会有事件触发。

**客户端数组**：预分配 `max_connections` 个 `tcp_client_t`，fd 初始化为 -1。新连接来了扫描找空闲槽位。1024 个元素的线性扫描在 L1 缓存里完成，约 1 微秒。用链表反而更慢（指针追踪 + 动态分配）。

## TCP 行缓冲

TCP 是字节流，没有消息边界。一次 `read` 可能读到半条消息，也可能读到三条消息拼在一起。需要自己拆行（以 `\n` 为分隔符）。

在 `src/master.c` 中添加 `handle_tcp_close`、`extract_line` 和 `handle_tcp_data`：

```c
/* 关闭 TCP 客户端连接 */
static void handle_tcp_close(tcp_client_t *client, int epoll_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    client->fd = -1;
    client->buf_len = 0;
}

/* 从缓冲区中提取一条以 '\n' 结尾的完整行，返回行长度（含 \n），没有则返回 0 */
static int extract_line(const char *buf, int len) {
    for (int i = 0; i < len; i++)
        if (buf[i] == '\n') return i + 1;
    return 0;
}

/* 边缘触发循环读取 TCP 数据直到 EAGAIN */
static void handle_tcp_data(tcp_client_t *client, int epoll_fd,
                            shm_header_t *header, void *slots) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    if (getpeername(client->fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        handle_tcp_close(client, epoll_fd);
        return;
    }

    uint64_t ts = get_timestamp();

    while (1) {
        int remaining = TCP_RECV_BUF_SIZE - client->buf_len - 1;
        if (remaining <= 0) {
            handle_tcp_close(client, epoll_fd);  /* 缓冲区满，断开 */
            return;
        }

        ssize_t n = read(client->fd,
                         client->recv_buf + client->buf_len, remaining);
        if (n > 0) {
            client->buf_len += n;
            client->recv_buf[client->buf_len] = '\0';

            /* 提取所有完整行 */
            int line_len;
            while ((line_len = extract_line(client->recv_buf,
                                            client->buf_len)) > 0) {
                client->recv_buf[line_len - 1] = '\0';  /* 去掉 \n */
                if (line_len > 1)
                    shm_produce(header, slots, &addr, 0,
                                client->recv_buf, (uint32_t)(line_len - 1), ts);
                /* 剩余数据移到开头——用 memmove，因为源和目标可能重叠 */
                int remain = client->buf_len - line_len;
                if (remain > 0)
                    memmove(client->recv_buf, client->recv_buf + line_len, remain);
                client->buf_len = remain;
            }
        } else if (n == 0) {
            handle_tcp_close(client, epoll_fd);  /* 对端关闭 */
            return;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                handle_tcp_close(client, epoll_fd);
            break;  /* EAGAIN：本轮读完 */
        }
    }
}

**`memmove` 而非 `memcpy`**：把剩余数据移到缓冲区开头时，源和目标在同一块内存中重叠——`memcpy` 在重叠时行为未定义，`memmove` 保证正确处理。

## ⚠ 重要：先读数据，再处理挂断

当客户端发完数据立刻 `close`，epoll 可能同时返回 `EPOLLIN` 和 `EPOLLRDHUP`。如果先处理挂断，缓冲区里没读完的数据就丢了：

```c
/* ✅ 正确顺序 */
if (events[i].events & EPOLLIN)
    handle_tcp_data(client, ...);   /* 先读数据 */
if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    handle_tcp_close(client, ...);  /* 再关闭连接 */
```

**EPOLLRDHUP vs EPOLLHUP**：

- `EPOLLRDHUP`：对端关闭了读端（发送了 FIN），本端仍可发送数据（半关闭）
- `EPOLLHUP`：连接完全断开。通常和 EPOLLRDHUP 一起处理

## UDP：简单但不可靠

UDP 是数据报协议，每个 `recvfrom` 返回一条完整消息。不需要行缓冲，不需要 accept。

在 `src/master.c` 中添加 `handle_udp_data`：

```c
/* 边缘触发循环 recvfrom 直到 EAGAIN */
static void handle_udp_data(int udp_fd, shm_header_t *header, void *slots) {
    char buf[UDP_RECV_BUF_SIZE];
    uint64_t ts = get_timestamp();

    while (1) {
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        ssize_t n = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&addr, &addr_len);
        if (n <= 0) break;  /* EAGAIN 或错误 */

        buf[n] = '\0';
        if (n > 0 && buf[n - 1] == '\n') buf[--n] = '\0';
        if (n > 0) shm_produce(header, slots, &addr, 1, buf, (uint32_t)n, ts);
    }
}
```

UDP 不保证送达。如果接收缓冲区满了，内核静默丢弃数据报。增大 `UDP_RECV_BUF_SIZE`（64KB）能减少丢包概率，但不能完全避免。这是 UDP 的天性——要可靠就用 TCP。

## 怎么验证

> **注意**：完整的功能验证需要等第 4 篇（共享内存）和第 5 篇（进程池+文件写入）完成后才能进行。目前可以验证网络监听是否正常——程序不会在 `bind` 时报错，`epoll_wait` 能正常返回。

```bash
# 编译运行
$ ./log_collector &
# 应该看到 [INFO] 启动日志（看 journal：journalctl -u log-collector -f）
# 没有 "bind" 错误说明 TCP/UDP 端口监听正常

# 完整验证（第 5 篇完成后）
$ echo "hello from tcp" | nc -q1 127.0.0.1 5140
$ cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log
2026-06-24T08:39:31+0800 127.0.0.1 [debug] hello from tcp

# UDP 测试
$ echo "hello from udp" | nc -u -w1 127.0.0.1 5140
$ cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log
2026-06-24T08:39:31+0800 127.0.0.1 [debug] hello from tcp
2026-06-24T08:39:32+0800 127.0.0.1 [debug] hello from udp
```

TCP 和 UDP 都能收到数据，日志按 IP 分目录、按日期分文件存储。注意 TCP 和 UDP 的消息都正确出现在同一个日志文件中——epoll 同时管理两种协议，行缓冲正确拆分了 TCP 消息。

## 你现在应该理解的

**epoll 的 O(1) 来自就绪链表**：`epoll_wait` 不遍历所有 fd，边缘触发要求循环读直到 EAGAIN。

**TCP 是字节流，UDP 是数据报**：TCP 需要行缓冲自己拆消息，UDP 天然有边界。

**EPOLLIN 必须在 EPOLLRDHUP 之前处理**：先读数据，再关连接。
