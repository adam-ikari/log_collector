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

/* ── 补充用例 ─────────────────────────────────── */

static void test_all_severities(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    const char *names[] = {"emerg","alert","crit","err","warning","notice","info","debug"};
    char out[1024];

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    for (int i = 0; i < 8; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "<%d>test severity %d", i, i);
        int len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
        assert(len > 0);
        char expected[16];
        snprintf(expected, sizeof(expected), "[%s]", names[i]);
        assert(strstr(out, expected) != NULL);
    }
    printf("PASS: test_all_severities\n");
}

static void test_pri_only_message(void) {
    /* 测试 PRI 格式正确但没有消息体 */
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    const char *msg = "<13>";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.1", &sin->sin_addr);

    int len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[notice]") != NULL);
    /* 消息体只有 PRI 本身（空消息体视为空），输出以换行结束 */
    assert(out[len - 1] == '\n');
    printf("PASS: test_pri_only_message\n");
}

static void test_unknown_address_family(void) {
    struct sockaddr_storage addr;
    char out[1024];
    const char *msg = "test unknown AF";

    memset(&addr, 0, sizeof(addr));
    addr.ss_family = AF_UNIX;  /* 非 IP 地址族 */
    int len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "unknown") != NULL);
    printf("PASS: test_unknown_address_family\n");
}

static void test_buffer_too_small(void) {
    /* 输出缓冲区太小，snprintf 截断——返回 -1 */
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[8];
    const char *msg = "<14>long message that will not fit";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    int len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len == -1);  /* 时间戳+IP+[level] 超过 8 字节 */
    printf("PASS: test_buffer_too_small\n");
}

static void test_no_pri_with_angle_bracket(void) {
    /* 有 < 但不是合法 PRI 格式（没有闭合的 >）→ 默认 debug */
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    const char *msg = "<not_a_pri message without close";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.2", &sin->sin_addr);

    int len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[debug]") != NULL);
    printf("PASS: test_no_pri_with_angle_bracket\n");
}

static void test_ipv4_mapped_ipv6(void) {
    struct sockaddr_storage addr;
    struct sockaddr_in6 *sin6;
    char out[1024];
    const char *msg = "<13>test ipv6 mapped address";

    memset(&addr, 0, sizeof(addr));
    sin6 = (struct sockaddr_in6 *)&addr;
    sin6->sin6_family = AF_INET6;
    inet_pton(AF_INET6, "2001:db8::1", &sin6->sin6_addr);

    int len = log_parser_format(&addr, 1655562725, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "2001:db8::1") != NULL);
    assert(strstr(out, "[notice]") != NULL);
    printf("PASS: test_ipv4_mapped_ipv6\n");
}

static void test_small_buffer_after_timestamp(void) {
    /* 正好容纳时间戳+IP+level但不够容纳消息体的缓冲区 */
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[64];

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "1.1.1.1", &sin->sin_addr);

    const char *msg = "<14>short";
    int len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "short") != NULL);
    printf("PASS: test_small_buffer_after_timestamp\n");
}

static void test_missing_newline_before_end(void) {
    /* 消息体不以换行结尾 → 自动追加 \n */
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    const char *msg = "<14>message without trailing newline";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    int len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(out[len - 1] == '\n');
    printf("PASS: test_missing_newline_before_end\n");
}

static void test_pri_with_leading_spaces(void) {
    /* PRI 后带前导空格：<13>   message → 空格应被跳过 */
    struct sockaddr_storage addr;
    struct sockaddr_in *sin;
    char out[1024];
    const char *msg = "<13>   message with leading spaces";

    memset(&addr, 0, sizeof(addr));
    sin = (struct sockaddr_in *)&addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);

    int len = log_parser_format(&addr, 1, msg, (uint32_t)strlen(msg), out, sizeof(out));
    assert(len > 0);
    assert(strstr(out, "[notice]") != NULL);
    /* 消息体不应包含前导空格 */
    assert(strstr(out, "message with leading spaces") != NULL);
    printf("PASS: test_pri_with_leading_spaces\n");
}

int main(void) {
    test_basic_syslog();
    test_no_pri();
    test_emerg_severity();
    test_ipv6();
    test_all_severities();
    test_pri_only_message();
    test_unknown_address_family();
    test_buffer_too_small();
    test_no_pri_with_angle_bracket();
    test_ipv4_mapped_ipv6();
    test_small_buffer_after_timestamp();
    test_missing_newline_before_end();
    test_pri_with_leading_spaces();
    printf("All log_parser tests passed!\n");
    return 0;
}
