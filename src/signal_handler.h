/* src/signal_handler.h */
#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 全局标志 (定义在 signal_handler.c) */
extern volatile sig_atomic_t g_shutdown;
extern volatile sig_atomic_t g_sighup;
extern volatile sig_atomic_t g_sigchld;

/* 注册所有信号处理 */
int signal_handlers_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SIGNAL_HANDLER_H */
