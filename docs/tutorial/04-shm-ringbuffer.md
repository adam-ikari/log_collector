# 第 4 篇：POSIX 共享内存环形缓冲区

## 回忆已学知识

- **共享内存**：`shm_open`、`mmap`、`ftruncate`、`shm_unlink`、`MAP_SHARED`
- **互斥锁**：`pthread_mutex_t`、`pthread_mutex_lock/unlock`、`PTHREAD_PROCESS_SHARED`
- **信号量**：`sem_t`、`sem_init`、`sem_wait`、`sem_post`、`sem_trywait`
- **生产者消费者模型**：线程池/进程池的设计理念

## 这次解决什么问题

Master 收到了日志数据，但解析和写磁盘是 Worker 的事。数据怎么从 Master 交到 Worker 手里？

你可以用管道（pipe），但管道要在 fork 之前创建，而且数据在内核里拷来拷去。你可以用 socket，但本地通信走网络栈太浪费。

**共享内存是最快的**：Master 写进去，Worker 直接读同一块物理内存，零拷贝。

但共享内存带来了新问题：多个进程同时读写，怎么保证不冲突？Master 写太快把还没读的数据覆盖了怎么办？Worker 没数据可读时怎么等？

这就是信号量和互斥锁要解决的问题。

## 先理解两个概念

**互斥锁（mutex）** 像一个厕所门——一次只能一个人进去，出来之前别人都得等。谁锁的谁开。

**信号量（semaphore）** 像一个停车场计数器——显示还剩多少个空位。进一辆车减一，出一辆车加一。减和加可以是不同的人。

| 维度 | 互斥锁 (mutex) | 信号量 (semaphore) |
|------|---------------|-------------------|
| 本质 | 锁，只能被一个线程/进程持有 | 计数器，可以有多个资源 |
| 操作 | lock / unlock | wait (P) / post (V) |
| 所有关系 | 必须由加锁者解锁 | 可以在不同进程中 wait 和 post |
| 用途 | 保护临界区（互斥访问） | 资源计数、生产者消费者同步 |

在我们的系统里：
- `sem_free`：还剩多少个空槽位（初始 = slot_count，全部空闲）
- `sem_used`：有多少个槽位有数据（初始 = 0，没有数据）
- `mutex`：保护 `write_pos` 和 `read_pos`，防止两个进程同时改

## POSIX 共享内存 vs 普通文件 mmap

| 特性 | shm_open | 普通文件 mmap |
|------|----------|-------------|
| 存储位置 | `/dev/shm`（tmpfs 内存文件系统） | 磁盘文件 |
| 数据持久性 | 重启后丢失 | 持久化到磁盘 |
| 性能 | 纯内存，不落盘 | 内核可能在内存不足时刷盘 |
| 语义 | 明确表示"共享内存" | 文件映射 |
| 清理 | `shm_unlink` | `unlink` |

教学项目用 `shm_open`，因为这就是讲共享内存的正确方式。

## 内存布局

```
┌──────────────────────────────────────┐
│  Header（shm_header_t）              │
│  ┌────────────────────────────────┐  │
│  │ magic | version | buffer_size  │  │
│  │ slot_size | slot_count         │  │
│  │ write_pos | read_pos           │  │
│  │ pthread_mutex_t mutex          │  │
│  │ sem_t sem_free                 │  │
│  │ sem_t sem_used                 │  │
│  └────────────────────────────────┘  │
├──────────────────────────────────────┤
│  Slot 0 (log_slot_t)                 │
│  ┌────────────────────────────────┐  │
│  │ client_addr | protocol         │  │
│  │ data_len | timestamp           │  │
│  │ data[4096]                     │  │
│  └────────────────────────────────┘  │
│  Slot 1                              │
│  ...                                 │
│  Slot 1023                           │
└──────────────────────────────────────┘
```

总大小 = `sizeof(shm_header_t) + slot_size × slot_count` ≈ 200 + 4096 × 1024 ≈ 4MB

每个槽位 = `log_slot_t` 结构体，日志正文存在 `data` 字段中。结构体的 `data[4096]` 大小与槽位大小（`CFG_SLOT_SIZE = 4096`）对齐。

## 创建共享内存

