/* src/shm_buffer.h */
#ifndef SHM_BUFFER_H
#define SHM_BUFFER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* SHM_BUFFER_H */
