/* tests/test_file_writer.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include "file_writer.h"

static void test_write_and_date_switch(void) {
    file_writer_t fw;
    char buf[256];
    char file_path[384];
    char today[16];

    file_writer_init(&fw, "/tmp/test_log_collector_writer");

    /* 写入日志 */
    int rc = file_writer_write(&fw, "192.168.1.1", "test log entry 1\n", 18);
    assert(rc == 0);

    /* 验证文件存在 */
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(today, sizeof(today), "%Y-%m-%d", &tm_info);

    snprintf(file_path, sizeof(file_path),
             "/tmp/test_log_collector_writer/192.168.1.1/%s.log", today);

    int fd = open(file_path, O_RDONLY);
    assert(fd >= 0);
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    assert(n > 0);
    buf[n] = '\0';
    assert(strstr(buf, "test log entry 1") != NULL);
    close(fd);

    /* 写入同一 IP 的另一条 */
    rc = file_writer_write(&fw, "192.168.1.1", "test log entry 2\n", 18);
    assert(rc == 0);

    file_writer_close(&fw);

    /* 清理 */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf /tmp/test_log_collector_writer");
    system(cmd);

    printf("PASS: test_write_and_date_switch\n");
}

static void test_ip_switch(void) {
    file_writer_t fw;
    char today[16];

    file_writer_init(&fw, "/tmp/test_log_collector_writer2");

    file_writer_write(&fw, "10.0.0.1", "msg from host1\n", 15);
    file_writer_write(&fw, "10.0.0.2", "msg from host2\n", 15);

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(today, sizeof(today), "%Y-%m-%d", &tm_info);

    /* 两个目录都应该存在 */
    struct stat st;
    assert(stat("/tmp/test_log_collector_writer2/10.0.0.1", &st) == 0);
    assert(stat("/tmp/test_log_collector_writer2/10.0.0.2", &st) == 0);

    file_writer_close(&fw);
    system("rm -rf /tmp/test_log_collector_writer2");

    printf("PASS: test_ip_switch\n");
}

int main(void) {
    test_write_and_date_switch();
    test_ip_switch();
    printf("All file_writer tests passed!\n");
    return 0;
}
