/* src/worker.c */
#include "worker.h"
#include "shm_buffer.h"
#include "log_parser.h"
#include "file_writer.h"

/*
 * worker.c — Worker 进程模块
 *
 * Worker 是独立的子进程（Master fork），负责消费共享内存中的日志、
 * 格式化后写入磁盘文件。
 *
 * 处理流水线：
 *   原始字节 → shm_consume → log_parser_format → file_writer_write → 磁盘
 *
 * 退出条件：消费到 data_len==0 的哨兵槽位时退出循环并清理资源。
 */

/*
 * worker_run — Worker 进程主函数
 *
 * 注意：fork 后子进程必须重置信号处理为默认值，因为 fork 会继承父进程的 handler。
 */
void worker_run(const config_t *cfg) {
    shm_header_t *header = NULL;
    void *slots = NULL;
    uint64_t slot_size = 0, slot_count = 0;
    file_writer_t fw;
    char *buf = NULL;

    /* 连接共享内存 */
    if (shm_connect(&header, &slots, &slot_size, &slot_count) != 0) _exit(1);

    /* 分配接收缓冲区（大小 = slot_size，足够容纳最大数据） */
    if (!(buf = malloc((size_t)slot_size))) { shm_disconnect(header, slots); _exit(1); }

    /* 初始化文件写入器 */
    file_writer_init(&fw, cfg->log_dir);

    /* 主消费循环 */
    for (;;) {
        struct sockaddr_storage addr;
        uint8_t protocol;
        uint32_t data_len;
        uint64_t timestamp;

        int ret = shm_consume(header, slots, slot_size,
                              &addr, &protocol, buf, &data_len, &timestamp);

        if (data_len == 0) break;   /* 哨兵：退出 */
        if (ret <= 0)    continue;  /* 无数据 */

        /* 格式化日志 */
        char formatted[8192];
        int written = log_parser_format(&addr, timestamp,
                                        buf, data_len, formatted, sizeof(formatted));
        if (written <= 0) continue;

        /* 提取 IP 字符串 */
        char ip_str[INET6_ADDRSTRLEN];
        if (addr.ss_family == AF_INET)
            inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr,
                      ip_str, sizeof(ip_str));
        else if (addr.ss_family == AF_INET6)
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr,
                      ip_str, sizeof(ip_str));
        else
            snprintf(ip_str, sizeof(ip_str), "unknown");

        /* 写入文件 */
        file_writer_write(&fw, ip_str, formatted, written);
    }

    /* 清理 */
    file_writer_close(&fw);
    shm_disconnect(header, slots);
    free(buf);
    _exit(0);
}