```c
int shm_init(shm_header_t **header_out, void **slots_out,
             uint64_t slot_size, uint64_t slot_count) {
    size_t total = sizeof(shm_header_t) + slot_size * slot_count;

    /* shm_open：O_EXCL 保证原子创建，已存在则先 unlink */
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) {
        shm_unlink(SHM_NAME);
        fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
        if (fd < 0) return -1;
    }

    /* 设置大小（shm_open 初始大小为 0） */
    ftruncate(fd, total);

    /* mmap 映射到进程地址空间 */
    shm_header_t *header = mmap(NULL, total,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    close(fd);  /* mmap 后可关闭 fd，映射仍有效 */
    if (header == MAP_FAILED) { shm_unlink(SHM_NAME); return -1; }

    /* 初始化头部 */
    header->magic       = SHM_MAGIC;
    header->version     = SHM_VERSION;
    header->buffer_size = total;
    header->write_pos   = 0;
    header->read_pos    = 0;

    /* 跨进程互斥锁 */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&header->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    /* 跨进程信号量 */
    if (sem_init(&header->sem_free, 1, slot_count) != 0 ||
        sem_init(&header->sem_used, 1, 0) != 0) {
        pthread_mutex_destroy(&header->mutex);
        munmap(header, total);
        shm_unlink(SHM_NAME);
        return -1;
    }

    /* 清零槽位区 */
    void *slots = (char *)header + sizeof(shm_header_t);
    memset(slots, 0, slot_size * slot_count);

    *header_out = header;
    *slots_out  = slots;
    return 0;
}
```

关键点：

**`shm_open + O_EXCL`**：原子操作——如果对象已存在就报错。防止两个 Master 实例同时初始化。

**`ftruncate`**：`shm_open` 创建的对象大小是 0。不调 `ftruncate` 的话，访问超出大小的地址会收到 **SIGBUS**（不是 SIGSEGV，是总线错误）。

**`close(fd)` 在 mmap 之后**：内核维护引用计数。mmap 增加引用，close 减少引用。只要还有 mmap 映射在，共享内存对象就不会释放。

**`PTHREAD_PROCESS_SHARED`**：普通互斥锁只能同进程的线程间共享。这个属性让它能在 fork 出的进程间共享。前提是锁本身在共享内存里。

**`sem_init(&sem, 1, N)`**：第二个参数 `1` 表示跨进程共享。第三个参数是初始值。

## 生产者写入

```c
int shm_produce(shm_header_t *header, void *slots,
                const struct sockaddr_storage *addr, uint8_t protocol,
                const char *data, uint32_t data_len, uint64_t timestamp) {

    /* 1. 问：还有空槽位吗？没有就丢弃（非阻塞） */
    if (sem_trywait(&header->sem_free) != 0) return -1;

    /* 2. 锁门 */
    pthread_mutex_lock(&header->mutex);

    /* 3. 找到当前写入位置 */
    log_slot_t *slot = get_slot(slots, header->slot_size, header->write_pos);

    /* 4. 写元数据 */
    if (addr) memcpy(&slot->client_addr, addr, sizeof(*addr));
    /* 防御：数据长度不能超过 data 数组 */
    if (data_len > sizeof(slot->data))
        data_len = sizeof(slot->data);

    slot->protocol  = protocol;
    slot->data_len  = data_len;
    slot->timestamp = timestamp;
    memcpy(slot->data, data, data_len);
    slot->data[data_len] = '\0';

    /* 6. 推进写指针（环形） */
    header->write_pos = (header->write_pos + 1) % header->slot_count;

    /* 7. 开门 */
    pthread_mutex_unlock(&header->mutex);

    /* 8. 喊：有新数据了！ */
    sem_post(&header->sem_used);

    return 0;
}
```

**为什么用 `sem_trywait`（非阻塞）？** Master 不能因为缓冲区满就卡住——网络数据还在不断进来。满了就丢弃这条日志，继续处理下一个连接。这是有损设计，但保证了系统不会因为一个慢 Worker 而整体阻塞。

**为什么还要 mutex？** 虽然目前只有一个生产者（Master），但 mutex 提供了内存屏障——`pthread_mutex_unlock` 保证之前的写入对后续的 `pthread_mutex_lock` 可见。Worker 读到的一定是完整的槽位内容。

## 消费者读取

```c
int shm_consume(shm_header_t *header, void *slots, uint64_t slot_size,
                struct sockaddr_storage *addr, uint8_t *protocol,
                char *data, uint32_t *data_len, uint64_t *timestamp) {

    /* 1. 等：有数据吗？没有就睡（阻塞） */
    sem_wait(&header->sem_used);

    /* 2. 锁门，读数据 */
    pthread_mutex_lock(&header->mutex);

    log_slot_t *slot = get_slot(slots, header->slot_size, header->read_pos);

    /* 3. 哨兵检测 */
    if (slot->data_len == 0) {
        *data_len = 0;
        header->read_pos = (header->read_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);
        sem_post(&header->sem_free);
        return 0;  /* 告诉 Worker：该退出了 */
    }

    /* 4. 读数据 */
    memcpy(addr, &slot->client_addr, sizeof(*addr));
    *protocol  = slot->protocol;
    *data_len  = slot->data_len;
    *timestamp = slot->timestamp;
    memcpy(data, slot->data, slot->data_len);
    data[slot->data_len] = '\0';

    /* 5. 推进读指针 */
    header->read_pos = (header->read_pos + 1) % header->slot_count;

    /* 6. 开门 */
    pthread_mutex_unlock(&header->mutex);

    /* 7. 喊：空出一个槽位！ */
    sem_post(&header->sem_free);

    return slot->data_len;
}
```

