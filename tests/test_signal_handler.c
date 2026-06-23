/* tests/test_signal_handler.c */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include "signal_handler.h"

/*
 * test_signal_handler — 测试信号处理模块
 *
 * 测试 signal_handlers_init 注册和全局标志位机制。
 * 注意：signal() 的注册是全局副作用，测试需谨慎。
 */

static void test_handlers_init(void) {
    /* signal_handlers_init 应该成功返回 0 */
    int rc = signal_handlers_init();
    assert(rc == 0);
    printf("PASS: test_handlers_init\n");
}

static void test_global_flags_initial(void) {
    /* 初始状态：所有标志应为 0 */
    assert(g_shutdown == 0);
    assert(g_sighup == 0);
    assert(g_sigchld == 0);
    printf("PASS: test_global_flags_initial\n");
}

static void test_shutdown_flag_set(void) {
    /* 模拟信号处理：直接设置标志位（和 handler 做同样的事） */
    g_shutdown = 0;
    g_shutdown = 1;
    assert(g_shutdown == 1);
    g_shutdown = 0;  /* 重置 */
    printf("PASS: test_shutdown_flag_set\n");
}

static void test_sighup_flag_set(void) {
    g_sighup = 0;
    g_sighup = 1;
    assert(g_sighup == 1);
    g_sighup = 0;
    printf("PASS: test_sighup_flag_set\n");
}

static void test_sigchld_flag_set(void) {
    g_sigchld = 0;
    g_sigchld = 1;
    assert(g_sigchld == 1);
    g_sigchld = 0;
    printf("PASS: test_sigchld_flag_set\n");
}

static void test_sigterm_triggers_shutdown(void) {
    /* 发送 SIGTERM 给自己，验证 handler 设置了 g_shutdown */
    g_shutdown = 0;
    signal_handlers_init();

    /* raise 发送信号给自己 */
    raise(SIGTERM);

    assert(g_shutdown == 1);
    g_shutdown = 0;  /* 重置 */
    printf("PASS: test_sigterm_triggers_shutdown\n");
}

static void test_sigint_triggers_shutdown(void) {
    /* SIGINT 也应该触发 g_shutdown */
    g_shutdown = 0;
    signal_handlers_init();

    raise(SIGINT);

    assert(g_shutdown == 1);
    g_shutdown = 0;
    printf("PASS: test_sigint_triggers_shutdown\n");
}

static void test_sighup_triggers_flag(void) {
    g_sighup = 0;
    signal_handlers_init();

    raise(SIGHUP);

    assert(g_sighup == 1);
    g_sighup = 0;
    printf("PASS: test_sighup_triggers_flag\n");
}

static void test_sigchld_triggers_flag(void) {
    g_sigchld = 0;
    signal_handlers_init();

    raise(SIGCHLD);

    assert(g_sigchld == 1);
    g_sigchld = 0;
    printf("PASS: test_sigchld_triggers_flag\n");
}

static void test_sigpipe_ignored(void) {
    /* SIGPIPE 被设为 SIG_IGN，raise 后进程不应退出 */
    signal_handlers_init();

    /* 如果 SIGPIPE 不是 SIG_IGN，raise 会终止进程 */
    raise(SIGPIPE);
    /* 到达这里说明 SIGPIPE 被正确忽略 */
    printf("PASS: test_sigpipe_ignored\n");
}

int main(void) {
    test_handlers_init();
    test_global_flags_initial();
    test_shutdown_flag_set();
    test_sighup_flag_set();
    test_sigchld_flag_set();
    test_sigterm_triggers_shutdown();
    test_sigint_triggers_shutdown();
    test_sighup_triggers_flag();
    test_sigchld_triggers_flag();
    test_sigpipe_ignored();
    printf("All signal_handler tests passed!\n");
    return 0;
}
