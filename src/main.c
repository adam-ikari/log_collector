/* src/main.c — 程序入口：串联所有模块 */
#include "common.h"
#include "config.h"
#include "signal_handler.h"
#include "shm_buffer.h"
#include "master.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "用法: %s [-h]\n", prog);
    fprintf(stderr, "  -h   显示帮助\n");
}

/*
 * 启动流程：
 *   解析参数 → 注册信号 → 创建共享内存 → Master 主循环 → 清理
 *
 * 守护进程化由 systemd Type=simple 负责，程序本身只做前台运行。
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
    int opt, rc;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    sd_journal_print(LOG_INFO, "Log Collector 启动 (TCP:%d UDP:%d workers:%d)",
                     cfg.tcp_port, cfg.udp_port, cfg.worker_count);

    if (signal_handlers_init() < 0) {
        sd_journal_print(LOG_ERR, "信号注册失败");
        return 1;
    }

    if (shm_init(&shm_header, &slots, cfg.slot_size, cfg.slot_count) < 0) {
        sd_journal_print(LOG_ERR, "共享内存初始化失败");
        return 1;
    }

    rc = master_run(&cfg, shm_header, slots);

    sd_journal_print(LOG_INFO, "Log Collector 已退出 (rc=%d)", rc);

    shm_destroy(shm_header, slots, cfg.slot_count);
    return rc;
}
