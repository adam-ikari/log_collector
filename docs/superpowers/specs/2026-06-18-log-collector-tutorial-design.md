# Log Collector 教学教程设计文档

## 概述

基于 `log_collector` 项目（C99 Linux 网络日志收集系统），制作一套教学教程。读者已具备 C 语言和 Linux 编程基础，教程侧重架构设计和实际用法，按项目开发顺序逐步构建。

## 教程形式

- **6 篇系列文章（Markdown）**：按开发顺序，从零开始搭建项目
- **增强代码注释**：关键函数和数据结构增加教学性中文注释
- **README_TUTORIAL.md**：项目级别教学 README，含架构图和快速开始

## 叙事方式

每篇是一次开发迭代，结构为：
1. 这一步要解决什么问题
2. 怎么设计的（架构决策）
3. 关键代码怎么写
4. 怎么验证

## 文章目录

### 第 1 篇：搭骨架 — 项目结构、CMake、配置系统

**对应源文件：** CMakeLists.txt, include/common.h, src/config.c

**内容要点：**
- CMakeLists.txt：C99 标准、源文件列表、链接 pthread 和 rt
- common.h：所有类型定义、常量集中管理（config_t、shm_header_t、log_slot_t）
- config_t 结构体设计：listen_addr、tcp_port、udp_port、max_connections、worker_count、slot_size、slot_count、log_dir
- INI 配置解析：默认值 + 文件覆盖、trim_whitespace、key=value 解析、跳过注释和节头
- 编译运行，验证配置加载正确

### 第 2 篇：变成守护进程 — daemonize + 信号处理

**对应源文件：** src/daemon.c, src/signal_handler.c, src/main.c

**内容要点：**
- 守护进程是什么、为什么需要
- double-fork + setsid 的完整流程：fork→父进程退出→setsid→fork→子进程退出→chdir("/")→umask(0)→关闭标准 fd→重定向 /dev/null→写 PID 文件
- 信号处理设计：SIGTERM/SIGINT 触发优雅关闭、SIGCHLD 收割子进程、SIGHUP 重载配置、SIGPIPE 忽略
- sigaction 注册信号处理函数，volatile sig_atomic_t 全局标志
- main.c 串联：解析命令行参数（-f 前台、-c 配置路径、-h 帮助）→ 加载配置 → daemonize → 信号初始化 → 共享内存初始化 → master_run

### 第 3 篇：接收网络日志 — epoll + TCP/UDP

**对应源文件：** src/master.c（网络 I/O 部分）

**内容要点：**
- 创建 TCP listener：socket + SO_REUSEADDR + 非阻塞 + bind + listen
- 创建 UDP listener：socket + 非阻塞 + bind
- epoll 边缘触发（EPOLLET）事件循环：epoll_create1 → epoll_ctl 注册 TCP/UDP listener → epoll_wait 主循环
- TCP accept 循环：accept4 + SOCK_NONBLOCK，客户端 fd 注册到 epoll（EPOLLIN | EPOLLRDHUP | EPOLLET）
- TCP 数据读取：边缘触发循环 read 直到 EAGAIN，行缓冲提取完整消息（extract_line + memmove）
- UDP 数据读取：边缘触发循环 recvfrom 直到 EAGAIN，每个数据报作为一条独立消息
- 收到数据后暂时打印到 stdout（下一篇才写入共享内存）

### 第 4 篇：共享内存环形缓冲区 — mmap + 信号量 + 互斥锁

**对应源文件：** src/shm_buffer.c

**内容要点：**
- 为什么选 mmap 文件后备而非 shm_open：兼容无 /dev/shm 的环境
- 内存布局设计：Header（magic、version、buffer_size、slot_size、slot_count、write_pos、read_pos、mutex、sem_free、sem_used）+ Slot 数组（固定大小槽位，每个槽位包含 client_addr、protocol、data_len、timestamp、data）
- 环形队列：write_pos / read_pos 两个指针，模运算实现循环
- 生产者消费者模型：sem_free（空闲槽位计数，初始值=slot_count）、sem_used（已用槽位计数，初始值=0）
- 跨进程互斥锁：pthread_mutexattr_setpshared + PTHREAD_PROCESS_SHARED
- POSIX 信号量：sem_init 嵌入共享内存（pshared=1）
- shm_produce：sem_trywait(sem_free) 非阻塞 → lock → 写入 slot[write_pos] → write_pos++ → unlock → sem_post(sem_used)
- shm_consume：sem_wait(sem_used) 阻塞等待 → lock → 读取 slot[read_pos] → read_pos++ → unlock → sem_post(sem_free)
- 哨兵机制：data_len=0 通知 Worker 退出
- shm_connect：Worker 通过 mmap 同一文件连接到已有共享内存

### 第 5 篇：进程池与 Worker — fork + 日志写入

**对应源文件：** src/master.c（进程管理部分）、src/worker.c、src/log_parser.c、src/file_writer.c

**内容要点：**
- Master fork Worker 进程池：fork_workers 循环 fork，子进程重置信号处理、调用 worker_run
- Worker 主流程：shm_connect → file_writer_init → 消费循环（shm_consume → log_parser_format → file_writer_write）
- SIGCHLD 处理：reap_workers 用 waitpid WNOHANG 收割退出的 Worker，自动 fork 新 Worker 补充
- 优雅关闭流程：Master 收到 SIGTERM → 关闭监听 socket → shm_send_sentinels 发送 N 个哨兵 → wait_workers 等待 Worker 退出（超时 10 秒后 SIGKILL）→ 清理共享内存
- 日志解析（log_parser.c）：提取 <PRI> 前缀、severity 映射（0=emerg 到 7=debug）、时间戳格式化、IP 地址提取
- 文件存储（file_writer.c）：按客户端 IP 分目录、按日期分文件（YYYY-MM-DD.log）、跨天自动切换、递归创建目录

### 第 6 篇：串联测试与调试

**内容要点：**
- 单元测试：test_config（默认值、解析、注释）、test_shm_buffer（初始化、fork 跨进程生产消费、哨兵）、test_log_parser（基本 syslog、无 PRI、IPv6）、test_file_writer（写入、IP 切换）
- E2E 测试：TCP/UDP 单消息、批量混合、多 IP 隔离、severity 全级别、高吞吐压力、Worker 崩溃恢复、无 PRI 默认级别
- 踩坑记录：EPOLLRDHUP 必须在 EPOLLIN 之后处理（否则丢数据）、目录需要递归创建（mkdir 不自动创建父目录）、pkill 正则需精确匹配避免误杀测试脚本、TCP 端口残留需 fuser 清理

## 输出文件

| 文件 | 说明 |
|------|------|
| `docs/tutorial/01-project-setup.md` | 第 1 篇：搭骨架 |
| `docs/tutorial/02-daemon-and-signals.md` | 第 2 篇：守护进程 |
| `docs/tutorial/03-epoll-network.md` | 第 3 篇：网络 I/O |
| `docs/tutorial/04-shm-ringbuffer.md` | 第 4 篇：共享内存 |
| `docs/tutorial/05-process-pool.md` | 第 5 篇：进程池 |
| `docs/tutorial/06-testing-and-debugging.md` | 第 6 篇：测试调试 |
| `README_TUTORIAL.md` | 项目教学 README |
| 各源文件注释增强 | 教学性中文注释 |

## 不包含的内容

- 柔性数组（data[]）的讲解
- 每篇文章不设"踩坑"独立章节
- 不涉及内核原理深度剖析
