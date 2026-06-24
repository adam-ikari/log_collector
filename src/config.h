/* src/config.h — 编译期配置（教学项目不需要配置文件） */
#ifndef CONFIG_H
#define CONFIG_H

#define CFG_LISTEN_ADDR       "0.0.0.0"
#define CFG_TCP_PORT          5140
#define CFG_UDP_PORT          5140
#define CFG_MAX_CONNECTIONS   1024
#define CFG_WORKER_COUNT      4
#define CFG_SLOT_SIZE         4096
#define CFG_SLOT_COUNT        1024
#define CFG_LOG_DIR           "/tmp/log_collector_test"
#define CFG_PID_FILE          "/tmp/log-collector.pid"

#endif
