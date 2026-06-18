/* src/worker.h */
#ifndef WORKER_H
#define WORKER_H

#include "common.h"

/* Worker 主循环。由 Master fork 后调用，不返回。 */
void worker_run(const config_t *cfg);

#endif /* WORKER_H */
