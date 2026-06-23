/* src/master.c */
#include "master.h"
#include "shm_buffer.h"
#include "worker.h"
#include "signal_handler.h"

/*
 * master.c — Master 进程
 *
 * 职责：epoll 事件循环、TCP/UDP 网络 I/O、Worker 进程池管理。
 *
 * 数据流：
 *   网络日志 → handle_tcp_data/handle_udp_data → shm_produce → 共享内存
 *
 * Worker 管理：
 *   fork_workers → 创建进程池
 *   reap_workers → SIGCHLD 收割 + 自动重启
 *   wait_workers → 优雅关闭（超时 10 秒 → SIGKILL）
 */

/* TCP 客户端状态 */
typedef struct {
    int  fd;
    char recv_buf[TCP_RECV_BUF_SIZE];
    int  buf_len;
} tcp_client_t;

static uint64_t get_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec;
}

/* ── 创建非阻塞 socket 的公共逻辑 ────────────── */

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int resolve_addr(const char *str, struct sockaddr_in *addr) {
    if (strcmp(str, "0.0.0.0") == 0 || strcmp(str, "*") == 0) {
        addr->sin_addr.s_addr = INADDR_ANY;
        return 0;
    }
    return inet_pton(AF_INET, str, &addr->sin_addr) <= 0 ? -1 : 0;
}

/* ── 创建 TCP/UDP 监听 socket ───────────────── */

/*
 * create_tcp_listener — 创建 TCP 监听 socket
 *
 * 步骤：socket → SO_REUSEADDR → O_NONBLOCK → bind → listen
 * 返回非阻塞的监听 fd，失败返回 -1。
 */
static int create_tcp_listener(const config_t *cfg) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket TCP"); return -1; }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
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

/*
 * create_udp_listener — 创建 UDP 监听 socket
 *
 * UDP 不需要 listen（无连接协议），直接 bind 后即可 recvfrom。
 */
static int create_udp_listener(const config_t *cfg) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket UDP"); return -1; }

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

/* ── TCP 行缓冲 ─────────────────────────────── */

/*
 * extract_line — 从缓冲区中提取一条以 '\n' 结尾的完整行
 *
 * 返回行的长度（包含 '\n'），没有完整行则返回 0。
 * TCP 是流式协议，需要行缓冲来拆分完整行。
 */
static int extract_line(const char *buf, int len) {
    for (int i = 0; i < len; i++)
        if (buf[i] == '\n') return i + 1;
    return 0;
}

/*
 * handle_tcp_close — 关闭 TCP 客户端连接
 *
 * 从 epoll 中删除 fd，关闭连接，重置客户端状态。
 */
static void handle_tcp_close(tcp_client_t *client, int epoll_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    client->fd = -1;
    client->buf_len = 0;
}

/*
 * handle_tcp_data — 边缘触发循环读取 TCP 数据直到 EAGAIN
 *
 * TCP 是流式协议，需要行缓冲：提取以 \n 结尾的完整行，剩余数据保留在缓冲区。
 * 边缘触发（EPOLLET）下必须循环 read 直到返回 EAGAIN，否则可能漏数据。
 */
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
        if (remaining <= 0) { handle_tcp_close(client, epoll_fd); return; }

        ssize_t n = read(client->fd, client->recv_buf + client->buf_len, remaining);
        if (n > 0) {
            client->buf_len += n;
            client->recv_buf[client->buf_len] = '\0';

            int line_len;
            while ((line_len = extract_line(client->recv_buf, client->buf_len)) > 0) {
                /* 去掉末尾 '\n'，写入共享内存 */
                client->recv_buf[line_len - 1] = '\0';
                if (line_len > 1)
                    shm_produce(header, slots, &addr, 0,
                                client->recv_buf, (uint32_t)(line_len - 1), ts);
                /* 将剩余数据移到缓冲区开头 */
                int remain = client->buf_len - line_len;
                if (remain > 0)
                    memmove(client->recv_buf, client->recv_buf + line_len, remain);
                client->buf_len = remain;
            }
        } else if (n == 0) {
            handle_tcp_close(client, epoll_fd); return;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                handle_tcp_close(client, epoll_fd);
            break; /* EAGAIN: 本轮数据读完 */
        }
    }
}

