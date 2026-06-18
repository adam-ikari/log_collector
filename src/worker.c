/* src/worker.c */
#include "worker.h"
#include "shm_buffer.h"
#include "log_parser.h"
#include "file_writer.h"

void worker_run(const config_t *cfg)
{
    shm_header_t *header = NULL;
    void *slots = NULL;
    uint64_t slot_size = 0;
    uint64_t slot_count = 0;
    file_writer_t fw;
    char buf[4096];
    char ip_str[INET6_ADDRSTRLEN];
    struct sockaddr_storage addr;
    uint8_t protocol;
    uint32_t data_len;
    uint64_t timestamp;

    /* 1. 连接共享内存 */
    if (shm_connect(&header, &slots, &slot_size, &slot_count) != 0) {
        _exit(1);
    }

    /* 2. 初始化文件写入器 */
    file_writer_init(&fw, cfg->log_dir);

    /* 3. 主消费循环 */
    for (;;) {
        int ret = shm_consume(header, slots, slot_size,
                              &addr, &protocol,
                              buf, &data_len, &timestamp);

        /* 4. 检查哨兵 (data_len==0 表示退出) */
        if (data_len == 0) {
            break;
        }

        /* 忽略无数据返回 (ret==-1, data_len 不变) */
        if (ret <= 0) {
            continue;
        }

        /* 5. 格式化日志 */
        char formatted[8192];
        int written = log_parser_format(&addr, timestamp,
                                        buf, data_len,
                                        formatted, sizeof(formatted));
        if (written <= 0) {
            continue;
        }

        /* 6. 提取 IP 字符串 */
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

        /* 7. 写入文件 */
        file_writer_write(&fw, ip_str, formatted, written);
    }

    /* 8. 清理 */
    file_writer_close(&fw);
    shm_disconnect(header, slots);

    _exit(0);
}
