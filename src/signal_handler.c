/* src/signal_handler.c */
#include "signal_handler.h"

volatile sig_atomic_t g_shutdown = 0;
volatile sig_atomic_t g_sighup = 0;
volatile sig_atomic_t g_sigchld = 0;

static void handle_sigterm(int sig) {
    (void)sig;
    g_shutdown = 1;
}

static void handle_sighup(int sig) {
    (void)sig;
    g_sighup = 1;
}

static void handle_sigchld(int sig) {
    (void)sig;
    g_sigchld = 1;
}

int signal_handlers_init(void) {
    struct sigaction sa;

    /* SIGTERM / SIGINT */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) < 0) return -1;
    if (sigaction(SIGINT, &sa, NULL) < 0) return -1;

    /* SIGHUP */
    sa.sa_handler = handle_sighup;
    if (sigaction(SIGHUP, &sa, NULL) < 0) return -1;

    /* SIGCHLD */
    sa.sa_handler = handle_sigchld;
    sa.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) return -1;

    /* SIGPIPE: 忽略 */
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, NULL) < 0) return -1;

    return 0;
}

void signal_wait_for_shutdown(void) {
    sigset_t mask;
    sigemptyset(&mask);
    while (!g_shutdown) {
        sigsuspend(&mask);
    }
}