/* ── UDP 数据读取 ───────────────────────────── */

/*
 * handle_udp_data — 边缘触发循环 recvfrom 直到 EAGAIN
 *
 * UDP 是数据报协议，每个 recvfrom 返回一条完整消息，无需行缓冲。
 */
static void handle_udp_data(int udp_fd, shm_header_t *header, void *slots) {
    char buf[UDP_RECV_BUF_SIZE];
    uint64_t ts = get_timestamp();

    while (1) {
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        ssize_t n = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&addr, &addr_len);
        if (n <= 0) break;

        buf[n] = '\0';
        if (n > 0 && buf[n - 1] == '\n') buf[--n] = '\0';
        if (n > 0) shm_produce(header, slots, &addr, 1, buf, (uint32_t)n, ts);
    }
}

/* ── Worker 进程池管理 ──────────────────────── */

/* 子进程中重置信号处理为默认值（fork 会继承父进程的 handler） */
static void reset_signals(void) {
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT,  SIG_DFL);
    signal(SIGHUP,  SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
}

/*
 * fork_workers — 派生 Worker 进程池
 *
 * 循环 fork cfg->worker_count 个子进程，每个子进程重置信号处理后调用 worker_run。
 * 返回 pid 数组，调用者负责 free。
 */
static pid_t *fork_workers(const config_t *cfg) {
    pid_t *pids = calloc((size_t)cfg->worker_count, sizeof(pid_t));
    if (!pids) return NULL;

    for (int i = 0; i < cfg->worker_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            reset_signals();
            free(pids);
            worker_run(cfg);
            _exit(0);
        } else if (pid > 0) {
            pids[i] = pid;
            sd_journal_print(LOG_INFO, "Worker %d 已启动 (PID=%d)", i, pid);
        } else {
            sd_journal_print(LOG_ERR, "fork worker %d 失败: %s", i, strerror(errno));
        }
    }
    return pids;
}

/*
 * reap_workers — 收割并重启退出的 worker 子进程
 *
 * 使用 waitpid(WNOHANG) 非阻塞检查是否有子进程退出。
 * 发现退出的 worker 后立即 fork 新进程替换，保证 worker 数量不变。
 */
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
                    sd_journal_print(LOG_ERR, "重启 Worker %d 失败: %s", i, strerror(errno));
                }
                break;
            }
        }
    }
}

/*
 * wait_workers — 等待所有 worker 退出（带超时）
 *
 * 策略：先等待 10 秒让 worker 自然退出（每秒轮询一次），
 * 超时后对仍未退出的 worker 发送 SIGKILL 强制终止。
 */
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

/* ── Master 主循环 ──────────────────────────── */

/*
 * master_run — Master 进程主循环
 *
 * 完整生命周期：
 *   1. 创建 TCP/UDP 监听 socket（非阻塞）
 *   2. 创建 epoll 实例，注册 EPOLLET 边缘触发
 *   3. 分配 TCP 客户端状态数组
 *   4. fork Worker 进程池
 *   5. epoll 事件主循环：
 *      a. 处理 SIGCHLD → reap_workers
 *      b. epoll_wait 100ms 超时
 *      c. TCP listener → accept（循环到 EAGAIN）
 *      d. UDP listener → handle_udp_data
 *      e. TCP client → handle_tcp_data（先读后处理挂断）
 *   6. 收到 shutdown 后：关闭监听 → 发哨兵 → 等 worker → 清客户端 → 关 epoll
 *
 * 返回 0 正常退出，-1 错误。
 */
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
                /* TCP accept（边缘触发循环） */
                while (1) {
                    struct sockaddr_storage addr;
                    socklen_t addr_len = sizeof(addr);
                    int client_fd = accept(tcp_fd, (struct sockaddr *)&addr, &addr_len);
                    if (client_fd < 0) break; /* EAGAIN 或错误 */
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
                /* TCP 客户端数据：先读数据，再处理挂断 */
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
