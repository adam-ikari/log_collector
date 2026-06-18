/* src/shm_buffer.c */
#include "shm_buffer.h"

int shm_init(shm_header_t **header_out, void **slots_out,
             uint64_t slot_size, uint64_t slot_count) {
    uint64_t total_size;
    size_t shm_size;
    int shm_fd;
    shm_header_t *header;
    void *slots;
    pthread_mutexattr_t mutex_attr;

    /* 计算总大小 */
    total_size = sizeof(shm_header_t) + slot_size * slot_count;
    if (total_size > (uint64_t)SIZE_MAX) {
        return -1;
    }
    shm_size = (size_t)total_size;

    /* 使用文件后备 mmap (兼容无 /dev/shm 的环境) */
    shm_fd = open(SHM_FILE_PATH, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (shm_fd < 0) {
        /* 可能上次未清理，尝试先 unlink 再创建 */
        unlink(SHM_FILE_PATH);
        shm_fd = open(SHM_FILE_PATH, O_CREAT | O_RDWR | O_EXCL, 0600);
        if (shm_fd < 0) {
            return -1;
        }
    }

    if (ftruncate(shm_fd, (off_t)shm_size) < 0) {
        close(shm_fd);
        unlink(SHM_FILE_PATH);
        return -1;
    }

    /* mmap 映射 */
    header = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd); /* mmap 后可以关闭 fd */
    if (header == MAP_FAILED) {
        unlink(SHM_FILE_PATH);
        return -1;
    }

    /* 初始化头部 */
    header->magic = SHM_MAGIC;
    header->version = SHM_VERSION;
    header->buffer_size = shm_size;
    header->slot_size = slot_size;
    header->slot_count = slot_count;
    header->write_pos = 0;
    header->read_pos = 0;

    /* 初始化跨进程互斥锁 */
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&header->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    /* 初始化跨进程信号量 (嵌入在共享内存中) */
    if (sem_init(&header->sem_free, 1, (unsigned int)slot_count) != 0) {
        pthread_mutex_destroy(&header->mutex);
        munmap(header, shm_size);
        unlink(SHM_FILE_PATH);
        return -1;
    }
    if (sem_init(&header->sem_used, 1, 0) != 0) {
        sem_destroy(&header->sem_free);
        pthread_mutex_destroy(&header->mutex);
        munmap(header, shm_size);
        unlink(SHM_FILE_PATH);
        return -1;
    }

    /* 清零槽位区 */
    slots = (void *)((char *)header + sizeof(shm_header_t));
    memset(slots, 0, (size_t)(slot_size * slot_count));

    *header_out = header;
    *slots_out = slots;
    return 0;
}

static inline log_slot_t *get_slot(void *slots, uint64_t slot_size, uint64_t index) {
    return (log_slot_t *)((char *)slots + slot_size * index);
}

int shm_connect(shm_header_t **header_out, void **slots_out,
                uint64_t *slot_size, uint64_t *slot_count) {
    int shm_fd;
    shm_header_t *header;
    size_t shm_size;

    shm_fd = open(SHM_FILE_PATH, O_RDWR, 0600);
    if (shm_fd < 0) return -1;

    /* 先读取头部获取大小 */
    header = mmap(NULL, sizeof(shm_header_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (header == MAP_FAILED) {
        close(shm_fd);
        return -1;
    }

    if (header->magic != SHM_MAGIC) {
        munmap(header, sizeof(shm_header_t));
        close(shm_fd);
        return -1;
    }

    shm_size = (size_t)header->buffer_size;

    /* 重新映射完整区域 */
    munmap(header, sizeof(shm_header_t));
    header = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (header == MAP_FAILED) return -1;

    *header_out = header;
    *slots_out = (void *)((char *)header + sizeof(shm_header_t));
    *slot_size = header->slot_size;
    *slot_count = header->slot_count;
    return 0;
}

int shm_produce(shm_header_t *header, void *slots,
                const struct sockaddr_storage *addr, uint8_t protocol,
                const char *data, uint32_t data_len, uint64_t timestamp) {
    log_slot_t *slot;

    /* 非阻塞尝试获取空闲槽位，满则丢弃 */
    if (sem_trywait(&header->sem_free) != 0) {
        return -1; /* 缓冲区满 */
    }

    pthread_mutex_lock(&header->mutex);

    slot = get_slot(slots, header->slot_size, header->write_pos);
    if (addr != NULL) {
        memcpy(&slot->client_addr, addr, sizeof(struct sockaddr_storage));
    }
    slot->protocol = protocol;
    slot->data_len = data_len;
    slot->timestamp = timestamp;
    memcpy(slot->data, data, data_len);
    slot->data[data_len] = '\0';

    header->write_pos = (header->write_pos + 1) % header->slot_count;

    pthread_mutex_unlock(&header->mutex);

    /* 通知消费者 */
    sem_post(&header->sem_used);

    return 0;
}

int shm_consume(shm_header_t *header, void *slots, uint64_t slot_size,
                struct sockaddr_storage *addr, uint8_t *protocol,
                char *data, uint32_t *data_len, uint64_t *timestamp) {
    log_slot_t *slot;
    (void)slot_size;

    /* 阻塞等待 */
    sem_wait(&header->sem_used);

    pthread_mutex_lock(&header->mutex);

    slot = get_slot(slots, header->slot_size, header->read_pos);

    if (slot->data_len == 0) {
        /* 哨兵 */
        header->read_pos = (header->read_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);
        /* 释放一个空闲槽位 */
        sem_post(&header->sem_free);
        *data_len = 0;
        return 0;
    }

    memcpy(addr, &slot->client_addr, sizeof(struct sockaddr_storage));
    *protocol = slot->protocol;
    *data_len = slot->data_len;
    *timestamp = slot->timestamp;
    memcpy(data, slot->data, slot->data_len);
    data[slot->data_len] = '\0';

    header->read_pos = (header->read_pos + 1) % header->slot_count;

    pthread_mutex_unlock(&header->mutex);

    /* 释放一个空闲槽位 */
    sem_post(&header->sem_free);

    return (int)slot->data_len;
}

void shm_send_sentinels(shm_header_t *header, int count) {
    int i;
    log_slot_t *slot;

    for (i = 0; i < count; i++) {
        /* 阻塞直到有空槽位 */
        sem_wait(&header->sem_free);

        pthread_mutex_lock(&header->mutex);
        slot = get_slot((void *)((char *)header + sizeof(shm_header_t)),
                        header->slot_size, header->write_pos);
        memset(slot, 0, (size_t)header->slot_size);
        slot->data_len = 0; /* 哨兵标记 */
        header->write_pos = (header->write_pos + 1) % header->slot_count;
        pthread_mutex_unlock(&header->mutex);

        sem_post(&header->sem_used);
    }
}

void shm_destroy(shm_header_t *header, void *slots, uint64_t slot_count) {
    (void)slots;
    (void)slot_count;

    sem_destroy(&header->sem_used);
    sem_destroy(&header->sem_free);
    pthread_mutex_destroy(&header->mutex);
    munmap(header, (size_t)header->buffer_size);
    unlink(SHM_FILE_PATH);
}

void shm_disconnect(shm_header_t *header, void *slots) {
    (void)slots;
    munmap(header, (size_t)header->buffer_size);
}
