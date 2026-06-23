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