**为什么消费者用 `sem_wait`（阻塞）？** Worker 唯一的工作就是等数据。没数据就睡，内核不调度它，不浪费 CPU。数据来了信号量自动唤醒。

## 环形队列怎么判断满/空

传统的环形缓冲区用 `write_pos + 1 == read_pos` 判断满，浪费一个槽位。我们不需要——信号量告诉我们一切：

- `sem_free == 0` → 满
- `sem_used == 0` → 空

## 哨兵机制

关闭时 Master 向缓冲区写 N 个 `data_len=0` 的槽位（N = Worker 数量）。每个 Worker 读到一个哨兵就退出循环。这比 `kill` 信号更优雅——Worker 会先把哨兵之前的日志处理完。

```c
void shm_send_sentinels(shm_header_t *header, int count) {
    for (int i = 0; i < count; i++) {
        sem_wait(&header->sem_free);   /* 阻塞等待空槽位 */
        pthread_mutex_lock(&header->mutex);
        log_slot_t *slot = get_slot(...);
        memset(slot, 0, header->slot_size);
        slot->data_len = 0;            /* 哨兵标记 */
        header->write_pos = (header->write_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);
        sem_post(&header->sem_used);
    }
}
```

注意哨兵发送用 `sem_wait`（阻塞）而非 `sem_trywait`。因为关闭时网络监听已停止，不会有新数据进来，可以安全等待。

## Worker 怎么连接

Worker 是 fork 出来的。fork 后子进程继承了父进程的 mmap 映射，可以直接访问共享内存。但代码里保留了 `shm_connect`——如果 Worker 需要独立启动（比如重启崩溃的 Worker），它可以通过 `shm_open` 重新连接：

```c
int shm_connect(shm_header_t **header_out, void **slots_out, ...) {
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);

    /* 第一步：映射头部，读取总大小 */
    shm_header_t *header = mmap(NULL, sizeof(shm_header_t), ...);
    if (header->magic != SHM_MAGIC) { ... return -1; }  /* 魔数校验 */

    /* 第二步：用正确大小重新映射 */
    size_t total = header->buffer_size;
    munmap(header, sizeof(shm_header_t));
    header = mmap(NULL, total, ...);
    close(fd);

    *header_out = header;
    *slots_out  = (char *)header + sizeof(shm_header_t);
    return 0;
}
```

分两步 mmap 是因为 Worker 不知道共享内存的总大小——大小是 Master 根据配置决定的。第一步读头部拿到 `buffer_size`，第二步用正确大小重新映射。魔数校验防止连到错误的共享内存对象。

## 怎么验证

```bash
# 单元测试（fork 父子进程模拟 Master/Worker）
cd build && make && ./tests/test_shm_buffer

# 手动检查
./log_collector -f &
ls -la /dev/shm/log_collector_shm    # 约 4MB
hexdump -C /dev/shm/log_collector_shm | head -5
# 前 4 字节应该是 47 4F 43 4C（"LCOG" 的小端表示）
```

## 你现在应该理解的

**信号量和互斥锁各司其职**：信号量管"有没有资源"，互斥锁管"资源内部的数据别被同时改"。生产者用 `sem_trywait`（非阻塞），消费者用 `sem_wait`（阻塞）。

**POSIX 共享内存 = shm_open + mmap**：在 `/dev/shm` 下创建 tmpfs 对象，纯内存。`ftruncate` 必须在 mmap 之前调。mmap 后可以关 fd。清理用 `shm_unlink`。

**哨兵是优雅关闭的关键**：`data_len=0` 的槽位通知 Worker 退出，比信号更可靠——Worker 会处理完哨兵之前的日志再退出。

**跨进程同步需要特殊设置**：`PTHREAD_PROCESS_SHARED` 和 `sem_init(..., 1, ...)` 的第二个参数是关键。

下一篇讲 Worker 进程池——怎么 fork、怎么崩溃重启、怎么解析日志、怎么写文件。
