/* src/daemon.c */
#include "daemon.h"

static int write_pid_file(const char *pid_file) {
    FILE *fp = fopen(pid_file, "w");
    if (fp == NULL) return -1;
    fprintf(fp, "%ld\n", (long)getpid());
    fclose(fp);
    return 0;
}

int daemonize(const char *pid_file) {
    pid_t pid;
    int fd;

    /* 第一次 fork */
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0); /* 父进程退出 */

    /* 创建新会话 */
    if (setsid() < 0) return -1;

    /* 第二次 fork (确保不是会话首进程) */
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);

    /* 切换工作目录 */
    if (chdir("/") < 0) return -1;

    /* 设置文件创建掩码 */
    umask(0);

    /* 关闭标准文件描述符 */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* 重定向到 /dev/null */
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        if (dup(fd) < 0 || dup(fd) < 0) {
            /* 忽略错误 */
        }
        if (fd > STDERR_FILENO) close(fd);
    }

    /* 写 PID 文件 */
    if (write_pid_file(pid_file) < 0) return -1;

    return 0;
}

void daemon_cleanup(const char *pid_file) {
    unlink(pid_file);
}
