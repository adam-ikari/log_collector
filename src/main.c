/* src/main.c — 程序入口：串联所有模块 */
#include "common.h"
#include "config.h"
#include "daemon.h"
#include "shm_buffer.h"
#include "signal_handler.h"
#include "master.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "用法: %s [-f]\n", prog);
    fprintf(stderr, "  -f   前台运行\n");
    fprintf(stderr, "  -h   显示帮助\n");
}

/*
 * 启动流程：
 *   解析参数 → 守护进程化 → 注册信号 → 创建共享内存 → Master 主循环 → 清理
 */
int main(int argc, char *argv[]) {
    config_t cfg = {
        .listen_addr     = CFG_LISTEN_ADDR,
        .tcp_port        = CFG_TCP_PORT,
        .udp_port        = CFG_UDP_PORT,
        .max_connections = CFG_MAX_CONNECTIONS,
        .worker_count    = CFG_WORKER_COUNT,
        .slot_size       = CFG_SLOT_SIZE,
        .slot_count      = CFG_SLOT_COUNT,
        .log_dir         = CFG_LOG_DIR,
    };
    shm_header_t *shm_header = NULL;
    void *slots = NULL;
    int foreground = 0, opt, rc;

    while ((opt = getopt(argc, argv, "fh")) != -1) {
        switch (opt) {
        case 'f': foreground = 1; break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    if (!foreground && daemonize(CFG_PID_FILE) < 0) {
        fprintf(stderr, "变成守护进程失败\n");
        return 1;
    }

    sd_journal_print(LOG_INFO, "Log Collector 启动 (TCP:%d UDP:%d workers:%d)",
                     cfg.tcp_port, cfg.udp_port, cfg.worker_count);

    if (signal_handlers_init() < 0) {
        sd_journal_print(LOG_ERR, "信号注册失败");
        if (!foreground) daemon_cleanup(CFG_PID_FILE);
        return 1;
    }

    if (shm_init(&shm_header, &slots, cfg.slot_size, cfg.slot_count) < 0) {
        sd_journal_print(LOG_ERR, "共享内存初始化失败");
        if (!foreground) daemon_cleanup(CFG_PID_FILE);
        return 1;
    }

    rc = master_run(&cfg, shm_header, slots);

    sd_journal_print(LOG_INFO, "Log Collector 已退出 (rc=%d)", rc);

    shm_destroy(shm_header, slots, cfg.slot_count);
    if (!foreground) daemon_cleanup(CFG_PID_FILE);
    return rc;
}
