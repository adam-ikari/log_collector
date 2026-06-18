/* tests/test_config.c */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "config.h"

static void test_defaults(void) {
    config_t cfg;
    int rc = config_load(&cfg, "/nonexistent/path.conf");
    assert(rc == 0);
    assert(strcmp(cfg.listen_addr, "0.0.0.0") == 0);
    assert(cfg.tcp_port == 5140);
    assert(cfg.udp_port == 5140);
    assert(cfg.max_connections == 1024);
    assert(cfg.worker_count == 4);
    assert(cfg.slot_size == 4096);
    assert(cfg.slot_count == 1024);
    assert(strcmp(cfg.log_dir, "/var/log/collector") == 0);
    printf("PASS: test_defaults\n");
}

static void test_parse_config(void) {
    const char *test_conf = "/tmp/test_log_collector.conf";
    FILE *fp = fopen(test_conf, "w");
    fprintf(fp, "[server]\n");
    fprintf(fp, "listen_addr = 192.168.1.1\n");
    fprintf(fp, "tcp_port = 9999\n");
    fprintf(fp, "udp_port = 8888\n");
    fprintf(fp, "max_connections = 512\n");
    fprintf(fp, "[worker]\n");
    fprintf(fp, "worker_count = 8\n");
    fprintf(fp, "buffer_slot_size = 8192\n");
    fprintf(fp, "buffer_slot_count = 2048\n");
    fprintf(fp, "[storage]\n");
    fprintf(fp, "log_dir = /tmp/logs\n");
    fclose(fp);

    config_t cfg;
    int rc = config_load(&cfg, test_conf);
    assert(rc == 0);
    assert(strcmp(cfg.listen_addr, "192.168.1.1") == 0);
    assert(cfg.tcp_port == 9999);
    assert(cfg.udp_port == 8888);
    assert(cfg.max_connections == 512);
    assert(cfg.worker_count == 8);
    assert(cfg.slot_size == 8192);
    assert(cfg.slot_count == 2048);
    assert(strcmp(cfg.log_dir, "/tmp/logs") == 0);
    remove(test_conf);
    printf("PASS: test_parse_config\n");
}

static void test_comments_and_blanks(void) {
    const char *test_conf = "/tmp/test_log_collector_conf2.conf";
    FILE *fp = fopen(test_conf, "w");
    fprintf(fp, "# This is a comment\n");
    fprintf(fp, "; Also a comment\n");
    fprintf(fp, "\n");
    fprintf(fp, "[server]\n");
    fprintf(fp, "tcp_port = 7777\n");
    fprintf(fp, "\n");
    fprintf(fp, "# another comment\n");
    fprintf(fp, "listen_addr = 10.0.0.1\n");
    fclose(fp);

    config_t cfg;
    config_load(&cfg, test_conf);
    assert(cfg.tcp_port == 7777);
    assert(strcmp(cfg.listen_addr, "10.0.0.1") == 0);
    remove(test_conf);
    printf("PASS: test_comments_and_blanks\n");
}

int main(void) {
    test_defaults();
    test_parse_config();
    test_comments_and_blanks();
    printf("All config tests passed!\n");
    return 0;
}
