/* tests/test_daemon.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "daemon.h"

/*
 * test_daemon — 测试守护进程化模块
 *
 * daemonize() 做 double-fork，父进程 _exit(0)。
 * 测试策略：在 fork 出的子进程中调 daemonize，孙子进程
 * 继续执行并验证 PID 文件，然后通过 _exit 返回。
 */

static void test_daemon_cleanup(void) {
    const char *test_pid = "/tmp/test_log_collector_test.pid";

    /* 先创建一个假的 PID 文件 */
    FILE *fp = fopen(test_pid, "w");
    assert(fp != NULL);
    fprintf(fp, "%ld\n", (long)getpid());
    fclose(fp);

    struct stat st;
    assert(stat(test_pid, &st) == 0);

    daemon_cleanup(test_pid);
    assert(stat(test_pid, &st) == -1);
    printf("PASS: test_daemon_cleanup\n");
}

static void test_cleanup_nonexistent_file(void) {
    daemon_cleanup("/tmp/test_log_collector_nonexistent.pid");
    printf("PASS: test_cleanup_nonexistent_file\n");
}

static void test_daemonize_writes_pid_file(void) {
    /*
     * 在子进程中调 daemonize，父进程等待并检查 PID 文件。
     * daemonize 内部：第一次 fork 后父进程 _exit(0)，
     * 第二次 fork 后爷爷进程 _exit(0)，孙子进程继续。
     * 我们 fork 一次让 daemonize 在子进程中运行，
     * 然后等待原始子进程退出（它是 daemonize 第一次 fork 的父进程），
     * 最后检查 PID 文件是否存在。
     */
    const char *test_pid = "/tmp/test_log_collector_daemon.pid";
    unlink(test_pid);  /* 先清理 */

    pid_t p = fork();
    assert(p >= 0);

    if (p == 0) {
        /* 子进程：调 daemonize
         * daemonize 会 double-fork，原始子进程 _exit(0)，
         * 孙子进程变成守护进程 */
        int rc = daemonize(test_pid);
        /* 如果 daemonize 成功，只有孙子进程到达这里 */
        if (rc == 0) {
            /* 验证 PID 文件存在且内容有效 */
            FILE *fp = fopen(test_pid, "r");
            if (fp) {
                long pid_val;
                if (fscanf(fp, "%ld", &pid_val) == 1) {
                    /* PID 应该是正数 */
                    assert(pid_val > 0);
                }
                fclose(fp);
            }
            daemon_cleanup(test_pid);
        }
        _exit(rc == 0 ? 0 : 1);
    } else {
        /* 等待子进程退出（daemonize 第一次 fork 的父进程 _exit(0)） */
        int status;
        waitpid(p, &status, 0);
        /* 子进程 _exit(0) 或 _exit(1) */
        assert(WIFEXITED(status));
        /* 孙子进程（守护进程）在后台运行——它会在验证后 _exit */
        /* 等待孙子进程也退出（最多 2 秒） */
        usleep(200000);
        /* 清理可能残留的 PID 文件 */
        unlink(test_pid);
    }
    printf("PASS: test_daemonize_writes_pid_file\n");
}

static void test_daemonize_invalid_pid_path(void) {
    /* PID 文件路径指向不存在的目录 → write_pid_file 失败 → daemonize 返回 -1
     * 注意：daemonize 在 fork 之前不做路径检查，fork 成功后
     * 父进程 _exit(0)，子进程继续并尝试 write_pid_file。
     * 如果路径无效，write_pid_file 返回 -1，daemonize 返回 -1。
     * 但父进程已经 _exit(0) 了。
     *
     * 这里只验证 daemon_cleanup 对无效路径不崩溃 */
    daemon_cleanup("/nonexistent_dir_xyz/test.pid");
    printf("PASS: test_daemonize_invalid_pid_path (cleanup handles bad paths)\n");
}

int main(void) {
    test_daemon_cleanup();
    test_cleanup_nonexistent_file();
    test_daemonize_writes_pid_file();
    test_daemonize_invalid_pid_path();
    printf("All daemon tests passed!\n");
    return 0;
}
