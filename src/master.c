/* src/master.c */
#include "master.h"
#include "shm_buffer.h"
#include "worker.h"
#include "signal_handler.h"

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

static int create_tcp_listener(const config_t *cfg) {
    int fd;
    int reuse = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket TCP");
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(fd);
        return -1;
    }

    /* 设置非阻塞 */
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            perror("fcntl F_GETFL");
            close(fd);
            return -1;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            perror("fcntl O_NONBLOCK");
            close(fd);
            return -1;
        }
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)cfg->tcp_port);

    if (strcmp(cfg->listen_addr, "0.0.0.0") == 0 || strcmp(cfg->listen_addr, "*") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, cfg->listen_addr, &addr.sin_addr) <= 0) {
            fprintf(stderr, "Invalid listen_addr: %s\n", cfg->listen_addr);
            close(fd);
            return -1;
        }
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind TCP");
        close(fd);
        return -1;
    }

    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

static int create_udp_listener(const config_t *cfg) {
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket UDP");
        return -1;
    }

    /* 设置非阻塞 */
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            perror("fcntl F_GETFL");
            close(fd);
            return -1;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            perror("fcntl O_NONBLOCK");
            close(fd);
            return -1;
        }
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)cfg->udp_port);

    if (strcmp(cfg->listen_addr, "0.0.0.0") == 0 || strcmp(cfg->listen_addr, "*") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, cfg->listen_addr, &addr.sin_addr) <= 0) {
            fprintf(stderr, "Invalid listen_addr: %s\n", cfg->listen_addr);
            close(fd);
            return -1;
        }
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind UDP");
        close(fd);
        return -1;
    }

    return fd;
}

/* 从缓冲区中提取一条完整的行 (以 '\n' 结尾)。
   返回行的长度 (包含 '\n')，如果缓冲区中没有完整行则返回 0。 */
static int extract_line(char *buf, int len) {
    int i;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            return i + 1;
        }
    }
    return 0;
}

/* 处理 TCP 客户端收到 HUP 或错误事件 */
static void handle_tcp_close(tcp_client_t *client, int epoll_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    client->fd = -1;
    client->buf_len = 0;
}

/* 处理 TCP 数据: edge-triggered 循环读取直到 EAGAIN */
static void handle_tcp_data(tcp_client_t *client, int epoll_fd,
                            shm_header_t *header, void *slots) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int remaining;
    ssize_t n;
    int line_len;
    uint64_t ts;

    /* 获取客户端地址 */
    if (getpeername(client->fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        handle_tcp_close(client, epoll_fd);
        return;
    }

    ts = get_timestamp();

    while (1) {
        remaining = TCP_RECV_BUF_SIZE - client->buf_len - 1; /* 留一个 '\0' */
        if (remaining <= 0) {
            /* 缓冲区满，丢弃旧数据或断开 */
            handle_tcp_close(client, epoll_fd);
            return;
        }

        n = read(client->fd, client->recv_buf + client->buf_len, (size_t)remaining);
        if (n > 0) {
            client->buf_len += (int)n;
            client->recv_buf[client->buf_len] = '\0';

            /* 提取完整行 */
            while ((line_len = extract_line(client->recv_buf, client->buf_len)) > 0) {
                /* 去掉末尾 '\n' */
                if (line_len > 0 && client->recv_buf[line_len - 1] == '\n') {
                    client->recv_buf[line_len - 1] = '\0';
                    line_len--;
                }
                /* 注意：data_len == 0 在 shm_produce 中是合法的（哨兵检测在 shm_consume 中处理），
                   对于 master 来说，空行被忽略 */
                if (line_len > 0) {
                    shm_produce(header, slots, &addr, 0, /* protocol=TCP */
                                client->recv_buf, (uint32_t)line_len, ts);
                }
                /* 将剩余数据移到缓冲区开头 */
                client->buf_len -= (line_len + 1); /* +1 for the removed '\n', which we replaced with '\0' */
                {
                    int remaining_data = client->buf_len;
                    if (remaining_data > 0) {
                        memmove(client->recv_buf,
                                client->recv_buf + line_len + 1,
                                (size_t)remaining_data);
                    }
                }
            }
        } else if (n == 0) {
            /* 对端关闭连接 */
            handle_tcp_close(client, epoll_fd);
            return;
        } else {
            /* n < 0 */
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                handle_tcp_close(client, epoll_fd);
                return;
            }
            break; /* EAGAIN: 本轮数据读完 */
        }
    }
}

