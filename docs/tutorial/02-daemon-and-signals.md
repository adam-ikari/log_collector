# 第 2 篇：信号处理 + systemd 服务

## 回忆已学知识

- **信号处理**：`signal` 注册 handler、`volatile sig_atomic_t` 安全标志、`SIG_IGN` 忽略信号
- **守护进程**：double-fork、`setsid`、会话与控制终端的关系
- **systemd**：`Type=simple` vs `Type=forking`、服务单元文件

## 这次解决什么问题

程序跑起来了，但有两个问题：

1. 关掉终端程序就没了——需要 systemd 管理生命周期
2. `kill` 程序时直接死掉——需要信号处理实现优雅关闭

现代 Linux 上，守护进程化由 systemd 负责，程序本身只写业务逻辑。`Type=simple` 是最简单的方式：systemd 直接启动程序，程序就是服务进程，不需要自己 fork。

## 信号处理：最小化原则

信号处理函数里能干什么？POSIX 白名单很短：`write`、`_exit`、`signal`、`sem_post` 等可以。不能干的事：`printf`、`malloc`、`fopen`——都不是异步信号安全的。

所以信号处理的最佳实践极其简单：**只设一个全局标志，然后马上返回**。主循环负责检查标志，做真正的处理。

```c
volatile sig_atomic_t g_shutdown = 0;  /* SIGTERM / SIGINT */
volatile sig_atomic_t g_sighup   = 0;  /* SIGHUP */
volatile sig_atomic_t g_sigchld  = 0;  /* SIGCHLD */

static void handle_sigterm(int sig) { (void)sig; g_shutdown = 1; }
static void handle_sighup(int sig)  { (void)sig; g_sighup   = 1; }
static void handle_sigchld(int sig) { (void)sig; g_sigchld  = 1; }
```

**`volatile`** 告诉编译器：这个变量的值随时可能被改变（被信号处理函数），别把它优化到寄存器里。每次读取都从内存加载。

**`sig_atomic_t`** 是平台相关的整数类型，保证读写是原子的（单条 CPU 指令完成）。在 x86 上通常是 `int`。

## 注册信号

```c
int signal_handlers_init(void) {
    /* SIGTERM / SIGINT → 优雅关闭 */
    if (signal(SIGTERM, handle_sigterm) == SIG_ERR ||
        signal(SIGINT,  handle_sigterm) == SIG_ERR) return -1;
    siginterrupt(SIGTERM, 1);  /* 关掉 SA_RESTART */
    siginterrupt(SIGINT,  1);

    /* SIGHUP → 重载配置 */
    if (signal(SIGHUP,  handle_sighup) == SIG_ERR  ||
        signal(SIGCHLD, handle_sigchld) == SIG_ERR ||
        signal(SIGPIPE, SIG_IGN)         == SIG_ERR) return -1;
    siginterrupt(SIGHUP,  1);
    siginterrupt(SIGCHLD, 1);

    return 0;
}
```

### signal() vs sigaction()

`sigaction` 是 POSIX 标准推荐的信号注册方式，行为在所有 UNIX 上一致。这里用 `signal()` 是因为本项目只跑 Linux——在 Linux 上 `signal()` 就是 `sigaction` 的包装，行为是确定的（BSD 语义：handler 保持注册，不会一次后就重置）。

但有一个坑：Linux 的 `signal()` 默认带 **SA_RESTART**——被信号中断的慢系统调用（比如 `epoll_wait`、`read`）会自动重启，而不是返回 -1 并设置 `errno=EINTR`。

这对 `epoll_wait` 来说是致命的：

```
信号来了 → handler 执行 → g_shutdown = 1
         → epoll_wait 被中断
         → SA_RESTART 让它自动重启
         → 继续等事件
         → while (!g_shutdown) 永远检查不到
```

解决办法：**`siginterrupt(sig, 1)`** 关掉 SA_RESTART。这样信号到来时 `epoll_wait` 返回 -1（errno=EINTR），主循环就能检查 `g_shutdown` 然后退出。

### 各信号的设计决策

| 信号    | 默认行为 | 我们的处理        | 原因                                           |
| ------- | -------- | ----------------- | ---------------------------------------------- |
| SIGTERM | 终止进程 | 设置 g_shutdown=1 | 优雅关闭，先通知 Worker 退出再清理资源         |
| SIGINT  | 终止进程 | 设置 g_shutdown=1 | 前台运行时 Ctrl+C 也能优雅退出                 |
| SIGHUP  | 终止进程 | 设置 g_sighup=1   | systemd `ExecReload` 发 HUP，用于重载配置      |
| SIGCHLD | 忽略     | 设置 g_sigchld=1  | 子进程退出时收割，防止僵尸进程                 |
| SIGPIPE | 终止进程 | SIG_IGN 忽略      | 写已关闭的 socket 返回 EPIPE，不应导致进程退出 |

