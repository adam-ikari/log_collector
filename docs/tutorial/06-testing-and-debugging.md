# 第 6 篇：把一切串起来

## 回忆已学知识

- **Shell 脚本**：`nc`、`ss`、`pkill`、`fuser`
- **进程基础**：fork 测试、父子进程
- **信号处理**：SIGTERM 优雅关闭、SIGKILL 强制终止

## 这次解决什么问题

所有模块都写完了。你怎么知道它们真的能工作？你怎么知道改了一行代码后没有搞坏别的功能？

这才是实战项目最关键的环节——不是"写完就行"，而是"写对了并且能证明它对了"。

## 测试策略：为什么只用 E2E

这个项目的核心逻辑涉及多进程协作——Master fork Worker、共享内存通信、epoll 网络 I/O。这些模块紧密耦合，单独测试某个函数意义不大：你测了 `shm_produce` 能写入数据，不代表 Worker 能正确消费；你测了 `log_parser_format` 能解析 `<PRI>`，不代表 TCP 行缓冲能正确拆分消息。

所以测试策略很直接：**把程序跑起来，用 `nc` 模拟真实客户端发数据，检查磁盘上的日志文件**。这就是端到端（E2E）测试。

## E2E 测试：9 个场景

`tests/e2e_test.sh` 用 `nc` 模拟真实客户端，验证完整数据流：

| # | 场景 | 验证什么 |
|---|------|---------|
| 1 | 启动与优雅关闭 | 进程起来了，SIGTERM 后退出 |
| 2 | UDP 单消息 | 日志内容、severity 解析、IP 记录 |
| 3 | TCP 单消息 | TCP 连接正常，行缓冲正确拆分 |
| 4 | 批量混合 | 10 TCP + 10 UDP，20 条全收到 |
| 5 | 多 IP 隔离 | 目录结构按 IP 分离 |
| 6 | severity 全级别 | PRI 0-7 全部正确映射 |
| 7 | 高吞吐压力 | 1000 条消息，接收率 ≥ 90% |
| 8 | Worker 崩溃恢复 | kill -9 Worker 后自动重启 |
| 9 | 无 PRI 默认级别 | 普通文本默认 debug |

### 压力测试的宽松判断

压力测试中要求 ≥ 90% 而非 100%，原因：
- UDP 天然不可靠，短期高频发送可能超过内核缓冲区
- 网络栈的队列深度有限制
- 测试环境可能负载波动

单元测试要求 100% 确定性，E2E 测试要有合理的容忍度。

### 关键测试技巧

**端口就绪检测**：TCP 测试前必须确认端口已监听，避免竞态：
```bash
for i in $(seq 1 10); do
    if ss -tlnp 2>/dev/null | grep -q 5140; then break; fi
    sleep 0.1
done
```

**进程清理**：测试前后彻底清理残留进程和端口占用：
```bash
pkill -9 -f "(^|/)log_collector[[:space:]]" 2>/dev/null || true
fuser -k 5140/tcp 2>/dev/null || true
fuser -k 5140/udp 2>/dev/null || true
```

**优雅关闭等待**：`kill -TERM` 后轮询进程是否退出，超时则 `kill -9`：
```bash
kill -TERM $COLLECTOR_PID
for i in $(seq 1 50); do
    if ! kill -0 $COLLECTOR_PID 2>/dev/null; then break; fi
    sleep 0.1
done
kill -9 $COLLECTOR_PID 2>/dev/null || true  # 超时兜底
```

## 怎么跑测试

```bash
# E2E 测试（约 1 分钟）
cd log_collector
bash tests/e2e_test.sh
```

期望结果：9 个场景 24 项断言全部 PASS。

```bash
============================================
  Results: 24 passed, 0 failed
============================================
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
hexdump -C /dev/shm/log_collector_shm | head -20

# 追踪系统调用
strace -f -e epoll_wait,accept,recvfrom ./log_collector -f
strace -f -e fork,waitpid,kill ./log_collector -f
strace -f -e shm_open,shm_unlink,mmap,munmap ./log_collector -f
```

### 常见问题排查

**程序启动后立即退出**：前台运行看错误信息 `./log_collector -f`。检查 `/dev/shm` 是否挂载：
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
守护进程        ──→ double-fork + setsid + PID 文件
CMake           ──→ 构建系统 + CTest 测试集成
```

这些知识点拼成了一个完整的、能跑在生产环境中的日志收集系统。每篇教程都对应源文件中的一个模块——教程告诉你"为什么这么写"，源代码告诉你"具体怎么写"。对着看，效果最好。

## 后续学习方向

完成了这个项目，你已经掌握了 Linux 系统编程的核心技能。接下来可以深入的方向：

1. **I/O 多路复用**：epoll 的更多高级用法（EPOLLONESHOT、timerfd、signalfd）
2. **io_uring**：Linux 5.1+ 的新异步 I/O 接口，比 epoll 更快
3. **内核开发**：内核模块、驱动开发
4. **分布式系统**：多机通信、Raft/Paxos 一致性协议