/* 处理 UDP 数据: edge-triggered 循环 recvfrom 直到 EAGAIN */
static void handle_udp_data(int udp_fd, shm_header_t *header, void *slots) {
    char buf[UDP_RECV_BUF_SIZE];
    ssize_t n;
    uint64_t ts;

    ts = get_timestamp();

    while (1) {
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        n = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0,
                     (struct sockaddr *)&addr, &addr_len);
        if (n <= 0) {
            break; /* EAGAIN or error */
        }

        buf[n] = '\0';

        /* 去掉末尾 '\n' */
        if (n > 0 && buf[n - 1] == '\n') {
            buf[--n] = '\0';
        }

        if (n > 0) {
            shm_produce(header, slots, &addr, 1, /* protocol=UDP */
                        buf, (uint32_t)n, ts);
        }
    }
}

/* 派生 worker 进程池。返回 pid 数组，调用者负责 free。 */
static pid_t *fork_workers(const config_t *cfg) {
    pid_t *worker_pids;
    int i;

    worker_pids = calloc((size_t)cfg->worker_count, sizeof(pid_t));
    if (worker_pids == NULL) {
        return NULL;
    }

    for (i = 0; i < cfg->worker_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            /* 子进程 */
            /* 重置信号处理为默认 (避免继承 master 的 handler) */
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGHUP, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
            signal(SIGPIPE, SIG_DFL);

            free(worker_pids);

            worker_run(cfg);

            _exit(0);
        } else if (pid > 0) {
            worker_pids[i] = pid;
        } else {
            /* fork 失败 */
            perror("fork worker");
            /* 继续运行，可能部分 worker 未启动 */
        }
    }

    return worker_pids;
}

/* 收割并重启退出的 worker 子进程 */
static void reap_workers(pid_t *worker_pids, int worker_count, const config_t *cfg) {
    int status;
    pid_t pid;
    int i;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        /* 找到这个 pid 对应的索引 */
        for (i = 0; i < worker_count; i++) {
            if (worker_pids[i] == pid) {
                /* 重启该 worker */
                pid_t new_pid = fork();
                if (new_pid == 0) {
                    /* 子进程 */
                    signal(SIGTERM, SIG_DFL);
                    signal(SIGINT, SIG_DFL);
                    signal(SIGHUP, SIG_DFL);
                    signal(SIGCHLD, SIG_DFL);
                    signal(SIGPIPE, SIG_DFL);
                    worker_run(cfg);
                    _exit(0);
                } else if (new_pid > 0) {
                    worker_pids[i] = new_pid;
                }
                break;
            }
        }
    }
}

/* 等待所有 worker 退出 (带超时) */
static void wait_workers(pid_t *worker_pids, int worker_count) {
    int i;
    int alive_count;
    int waited;
    int timeout_seconds = 10;

    for (waited = 0; waited < timeout_seconds; waited++) {
        alive_count = 0;
        for (i = 0; i < worker_count; i++) {
            if (worker_pids[i] <= 0) continue;
            int status;
            pid_t result = waitpid(worker_pids[i], &status, WNOHANG);
            if (result == 0) {
                alive_count++;
            } else if (result > 0) {
                worker_pids[i] = -1; /* 标记已退出 */
            }
        }
        if (alive_count == 0) break;
        sleep(1);
    }

    /* 对于仍未退出的 worker，发送 SIGKILL */
    for (i = 0; i < worker_count; i++) {
        if (worker_pids[i] > 0) {
            kill(worker_pids[i], SIGKILL);
            waitpid(worker_pids[i], NULL, 0);
        }
    }
}

/* 关闭所有还活跃的 TCP 客户端 */
static void close_all_clients(tcp_client_t *clients, int max_connections) {
    int i;
    for (i = 0; i < max_connections; i++) {
        if (clients[i].fd >= 0) {
            close(clients[i].fd);
            clients[i].fd = -1;
        }
    }
}

