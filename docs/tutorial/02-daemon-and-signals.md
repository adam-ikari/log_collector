# 第 2 篇：变成守护进程 + 信号处理

## 回忆已学知识

- **进程基础**：`fork` 创建子进程、`_exit` 直接退出、孤儿进程被 init 收养
- **信号处理**：`signal` 注册 handler、`volatile sig_atomic_t` 安全标志、`SIG_IGN` 忽略信号
- **守护进程**：double-fork、`setsid`、`chdir`、`umask`、关闭标准 fd

## 这次解决什么问题

你的程序在终端里跑得好好的，但一关终端就没了。为什么？

因为你的进程"挂"在终端下面。终端退出时，内核会给终端下的所有进程发 SIGHUP 信号，默认行为是终止进程。守护进程（daemon）的目的就是把自己从终端里"摘"出来，变成一个独立的、不受终端影响的进程。

你可能会想：那用 `nohup ./program &` 不就行了？行，但那不是你自己的代码在管理生命周期。学会守护进程化，你就理解了 Linux 进程管理的核心概念——会话、进程组、控制终端。

## 守护进程化：double-fork 的每一步

```c
int daemonize(const char *pid_file) {
    pid_t pid;

    /* 第 1 次 fork：脱离 shell */
    if ((pid = fork()) < 0) return -1;
    if (pid > 0) _exit(0);    /* 父进程退出，shell 以为命令结束了 */

    /* 创建新会话，脱离原控制终端 */
    if (setsid() < 0) return -1;

    /* 第 2 次 fork：确保不是会话首进程 */
    if ((pid = fork()) < 0) return -1;
    if (pid > 0) _exit(0);    /* 爷爷进程退出 */

    /* 切到根目录（避免占用挂载点） */
    chdir("/");

    /* 重置文件创建掩码 */
    umask(0);

    /* 关掉 stdin/stdout/stderr，重定向到 /dev/null */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup(fd); dup(fd);    /* fd 0→1, fd 0→2 */
        if (fd > STDERR_FILENO) close(fd);
    }

    /* 写 PID 文件 */
    return write_pid_file(pid_file);
}
```

### 为什么 fork 两次？

这是经典面试题。每一步都有明确目的：

**第 1 次 fork**：父进程退出，shell 收到通知"命令结束了"。子进程变成孤儿，被 init（PID=1）收养。然后子进程调用 `setsid()` 创建新会话。

但 `setsid` 有一个前提：**调用者不能是进程组 leader**。第一次 fork 恰好保证了子进程不是 leader（leader 是父进程）。

**第 2 次 fork**：会话首进程**可以**重新获得控制终端——如果它不小心打开了一个 tty 设备的话。第二次 fork 后，爷爷进程退出，孙子进程不是会话首进程，**永远**无法获得控制终端。这是防御性编程。

### 其他步骤的目的

**`chdir("/")`**：如果守护进程的工作目录在 `/mnt/usb`，`umount` 时会报 "device is busy"。切到根目录避免占用挂载点。

**`umask(0)`**：父进程可能设置了 `umask(022)`（新建文件不给组和其他人写权限）。守护进程应该自己用 `open(..., 0644)` 控制权限，不受父进程影响。

**关闭 stdin/stdout/stderr**：如果某个库函数不小心 `printf` 到了已关闭的 fd，新打开的 socket 可能恰好分配到 fd=1，日志就写到网络连接里去了。重定向到 `/dev/null` 是为了占住这三个 fd。

**`_exit(0)` vs `exit(0)`**：`_exit` 直接进内核，不刷新 stdio 缓冲区、不调 atexit 注册的函数。fork 后如果子进程用 `exit`，可能把父进程缓冲区的数据也刷出去，日志就写重了。

**`dup(fd)` 的巧妙之处**：`open("/dev/null")` 返回最小可用 fd（通常是 0），然后 `dup(0)` 返回 1，再 `dup(0)` 返回 2。最终 fd 0/1/2 都指向 `/dev/null`。

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

| 信号 | 默认行为 | 我们的处理 | 原因 |
|------|---------|-----------|------|
| SIGTERM | 终止进程 | 设置 g_shutdown=1 | 优雅关闭，先通知 Worker 退出再清理资源 |
| SIGINT | 终止进程 | 设置 g_shutdown=1 | 前台运行时 Ctrl+C 也能优雅退出 |
| SIGHUP | 终止进程 | 设置 g_sighup=1 | 终端断开不退出（守护进程无终端），改为重载配置 |
| SIGCHLD | 忽略 | 设置 g_sigchld=1 | 子进程退出时收割，防止僵尸进程 |
| SIGPIPE | 终止进程 | SIG_IGN 忽略 | 写已关闭的 socket 返回 EPIPE，不应导致进程退出 |