**为什么忽略 SIGPIPE？** 当向已关闭的 TCP 连接写入数据时，内核发送 SIGPIPE。默认行为是终止进程。对于网络服务器，这应该是可恢复的错误——忽略 SIGPIPE，让 `write()` 返回 -1 并设置 `errno=EPIPE`，由应用层处理。

## systemd 服务配置

程序本身不做守护进程化——systemd 用 `Type=simple` 直接管理：

```ini
# systemd/log-collector.service
[Unit]
Description=Log Collector
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/sbin/log_collector
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

逐行解释：

**`Type=simple`**：systemd 默认类型。systemd 认为 `ExecStart` 启动的进程就是服务进程，不需要 fork。程序退出，systemd 就知道服务停了。对比 `Type=forking`：传统守护进程自己 double-fork，systemd 需要从 PIDFile 追踪真正的进程——多了两个步骤，还容易出错。

**`After=network.target`**：等网络就绪后再启动。程序要 bind 端口。

**`ExecStart`**：直接启动，不需要 `-f` 参数——程序始终前台运行，守护进程化由 systemd 负责。

**`ExecReload=/bin/kill -HUP $MAINPID`**：`systemctl reload log-collector` 时执行。`$MAINPID` 是 systemd 自动替换的变量。SIGHUP 触发 `handle_sighup` → `g_sighup = 1`。

**`Restart=on-failure`**：异常退出时自动重启。正常 `exit(0)`（收到 SIGTERM 走优雅关闭）不会重启。只有 crash、信号崩溃、返回非 0 时才重启。

**`RestartSec=5`**：重启前等 5 秒。防止进程因配置错误反复 crash 重启。

**`StandardOutput=journal` / `StandardError=journal`**：stdout 和 stderr 自动进入 systemd journal。即使代码不用 `sd_journal_print()`，普通的 `fprintf(stderr, ...)` 也会被 systemd 捕获。

### 安装和使用

```bash
# cmake 自动安装 service 文件
sudo cmake --install build

# 或手动安装
sudo cp systemd/log-collector.service /etc/systemd/system/

# 重载 systemd 配置
sudo systemctl daemon-reload

# 开机自启 + 立即启动
sudo systemctl enable --now log-collector

# 查看状态
systemctl status log-collector

# 实时日志
journalctl -u log-collector -f

# 重载配置
sudo systemctl reload log-collector
```

### sd_journal_print

`main.c` 里的错误信息用 `sd_journal_print(LOG_ERR, ...)` 而非 `fprintf(stderr, ...)`：

```c
#include <systemd/sd-journal.h>

if (shm_init(&shm_header, &slots, cfg.slot_size, cfg.slot_count) < 0) {
    sd_journal_print(LOG_ERR, "共享内存初始化失败");
    ...
}
```

和 `syslog()` 的区别：`syslog()` 发给传统的 syslog 守护进程（rsyslog/syslog-ng），`sd_journal_print()` 直接写给 systemd journal。在 systemd 系统上后者更直接——不需要中间转发。而且 journal 自带结构化字段：`CODE_FILE`（哪个源文件）、`CODE_LINE`（哪一行）、`PRIORITY`（日志级别），不需要自己在消息里编码这些信息。

CMakeLists 里新增了对应的依赖：

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

target_link_libraries(log_collector PRIVATE ${SYSTEMD_LIBRARIES})
target_include_directories(log_collector PRIVATE ${SYSTEMD_INCLUDE_DIRS})
```

## 怎么验证

```bash
$ ./log_collector &
$ pgrep log_collector
2631409
2631411
2631412
2631413
2631414
# 5 个进程（1 Master + 4 Worker）

# 测试 SIGTERM 优雅关闭
$ kill -TERM 2631409
$ sleep 1
$ pgrep log_collector
# 无输出——所有进程已退出
```

## 你现在应该理解的

**信号处理只要设标志**：别在 handler 里干复杂的事。`volatile sig_atomic_t` 保证了这一点。

**Linux 上 signal() 带 SA_RESTART**：`siginterrupt()` 能解决。

**systemd Type=simple**：程序只写业务逻辑，守护进程化交给 systemd。不需要 double-fork，不需要 PID 文件。

下一篇用 epoll + TCP/UDP 接收网络日志。
