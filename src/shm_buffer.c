/* src/shm_buffer.c */
#include "shm_buffer.h"

/*
 * shm_buffer.c — POSIX 共享内存环形缓冲区
 *
 * 基于 shm_open(3) + mmap(2) 的进程间通信机制，实现单生产者（Master）
 * 多消费者（Worker）模式。
 *
 * 核心设计：
 *   - 环形队列：write_pos（生产者指针）、read_pos（消费者指针）
 *   - 同步原语：
 *     · pthread_mutex_t（PTHREAD_PROCESS_SHARED）：保护指针并发访问
 *     · sem_t sem_free：空闲槽位计数信号量，生产者 sem_trywait 非阻塞（满则丢弃）
 *     · sem_t sem_used：已用槽位计数信号量，消费者 sem_wait 阻塞等待
 *   - 哨兵机制：data_len==0 的槽位表示退出信号
 */

static inline log_slot_t *get_slot(void *slots, uint64_t slot_size, uint64_t index) {
    return (log_slot_t *)((char *)slots + slot_size * index);
}

/* 清理共享内存的错误路径（减少重复代码） */
static void shm_cleanup(shm_header_t *header, size_t size) {
    sem_destroy(&header->sem_used);
    sem_destroy(&header->sem_free);
    pthread_mutex_destroy(&header->mutex);
    munmap(header, size);
    shm_unlink(SHM_NAME);
}

/*
 * shm_init — Master 创建并初始化共享内存
 *
 * 步骤：shm_open → ftruncate → mmap → 初始化头部 → 初始化锁/信号量
 */
int shm_init(shm_header_t **header_out, void **slots_out,
             uint64_t slot_size, uint64_t slot_count) {
    size_t total = sizeof(shm_header_t) + (size_t)(slot_size * slot_count);
    int fd;

    /* shm_open：O_EXCL 保证原子创建，已存在则先 unlink */
    fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) {
        shm_unlink(SHM_NAME);
        fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
        if (fd < 0) return -1;
    }

    if (ftruncate(fd, (off_t)total) < 0) { close(fd); shm_unlink(SHM_NAME); return -1; }

    shm_header_t *header = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);  /* mmap 后可关闭 fd */
    if (header == MAP_FAILED) { shm_unlink(SHM_NAME); return -1; }

    /* 初始化头部 */
    header->magic       = SHM_MAGIC;
    header->version     = SHM_VERSION;
    header->buffer_size = total;
    header->slot_size   = slot_size;
    header->slot_count  = slot_count;
    header->write_pos   = 0;
    header->read_pos    = 0;

    /* 跨进程互斥锁 */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&header->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    /* 跨进程信号量 */
    if (sem_init(&header->sem_free, 1, (unsigned int)slot_count) != 0 ||
        sem_init(&header->sem_used, 1, 0) != 0) {
        pthread_mutex_destroy(&header->mutex);
        munmap(header, total);
        shm_unlink(SHM_NAME);
        return -1;
    }

    /* 清零槽位区 */
    void *slots = (char *)header + sizeof(shm_header_t);
    memset(slots, 0, (size_t)(slot_size * slot_count));

    *header_out = header;
    *slots_out  = slots;
    return 0;
}

/*
 * shm_connect — Worker 连接到已有的共享内存
 *
 * 分两步 mmap：先读头部获取 buffer_size，再用正确大小重新映射。
 * 魔数校验防止连到错误的共享内存对象。
 */
int shm_connect(shm_header_t **header_out, void **slots_out,
                uint64_t *slot_size, uint64_t *slot_count) {
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd < 0) return -1;

    shm_header_t *header = mmap(NULL, sizeof(shm_header_t),
                                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (header == MAP_FAILED) { close(fd); return -1; }

    if (header->magic != SHM_MAGIC) {
        munmap(header, sizeof(shm_header_t)); close(fd); return -1;
    }

    size_t total = (size_t)header->buffer_size;
    munmap(header, sizeof(shm_header_t));

    header = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (header == MAP_FAILED) return -1;

    *header_out = header;
    *slots_out  = (char *)header + sizeof(shm_header_t);
    *slot_size  = header->slot_size;
    *slot_count = header->slot_count;
    return 0;
}

