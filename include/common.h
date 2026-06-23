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
#include <systemd/sd-journal.h>

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
    char     listen_addr[64];
    int      tcp_port;
    int      udp_port;
    int      max_connections;
    int      worker_count;
    uint64_t slot_size;
    uint64_t slot_count;
    char     log_dir[256];
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
