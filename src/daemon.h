/* src/daemon.h */
#ifndef DAEMON_H
#define DAEMON_H

#include "common.h"

/* 将当前进程变为守护进程。成功时不返回(父进程已退出), 失败返回 -1。 */
int daemonize(const char *pid_file);

/* 删除 PID 文件 */
void daemon_cleanup(const char *pid_file);

#endif /* DAEMON_H */