/*
 * shm_produce — 生产者：向环形缓冲区写入一条日志（Master 调用）
 *
 * 流程：sem_trywait(sem_free) 非阻塞 → lock → 写入 slot[write_pos] →
 *       write_pos++ → unlock → sem_post(sem_used)
 * 缓冲区满时 sem_trywait 返回 EAGAIN，丢弃此条日志（不阻塞网络接收）
 */
int shm_produce(shm_header_t *header, void *slots,
                const struct sockaddr_storage *addr, uint8_t protocol,
                const char *data, uint32_t data_len, uint64_t timestamp) {
    if (sem_trywait(&header->sem_free) != 0) return -1;  /* 满则丢弃 */

    pthread_mutex_lock(&header->mutex);

    log_slot_t *slot = get_slot(slots, header->slot_size, header->write_pos);
    if (addr) memcpy(&slot->client_addr, addr, sizeof(struct sockaddr_storage));

    /* 防御：数据长度不能超过 data 数组 */
    if (data_len > sizeof(slot->data)) data_len = sizeof(slot->data);

    slot->protocol  = protocol;
    slot->data_len  = data_len;
    slot->timestamp = timestamp;
    memcpy(slot->data, data, data_len);
    slot->data[data_len] = '\0';

    header->write_pos = (header->write_pos + 1) % header->slot_count;

    pthread_mutex_unlock(&header->mutex);
    sem_post(&header->sem_used);
    return 0;
}

/*
 * shm_consume — 消费者：从环形缓冲区读取一条日志（Worker 调用）
 *
 * 流程：sem_wait(sem_used) 阻塞等待 → lock → 读取 slot[read_pos] →
 *       read_pos++ → unlock → sem_post(sem_free)
 * 检测到 data_len==0 的哨兵时返回 0，通知 Worker 退出。
 */
int shm_consume(shm_header_t *header, void *slots, uint64_t slot_size,
                struct sockaddr_storage *addr, uint8_t *protocol,
                char *data, uint32_t *data_len, uint64_t *timestamp) {
    (void)slot_size;
    sem_wait(&header->sem_used);
    pthread_mutex_lock(&header->mutex);

    log_slot_t *slot = get_slot(slots, header->slot_size, header->read_pos);

    if (slot->data_len == 0) {  /* 哨兵 */
        header->read_pos = (header->read_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);
        sem_post(&header->sem_free);
        *data_len = 0;
        return 0;
    }

    memcpy(addr, &slot->client_addr, sizeof(struct sockaddr_storage));
    *protocol  = slot->protocol;
    *data_len  = slot->data_len;
    *timestamp = slot->timestamp;
    memcpy(data, slot->data, slot->data_len);
    data[slot->data_len] = '\0';

    header->read_pos = (header->read_pos + 1) % header->slot_count;

    pthread_mutex_unlock(&header->mutex);
    sem_post(&header->sem_free);
    return (int)slot->data_len;
}

/*
 * shm_send_sentinels — 发送哨兵消息通知 Worker 退出
 *
 * 向缓冲区写入 count 个 data_len=0 的哨兵槽位。
 * 使用阻塞 sem_wait 确保所有哨兵都能写入（此时网络已关闭）。
 */
void shm_send_sentinels(shm_header_t *header, int count) {
    for (int i = 0; i < count; i++) {
        sem_wait(&header->sem_free);
        pthread_mutex_lock(&header->mutex);
        log_slot_t *slot = get_slot((char *)header + sizeof(shm_header_t),
                                    header->slot_size, header->write_pos);
        memset(slot, 0, (size_t)header->slot_size);
        slot->data_len = 0;
        header->write_pos = (header->write_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);
        sem_post(&header->sem_used);
    }
}

void shm_destroy(shm_header_t *header, void *slots, uint64_t slot_count) {
    (void)slots; (void)slot_count;
    shm_cleanup(header, (size_t)header->buffer_size);
}

void shm_disconnect(shm_header_t *header, void *slots) {
    (void)slots;
    munmap(header, (size_t)header->buffer_size);
}
