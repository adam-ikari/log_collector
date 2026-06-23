/* src/daemon.c — 守护进程化：double-fork + setsid */
#include "daemon.h"

static int write_pid_file(const char *pid_file) {
    FILE *fp = fopen(pid_file, "w");
    if (!fp) return -1;
    fprintf(fp, "%ld\n", (long)getpid());
    fclose(fp);
    return 0;
}

int daemonize(const char *pid_file) {
    pid_t pid;

    /* 第 1 次 fork：脱离 shell */
    if ((pid = fork()) < 0) return -1;
    if (pid > 0) _exit(0);

    /* 创建新会话，脱离控制终端 */
    if (setsid() < 0) return -1;

    /* 第 2 次 fork：确保不是会话首进程，无法重新获得控制终端 */
    if ((pid = fork()) < 0) return -1;
    if (pid > 0) _exit(0);

    chdir("/");
    umask(0);

    /* 关闭标准 fd，重定向到 /dev/null */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup(fd); dup(fd);
        if (fd > STDERR_FILENO) close(fd);
    }

    return write_pid_file(pid_file);
}

void daemon_cleanup(const char *pid_file) {
    unlink(pid_file);
}
