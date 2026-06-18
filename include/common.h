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