int master_run(const config_t *cfg, shm_header_t *header, void *slots) {
    int tcp_fd = -1;
    int udp_fd = -1;
    int epoll_fd = -1;
    tcp_client_t *clients = NULL;
    pid_t *worker_pids = NULL;
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];
    int i, nfds;
    int ret = -1;

    /* 1. 创建监听 socket */
    tcp_fd = create_tcp_listener(cfg);
    if (tcp_fd < 0) goto cleanup;

    udp_fd = create_udp_listener(cfg);
    if (udp_fd < 0) goto cleanup;

    /* 2. 创建 epoll 实例 */
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        goto cleanup;
    }

    /* 注册 TCP listener (edge-triggered) */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = tcp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &ev) < 0) {
        perror("epoll_ctl TCP listener");
        goto cleanup;
    }

    /* 注册 UDP listener (edge-triggered) */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = udp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &ev) < 0) {
        perror("epoll_ctl UDP listener");
        goto cleanup;
    }

    /* 3. 分配 TCP 客户端状态数组 */
    clients = calloc((size_t)cfg->max_connections, sizeof(tcp_client_t));
    if (clients == NULL) {
        perror("calloc clients");
        goto cleanup;
    }
    for (i = 0; i < cfg->max_connections; i++) {
        clients[i].fd = -1;
    }

    /* 4. 派生 Worker 进程池 */
    worker_pids = fork_workers(cfg);
    if (worker_pids == NULL) {
        perror("calloc worker_pids");
        goto cleanup;
    }

    /* 5. epoll 事件主循环 */
    while (!g_shutdown) {
        /* 在进入 epoll_wait 前先处理 SIGCHLD */
        if (g_sigchld) {
            g_sigchld = 0;
            reap_workers(worker_pids, cfg->worker_count, cfg);
        }

        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100); /* 100ms 超时，便于检查 shutdown */
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (i = 0; i < nfds; i++) {
            if (events[i].data.fd == tcp_fd) {
                /* TCP accept (edge-triggered loop) */
                while (1) {
                    struct sockaddr_storage addr;
                    socklen_t addr_len = sizeof(addr);
                    int client_fd = accept4(tcp_fd, (struct sockaddr *)&addr, &addr_len,
                                            SOCK_NONBLOCK);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        break;
                    }

                    /* 查找空闲 slot */
                    {
                        int slot;
                        int found = 0;
                        for (slot = 0; slot < cfg->max_connections; slot++) {
                            if (clients[slot].fd < 0) {
                                clients[slot].fd = client_fd;
                                clients[slot].buf_len = 0;
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            /* 连接数已满，拒绝 */
                            close(client_fd);
                            continue;
                        }

                        /* 注册到 epoll */
                        memset(&ev, 0, sizeof(ev));
                        ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                        ev.data.ptr = &clients[slot];
                        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                            close(client_fd);
                            clients[slot].fd = -1;
                        }
                    }
                }
            } else if (events[i].data.fd == udp_fd) {
                /* UDP 数据 */
                handle_udp_data(udp_fd, header, slots);
            } else {
                /* TCP 客户端数据 */
                tcp_client_t *client = (tcp_client_t *)events[i].data.ptr;

                /* 检查是否挂断或错误 */
                if ((events[i].events & EPOLLRDHUP) ||
                    (events[i].events & EPOLLHUP) ||
                    (events[i].events & EPOLLERR)) {
                    handle_tcp_close(client, epoll_fd);
                    continue;
                }

                if (events[i].events & EPOLLIN) {
                    handle_tcp_data(client, epoll_fd, header, slots);
                }
            }
        }
    }

    /* 6. 优雅关闭 */
    ret = 0;

cleanup:
    /* 关闭监听 sockets */
    if (tcp_fd >= 0) close(tcp_fd);
    if (udp_fd >= 0) close(udp_fd);

    /* 发送哨兵 */
    if (header != NULL) {
        shm_send_sentinels(header, cfg->worker_count);
    }

    /* 等待 worker 退出 */
    if (worker_pids != NULL) {
        wait_workers(worker_pids, cfg->worker_count);
        free(worker_pids);
    }

    /* 关闭所有客户端连接 */
    if (clients != NULL) {
        close_all_clients(clients, cfg->max_connections);
        free(clients);
    }

    /* 关闭 epoll */
    if (epoll_fd >= 0) close(epoll_fd);

    return ret;
}