**为什么忽略 SIGPIPE？** 当向已关闭的 TCP 连接写入数据时，内核发送 SIGPIPE。默认行为是终止进程。对于网络服务器，这应该是可恢复的错误——忽略 SIGPIPE，让 `write()` 返回 -1 并设置 `errno=EPIPE`，由应用层处理。

## 怎么验证

```bash
# 前台运行，Ctrl+C 测试信号响应
./log_collector -f
# 按 Ctrl+C → 程序应该退出（SIGINT → g_shutdown=1 → 主循环结束）

# 后台运行，检查守护进程化
./log_collector
echo $?                          # 应该输出 0（父进程已退出）
ps aux | grep log_collector      # PPID 应该是 1（被 init 收养）
cat /var/run/log-collector.pid   # 应该有 PID

# 检查标准 fd 重定向
ls -l /proc/$(pgrep log_collector)/fd
# 应该看到 0→/dev/null, 1→/dev/null, 2→/dev/null

# 测试 SIGTERM 优雅关闭
kill -TERM $(pgrep log_collector)
sleep 1
pgrep log_collector              # 应该没有进程了
```

## 你现在应该理解的

**double-fork 的每一步都有目的**：不是随便 fork 两次就完事。第一次为了让 setsid 成功，第二次为了防止获得控制终端。chdir("/")、umask(0)、关闭 fd——每一步都在解决一个具体的问题。

**信号处理只要设标志**：别在 handler 里干复杂的事。设完标志马上返回，让主循环去处理。`volatile sig_atomic_t` 保证了这一点。

**Linux 上 signal() 带 SA_RESTART**：这是 `signal()` vs `sigaction()` 最大的陷阱。`siginterrupt()` 能解决。

## systemd 服务配置

守护进程写好了，怎么让它开机自启、崩溃重启？在现代 Linux 上，答案是 systemd。

我们写一个 service 文件，告诉 systemd 怎么管这个进程：

```ini
# systemd/log-collector.service
[Unit]
Description=Log Collector
After=network.target

[Service]
Type=forking
PIDFile=/var/run/log-collector.pid
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

**`After=network.target`**：等网络就绪后再启动。程序要 bind 端口，网络没起来会失败。

**`Type=forking`**：关键配置。systemd 默认 `Type=simple`（认为服务进程就是它启动的那个进程）。但我们的 `daemonize()` 做了 double-fork——父进程 `_exit(0)` 退出，真正的守护进程是孙子进程。`Type=forking` 告诉 systemd：初始进程会 fork 然后退出，通过 PIDFile 追踪真正的守护进程。

**`PIDFile=/var/run/log-collector.pid`**：和 `daemon.c` 里 `write_pid_file()` 写的路径一致。systemd 读取这个文件确认守护进程的 PID。

**`ExecStart=/usr/local/sbin/log_collector`**：不传 `-f`，让程序走 double-fork。systemd 等父进程退出后从 PID 文件获取真正 PID。

**`ExecReload=/bin/kill -HUP $MAINPID`**：`systemctl reload log-collector` 时执行。`$MAINPID` 是 systemd 自动替换的变量。SIGHUP 触发 `handle_sighup` → `g_sighup = 1`，主循环检测到后做配置热重载（当前是 TODO，预留了扩展点）。

**`Restart=on-failure`**：异常退出时自动重启。注意是 `on-failure` 不是 `always`——正常 `exit(0)`（收到 SIGTERM 走优雅关闭）不会重启。只有 crash、信号崩溃、返回非 0 时才重启。

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

### 前台模式和守护进程模式的分工

| 模式 | 怎么跑 | 日志去哪 |
|------|--------|---------|
| `-f` 前台 | `./log_collector -f` | stderr → 终端 |
| 守护进程 | `./log_collector` 或 systemd | `sd_journal_print` → journald |

前台模式用于开发和调试（直接看终端输出），守护进程模式用于生产（systemd 管理生命周期，journald 收集日志）。两个模式各司其职。

下一篇用 epoll + TCP/UDP 接收网络日志。
