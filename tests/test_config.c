/* tests/test_config.c */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "config.h"

/*
 * test_config — 验证编译期配置常量的默认值
 *
 * 这些测试确保 config.h 中的宏定义符合设计文档的约定。
 */

static void test_default_ports(void) {
    assert(CFG_TCP_PORT == 5140);
    assert(CFG_UDP_PORT == 5140);
    printf("PASS: test_default_ports\n");
}

static void test_listen_addr(void) {
    assert(strcmp(CFG_LISTEN_ADDR, "0.0.0.0") == 0);
    printf("PASS: test_listen_addr\n");
}

static void test_max_connections(void) {
    assert(CFG_MAX_CONNECTIONS == 1024);
    printf("PASS: test_max_connections\n");
}

static void test_worker_count(void) {
    assert(CFG_WORKER_COUNT == 4);
    printf("PASS: test_worker_count\n");
}

static void test_slot_config(void) {
    assert(CFG_SLOT_SIZE == 4096);
    assert(CFG_SLOT_COUNT == 1024);
    printf("PASS: test_slot_config\n");
}

static void test_log_dir(void) {
    assert(strcmp(CFG_LOG_DIR, "/tmp/log_collector_test") == 0);
    printf("PASS: test_log_dir\n");
}

static void test_pid_file(void) {
    assert(strcmp(CFG_PID_FILE, "/var/run/log-collector.pid") == 0);
    printf("PASS: test_pid_file\n");
}

static void test_slot_size_alignment(void) {
    /* 槽位大小应至少能容纳 log_slot_t 结构体 */
    assert(CFG_SLOT_SIZE >= sizeof(uint32_t) * 4);  /* 基本头部 */
    assert(CFG_SLOT_SIZE >= 256);  /* 合理的最小值 */
    printf("PASS: test_slot_size_alignment\n");
}

static void test_slot_count_positive(void) {
    assert(CFG_SLOT_COUNT > 0);
    printf("PASS: test_slot_count_positive\n");
}

int main(void) {
    test_default_ports();
    test_listen_addr();
    test_max_connections();
    test_worker_count();
    test_slot_config();
    test_log_dir();
    test_pid_file();
    test_slot_size_alignment();
    test_slot_count_positive();
    printf("All config tests passed!\n");
    return 0;
}
