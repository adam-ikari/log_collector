/* tests/test_shm_buffer.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include "shm_buffer.h"

static void test_init_and_destroy(void) {
    shm_header_t *header;
    void *slots;
    int rc;

    rc = shm_init(&header, &slots, 4096, 16);
    assert(rc == 0);
    assert(header->magic == SHM_MAGIC);
    assert(header->version == SHM_VERSION);
    assert(header->slot_size == 4096);
    assert(header->slot_count == 16);
    assert(header->write_pos == 0);
    assert(header->read_pos == 0);

    shm_destroy(header, slots, 16);
    printf("PASS: test_init_and_destroy\n");
}

static void test_init_with_different_sizes(void) {
    /* 测试不同槽位大小和数量的初始化 */
    shm_header_t *header;
    void *slots;
    int rc;

    rc = shm_init(&header, &slots, 1024, 8);
    assert(rc == 0);
    assert(header->magic == SHM_MAGIC);
    assert(header->slot_size == 1024);
    assert(header->slot_count == 8);
    /* 信号量初始化为 slot_count = 8 */
    shm_destroy(header, slots, 8);
    printf("PASS: test_init_with_different_sizes\n");
}

static void test_fork_produce_consume(void) {
    pid_t pid;
    shm_header_t *header;
    void *slots;
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    uint8_t protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char data[4096];
    int rc;
    const char *msg = "<13>test message from UDP";
    uint32_t msg_len = (uint32_t)strlen(msg);

    shm_init(&header, &slots, 4096, 16);

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        /* 子进程: 消费者 */
        shm_header_t *child_header;
        void *child_slots;
        uint64_t child_slot_size, child_slot_count;

        rc = shm_connect(&child_header, &child_slots, &child_slot_size, &child_slot_count);
        assert(rc == 0);
        assert(child_slot_size == 4096);
        assert(child_slot_count == 16);

        /* 消费数据 */
        data_len = 0;
        rc = shm_consume(child_header, child_slots, child_slot_size, &addr,
                         &protocol, data, &data_len, &timestamp);
        assert(rc > 0);
        assert(data_len == msg_len);
        assert(protocol == 1);
        assert(strcmp(data, msg) == 0);

        shm_disconnect(child_header, child_slots);
        _exit(0);
    } else {
        /* 父进程: 生产者 */
        usleep(50000); /* 等子进程连接好 */

        memset(&addr, 0, sizeof(addr));
        sin = (struct sockaddr_in *)&addr;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(12345);
        inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

        rc = shm_produce(header, slots, &addr, 1, msg, msg_len, 1655562725);
        assert(rc == 0);

        waitpid(pid, NULL, 0);
        shm_destroy(header, slots, 16);
    }
    printf("PASS: test_fork_produce_consume\n");
}

static void test_sentinel(void) {
    pid_t pid;
    shm_header_t *header;
    void *slots;
    struct sockaddr_storage addr;
    uint8_t protocol;
    uint32_t data_len;
    uint64_t timestamp;
    char data[4096];
    int rc;

    shm_init(&header, &slots, 4096, 16);

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        shm_header_t *child_header;
        void *child_slots;
        uint64_t child_slot_size, child_slot_count;

        shm_connect(&child_header, &child_slots, &child_slot_size, &child_slot_count);

        /* 应该收到哨兵 (data_len=0) */
        data_len = 1;
        rc = shm_consume(child_header, child_slots, child_slot_size, &addr,
                         &protocol, data, &data_len, &timestamp);
        assert(rc == 0);
        assert(data_len == 0);

        shm_disconnect(child_header, child_slots);
        _exit(0);
    } else {
        usleep(50000);
        shm_send_sentinels(header, 1);
        waitpid(pid, NULL, 0);
        shm_destroy(header, slots, 16);
    }
    printf("PASS: test_sentinel\n");
}

/* ── 补充用例 ─────────────────────────────────── */

