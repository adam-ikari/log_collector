/* src/signal_handler.h */
#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include "common.h"

/* 全局标志 (定义在 signal_handler.c) */
extern volatile sig_atomic_t g_shutdown;
extern volatile sig_atomic_t g_sighup;
extern volatile sig_atomic_t g_sigchld;

/* 注册所有信号处理 */
int signal_handlers_init(void);

/* 阻塞等待信号 (用于信号主循环，如 SIGTERM) */
void signal_wait_for_shutdown(void);

#endif /* SIGNAL_HANDLER_H */
