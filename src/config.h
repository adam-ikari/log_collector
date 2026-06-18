/* src/config.h */
#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

/* 加载配置：先设默认值，再读文件覆盖。返回 0 成功，-1 读文件失败(不致命，用默认值) */
int config_load(config_t *cfg, const char *path);

#endif /* CONFIG_H */