static void test_ring_wraparound(void) {
    /* 测试环形队列绕回：只有 4 个槽位，写入 6 条，消费 6 条 */
    pid_t pid;
    shm_header_t *header;
    void *slots;
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;

    shm_init(&header, &slots, 4096, 4);  /* 只有 4 个槽位 */

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        shm_header_t *ch;
        void *cs;
        uint64_t ss, sc;
        int got = 0;

        shm_connect(&ch, &cs, &ss, &sc);
        assert(ss == 4096 && sc == 4);

        for (int i = 0; i < 6; i++) {
            struct sockaddr_storage a;
            uint8_t proto;
            uint32_t dlen;
            uint64_t ts;
            char buf[64];
            int r = shm_consume(ch, cs, ss, &a, &proto, buf, &dlen, &ts);
            if (dlen == 0) {
                /* 哨兵——跳过 */
                continue;
            }
            assert(r > 0);
            assert(dlen > 0);
            got++;
        }
        assert(got == 6);  /* 6 条全部消费 */
        shm_disconnect(ch, cs);
        _exit(0);
    } else {
        usleep(50000);

        /* 写入 6 条（超过 4 个槽位，必须环形绕回） */
        for (int i = 0; i < 6; i++) {
            char msg[64];
            int msg_len = snprintf(msg, sizeof(msg), "<13>wraparound msg %d\n", i);
            int r = shm_produce(header, slots, &addr, 1, msg, (uint32_t)msg_len, 1655562725 + i);
            assert(r == 0);
            usleep(1000);  /* 给子进程消费的时间 */
        }

        /* 发哨兵让子进程退出 */
        shm_send_sentinels(header, 1);
        waitpid(pid, NULL, 0);
        shm_destroy(header, slots, 4);
    }
    printf("PASS: test_ring_wraparound\n");
}

static void test_buffer_full_drops(void) {
    /* 缓冲区满时 sem_trywait 返回 -1，生产失败 */
    shm_header_t *header;
    void *slots;
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;

    shm_init(&header, &slots, 4096, 2);  /* 只有 2 个槽位 */

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    /* 填满 2 个槽位 */
    int rc1 = shm_produce(header, slots, &addr, 0, "msg1", 4, 1);
    int rc2 = shm_produce(header, slots, &addr, 0, "msg2", 4, 1);
    assert(rc1 == 0);
    assert(rc2 == 0);

    /* 第 3 条应该被丢弃 */
    int rc3 = shm_produce(header, slots, &addr, 0, "msg3", 4, 1);
    assert(rc3 == -1);

    shm_destroy(header, slots, 2);
    printf("PASS: test_buffer_full_drops\n");
}

static void test_slot_size_protection(void) {
    /* 防御：data_len 超过 sizeof(slot->data) 时被截断 */
    shm_header_t *header;
    void *slots;
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;

    shm_init(&header, &slots, sizeof(log_slot_t), 4);

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    /* 传入超大的 data_len，内部应该截断到 sizeof(slot->data) */
    char big_data[5000];
    memset(big_data, 'X', sizeof(big_data));
    big_data[sizeof(big_data) - 1] = '\0';

    int rc = shm_produce(header, slots, &addr, 0, big_data, 5000, 1);
    assert(rc == 0);

    /* 消费验证：实际写入长度被截断 */
    uint8_t proto;
    uint32_t dlen;
    uint64_t ts;
    char buf[5000];
    struct sockaddr_storage a;
    int consume_rc = shm_consume(header, slots, sizeof(log_slot_t),
                                  &a, &proto, buf, &dlen, &ts);
    assert(consume_rc > 0);
    /* 实际长度不应超过 data 数组 */
    assert(dlen <= 4096);

    shm_destroy(header, slots, 4);
    printf("PASS: test_slot_size_protection\n");
}

static void test_shm_connect_magic_fail(void) {
    /* 创建共享内存后改掉魔数，shm_connect 应该失败 */
    shm_header_t *header;
    void *slots;

    shm_init(&header, &slots, 4096, 4);

    /* 破坏魔数 */
    header->magic = 0xDEADBEEF;

    shm_header_t *ch;
    void *cs;
    uint64_t ss, sc;
    int rc = shm_connect(&ch, &cs, &ss, &sc);
    assert(rc == -1);  /* 魔数不匹配应失败 */

    /* 恢复魔数以便 destroy */
    header->magic = SHM_MAGIC;
    shm_destroy(header, slots, 4);
    printf("PASS: test_shm_connect_magic_fail\n");
}

static void test_connection_before_destroy(void) {
    /* 验证 Worker 不能在共享内存未创建时连接 */
    shm_header_t *ch;
    void *cs;
    uint64_t ss, sc;

    /* 确保先清理 */
    shm_unlink(SHM_NAME);

    int rc = shm_connect(&ch, &cs, &ss, &sc);
    assert(rc == -1);
    printf("PASS: test_connection_before_destroy\n");
}

int main(void) {
    test_init_and_destroy();
    test_init_with_different_sizes();
    test_fork_produce_consume();
    test_sentinel();
    test_ring_wraparound();
    test_buffer_full_drops();
    test_slot_size_protection();
    test_shm_connect_magic_fail();
    test_connection_before_destroy();
    printf("All shm_buffer tests passed!\n");
    return 0;
}
