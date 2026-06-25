# 第 6 篇：把一切串起来

## 回忆已学知识

- **Shell 脚本**：`nc`、`ss`、`pkill`、`fuser`
- **进程基础**：fork 测试、父子进程
- **信号处理**：SIGTERM 优雅关闭、SIGKILL 强制终止

## 这次解决什么问题

所有模块都写完了。你怎么知道它们真的能工作？

不需要复杂的测试框架。用几个简单的命令，就能验证每个模块是否正常。

## 验证前的准备

确保所有源文件都已实现（前 5 篇完成后应该有的文件）：

```
log_collector/
├── CMakeLists.txt
├── include/
│   └── common.h              # 第 1 篇
├── src/
│   ├── main.c                # 第 2 篇（完整启动流程）
│   ├── config.h              # 第 1 篇
│   ├── signal_handler.c/h    # 第 2 篇
│   ├── master.c/h            # 第 3+5 篇（epoll + 进程池）
│   ├── worker.c/h            # 第 5 篇
│   ├── shm_buffer.c/h        # 第 4 篇
│   ├── log_parser.c/h        # 第 5 篇
│   └── file_writer.c/h       # 第 5 篇
└── systemd/
    └── log-collector.service # 第 2 篇
```

编译并确认可执行：

```bash
cd build && cmake .. && make
# 应该 0 错误 0 警告
```

## 验证清单

### 1. 启动与优雅关闭

```bash
$ ./log_collector &
$ pgrep log_collector
2618473
2618475
2618476
2618477
2618478
# 5 个进程（1 Master + 4 Worker）

$ kill -TERM 2618473
$ sleep 1
$ pgrep log_collector
# 无输出——所有进程已退出
```

### 2. UDP 单消息

```bash
$ ./log_collector &
$ echo "<13>UDP connection timeout" | nc -u -w1 127.0.0.1 5140
$ sleep 0.5
$ cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log
2026-06-24T08:39:31+0800 127.0.0.1 [notice] UDP connection timeout
# <13> → severity=5 → [notice] ✓
```

### 3. TCP 单消息

```bash
$ echo "<14>TCP info message" | nc -q1 127.0.0.1 5140
$ cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log
2026-06-24T08:39:31+0800 127.0.0.1 [notice] UDP connection timeout
2026-06-24T08:39:32+0800 127.0.0.1 [info] TCP info message
# TCP 行缓冲正确拆分，<14> → severity=6 → [info] ✓
```

### 4. 批量混合（TCP + UDP）

```bash
$ for i in $(seq 1 10); do
    echo "<$((13 + i % 7))>TCP batch #$i" | nc -q1 127.0.0.1 5140
done
$ for i in $(seq 1 10); do
    echo "<$((10 + i % 7))>UDP batch #$i" | nc -u -w1 127.0.0.1 5140
done
$ sleep 1
$ grep -c "batch" /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log
20
# 20 条全部收到
```

### 5. 多 IP 隔离

```bash
$ echo "<13>message from client A" | nc -u -w1 127.0.0.1 5140
$ echo "<14>message from client B" | nc -u -w1 127.0.0.1 5140
$ ls /tmp/log_collector_test/
127.0.0.1
# 按客户端 IP 分目录
```

### 6. severity 全级别

```bash
$ for pri in 0 1 2 3 4 5 6 7; do
    echo "<$pri>severity test pri=$pri" | nc -u -w1 127.0.0.1 5140
done
$ sleep 0.5
$ cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log | grep "severity test"
2026-06-24T08:39:31+0800 127.0.0.1 [emerg] severity test pri=0
2026-06-24T08:39:31+0800 127.0.0.1 [alert] severity test pri=1
2026-06-24T08:39:31+0800 127.0.0.1 [crit] severity test pri=2
2026-06-24T08:39:31+0800 127.0.0.1 [err] severity test pri=3
2026-06-24T08:39:31+0800 127.0.0.1 [warning] severity test pri=4
2026-06-24T08:39:31+0800 127.0.0.1 [notice] severity test pri=5
2026-06-24T08:39:31+0800 127.0.0.1 [info] severity test pri=6
2026-06-24T08:39:31+0800 127.0.0.1 [debug] severity test pri=7
# PRI 0-7 全部正确映射
```

