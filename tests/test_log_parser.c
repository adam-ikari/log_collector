/* tests/test_log_parser.c */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "log_parser.h"

static void test_basic_syslog(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    int len;
    const char *msg = "<13>Jun 18 14:32:05 myhost myapp[1234]: connection timeout";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.100", &sin->sin_addr);

    len = log_parser_format(&addr, 1655562725, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "192.168.1.100") != NULL);
    assert(strstr(out, "[notice]") != NULL);  /* severity 5 = notice (13 & 0x07 = 5) */
    assert(strstr(out, "connection timeout") != NULL);
    assert(out[len - 1] == '\n');
    printf("PASS: test_basic_syslog\n");
}

static void test_no_pri(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    int len;
    const char *msg = "plain message without priority";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.55", &sin->sin_addr);

    len = log_parser_format(&addr, 1655562725, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[debug]") != NULL); /* 默认级别 */
    assert(strstr(out, "plain message") != NULL);
    printf("PASS: test_no_pri\n");
}

static void test_emerg_severity(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    int len;
    const char *msg = "<0>system is on fire";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[emerg]") != NULL);
    assert(strstr(out, "system is on fire") != NULL);
    printf("PASS: test_emerg_severity\n");
}

static void test_ipv6(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in6 *sin6;
    char out[1024];
    int len;
    const char *msg = "<14>test ipv6 client";

    memset(&addr, 0, sizeof(addr));
    sin6 = (struct sockaddr_in6 *)&addr;
    sin6->sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &sin6->sin6_addr);

    len = log_parser_format(&addr, 1655562725, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[info]") != NULL); /* severity 6 = info (14 & 0x07 = 6) */
    printf("PASS: test_ipv6\n");
}

int main(void) {
    test_basic_syslog();
    test_no_pri();
    test_emerg_severity();
    test_ipv6();
    printf("All log_parser tests passed!\n");
    return 0;
}
