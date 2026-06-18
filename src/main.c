/* src/main.c */
#include "common.h"
#include "config.h"
#include "daemon.h"
#include "shm_buffer.h"
#include "signal_handler.h"
#include "master.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-f] [-c <config>]\n", prog);
    fprintf(stderr, "  -f          Run in foreground (do not daemonize)\n");
    fprintf(stderr, "  -c <path>   Config file path (default: %s)\n",
            DEFAULT_CONF_PATH);
    fprintf(stderr, "  -h          Show this help\n");
}

int main(int argc, char *argv[]) {
    config_t cfg;
    shm_header_t *shm_header = NULL;
    void *slots = NULL;
    int foreground = 0;
    const char *conf_path = DEFAULT_CONF_PATH;
    int opt;
    int rc;

    /* 解析命令行参数 */
    while ((opt = getopt(argc, argv, "fc:h")) != -1) {
        switch (opt) {
        case 'f':
            foreground = 1;
            break;
        case 'c':
            conf_path = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* 加载配置 */
    config_load(&cfg, conf_path);

    /* 守护进程化 */
    if (!foreground) {
        if (daemonize(DEFAULT_PID_FILE) < 0) {
            fprintf(stderr, "Failed to daemonize\n");
            return 1;
        }
    }

    /* 初始化信号处理 */
    if (signal_handlers_init() < 0) {
        syslog(LOG_ERR, "Failed to initialize signal handlers");
        if (!foreground) daemon_cleanup(DEFAULT_PID_FILE);
        return 1;
    }

    /* 初始化共享内存 */
    if (shm_init(&shm_header, &slots, cfg.slot_size, cfg.slot_count) < 0) {
        syslog(LOG_ERR, "Failed to initialize shared memory");
        if (!foreground) daemon_cleanup(DEFAULT_PID_FILE);
        return 1;
    }

    /* 启动 Master (包含 Worker 进程池) */
    rc = master_run(&cfg, shm_header, slots);

    /* 清理 */
    shm_destroy(shm_header, slots, cfg.slot_count);
    if (!foreground) daemon_cleanup(DEFAULT_PID_FILE);

    return rc;
}