### 7. 高吞吐压力

```bash
$ for i in $(seq 1 500); do
    echo "<14>stress #$i" | nc -u -w0 127.0.0.1 5140
done
$ sleep 2
$ grep -c "stress" /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log
500
# 500 条 UDP 消息全部收到
```

### 8. Worker 崩溃恢复

```bash
$ ps --forest -o pid,ppid,cmd $(pgrep log_collector | tr '\n' ' ')
    PID    PPID CMD
2618473 2618467 ./log_collector
2618475 2618473  \_ ./log_collector
2618476 2618473  \_ ./log_collector
2618477 2618473  \_ ./log_collector
2618478 2618473  \_ ./log_collector

$ kill -9 2618478   # 杀掉一个 Worker
$ sleep 2
$ pgrep -c log_collector
5
# 仍然是 5 个进程，Master 自动重启了 Worker
```

### 9. 无 PRI 默认级别

```bash
$ echo "plain text without priority" | nc -u -w1 127.0.0.1 5140
$ sleep 0.5
$ cat /tmp/log_collector_test/127.0.0.1/$(date +%Y-%m-%d).log | grep "plain text"
2026-06-24T08:39:31+0800 127.0.0.1 [debug] plain text without priority
# 无 PRI → 默认 [debug] ✓
```

## 验证完成后的清理

```bash
$ kill -TERM $(pgrep log_collector | head -1)
$ sleep 1
$ rm -rf /tmp/log_collector_test /dev/shm/log_collector_shm
```

## 调试备忘

遇到问题时，几个有用的命令：

```bash
# 进程树（Master → Worker 层级关系）
ps --forest -o pid,ppid,cmd $(pgrep log_collector)

# 文件描述符分配
ls -l /proc/$(pgrep log_collector | head -1)/fd

# 共享内存
ls -la /dev/shm/log_collector_shm
xxd /dev/shm/log_collector_shm | head -20
```

### 常见问题排查

**程序启动后立即退出**：前台运行看错误信息 `./log_collector`。检查 `/dev/shm` 是否挂载：

```bash
mount | grep /dev/shm
# 如果没有：sudo mkdir -p /dev/shm && sudo mount -t tmpfs tmpfs /dev/shm
```

**Worker 不断重启**：日志目录可能有问题。检查 syslog 输出：

```bash
tail -f /var/log/syslog | grep log_collector
```

**日志丢失**：可能原因——共享内存满（增大 `CFG_SLOT_COUNT`）、Worker 处理太慢（增大 `CFG_WORKER_COUNT`）、UDP 内核队列满：

```bash
netstat -su | grep "receive errors"
```

## 知识图谱

这个项目用到的每个知识点，汇总在一起：

```
fork/waitpid    ──→ Worker 进程池的创建和收割
信号处理        ──→ 优雅关闭（g_shutdown 标志 + siginterrupt）
POSIX 共享内存  ──→ shm_open + mmap 创建环形缓冲区
互斥锁          ──→ PTHREAD_PROCESS_SHARED 保护 write_pos/read_pos
信号量          ──→ sem_free/sem_used 实现生产者消费者
epoll           ──→ EPOLLET 边缘触发管理 TCP/UDP + 客户端连接
Socket          ──→ TCP/UDP 监听，接收网络日志
文件 I/O        ──→ 按 IP+日期 分目录写日志文件
进程池          ──→ Worker 创建/崩溃重启/优雅关闭的完整模式
systemd 服务    ──→ Type=simple 前台运行，sd_journal_print 日志
CMake           ──→ 构建系统
```

这些知识点拼成了一个完整的、能跑在生产环境中的日志收集系统。每篇教程都对应源文件中的一个模块——教程告诉你"为什么这么写"，源代码告诉你"具体怎么写"。对着看，效果最好。

## 后续学习方向

完成了这个项目，你已经掌握了 Linux 系统编程的核心技能。接下来可以深入的方向：

1. **I/O 多路复用**：epoll 的更多高级用法（EPOLLONESHOT、timerfd、signalfd）
2. **io_uring**：Linux 5.1+ 的新异步 I/O 接口，比 epoll 更快
3. **内核开发**：内核模块、驱动开发
4. **分布式系统**：多机通信、Raft/Paxos 一致性协议
