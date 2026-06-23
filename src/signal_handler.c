/* src/signal_handler.c */
#include "signal_handler.h"

/*
 * 信号处理函数只设置全局标志位，不在信号处理函数中做复杂操作。
 * 主循环轮询这些标志来决定做什么，避免信号处理函数中的重入问题。
 *
 * volatile sig_atomic_t：保证在信号处理函数中读写是原子的。
 */
volatile sig_atomic_t g_shutdown = 0;
volatile sig_atomic_t g_sighup   = 0;
volatile sig_atomic_t g_sigchld  = 0;

static void handle_sigterm(int sig) { (void)sig; g_shutdown = 1; }
static void handle_sighup(int sig)  { (void)sig; g_sighup   = 1; }
static void handle_sigchld(int sig) { (void)sig; g_sigchld  = 1; }

/*
 * signal_handlers_init — 注册所有信号处理函数
 *
 * SIGTERM/SIGINT → handle_sigterm（设置 g_shutdown=1）
 * SIGHUP        → handle_sighup（设置 g_sighup=1，可用于 reload 配置）
 * SIGCHLD       → handle_sigchld（设置 g_sigchld=1，触发 worker 收割）
 * SIGPIPE       → SIG_IGN（忽略，避免写已关闭的 socket 导致进程退出）
 *
 * Linux 上 signal() 默认带 SA_RESTART——被信号中断的系统调用会自动重启。
 * 对 epoll_wait 这意味着信号来了也不会返回，可能永远检查不到 g_shutdown。
 * 所以注册后用 siginterrupt() 关掉 SA_RESTART。
 */
int signal_handlers_init(void) {
    if (signal(SIGTERM, handle_sigterm) == SIG_ERR ||
        signal(SIGINT,  handle_sigterm) == SIG_ERR) return -1;
    siginterrupt(SIGTERM, 1);
    siginterrupt(SIGINT,  1);

    if (signal(SIGHUP,  handle_sighup) == SIG_ERR  ||
        signal(SIGCHLD, handle_sigchld) == SIG_ERR ||
        signal(SIGPIPE, SIG_IGN)         == SIG_ERR) return -1;
    siginterrupt(SIGHUP,  1);
    siginterrupt(SIGCHLD, 1);

    return 0;
}
