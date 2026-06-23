/* src/log_parser.c */
#include "log_parser.h"

/*
 * log_parser.c — Syslog 日志解析器
 *
 * 负责解析原始 syslog 消息，提取 PRI（severity）、
 * 格式化输出为：timestamp ip [level] message\n
 *
 * Syslog PRI 格式：<N> 其中 N = facility * 8 + severity
 * severity 取值范围 0-7：emerg, alert, crit, err, warning, notice, info, debug
 */

static const char *severity_names[] = {
    "emerg", "alert", "crit", "err",
    "warning", "notice", "info", "debug"
};

/*
 * addr_to_str — 将 sockaddr_storage 转换为 IP 字符串
 *
 * 支持 IPv4 (AF_INET) 和 IPv6 (AF_INET6)。
 * 未知地址族返回 "unknown"。
 */
static const char *addr_to_str(const struct sockaddr_storage *addr,
                               char *buf, size_t buf_size) {
    if (addr->ss_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
        inet_ntop(AF_INET, &sin->sin_addr, buf, (socklen_t)buf_size);
    } else if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)addr;
        inet_ntop(AF_INET6, &sin6->sin6_addr, buf, (socklen_t)buf_size);
    } else {
        snprintf(buf, buf_size, "unknown");
    }
    return buf;
}

/*
 * extract_pri — 从 <PRI> 前缀中提取 severity
 *
 * syslog PRI = facility * 8 + severity，severity 取低 3 位（pri & 0x07）。
 * 返回 PRI 部分的长度（含尖括号），失败返回 -1。
 */
static int extract_pri(const char *msg, uint32_t len, int *severity_out) {
    const char *end;
    long pri;

    if (len < 3 || msg[0] != '<') return -1;

    end = memchr(msg, '>', len);
    if (end == NULL) return -1;

    pri = strtol(msg + 1, NULL, 10);
    *severity_out = (int)(pri & 0x07);
    return (int)(end - msg + 1);
}

/*
 * log_parser_format — 格式化一条日志为人类可读的行
 *
 * 输出格式：YYYY-MM-DDTHH:MM:SS±TZ IP [level] message\n
 * 优先级提取失败时默认 severity=debug。
 * 如果 body 不以换行结尾，自动追加 '\n'。
 * 返回实际写入字节数，-1 表示错误。
 */
int log_parser_format(const struct sockaddr_storage *addr,
                      uint64_t recv_timestamp,
                      const char *raw_msg, uint32_t raw_len,
                      char *out, size_t out_size) {
    char ip_str[INET6_ADDRSTRLEN];
    char time_buf[32];
    time_t ts;
    struct tm tm_info;
    int severity = 7; /* 默认 debug */
    int pri_len;
    const char *body;
    uint32_t body_len;
    int written;

    /* 时间戳转换 */
    ts = (time_t)recv_timestamp;
    localtime_r(&ts, &tm_info);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S%z", &tm_info);

    /* IP 地址 */
    addr_to_str(addr, ip_str, sizeof(ip_str));

    /* 提取 PRI */
    pri_len = extract_pri(raw_msg, raw_len, &severity);
    if (pri_len > 0) {
        body = raw_msg + pri_len;
        body_len = raw_len - (uint32_t)pri_len;
        /* 跳过前导空格 */
        while (body_len > 0 && *body == ' ') {
            body++;
            body_len--;
        }
    } else {
        body = raw_msg;
        body_len = raw_len;
    }

    /* 格式化输出: timestamp ip [level] message */
    written = snprintf(out, out_size, "%s %s [%s] ",
                       time_buf, ip_str, severity_names[severity]);

    if (written < 0 || (size_t)written >= out_size) return -1;

    /* 追加消息体 (确保不溢出) */
    size_t remaining = out_size - (size_t)written;
    size_t copy_len = (size_t)body_len < remaining - 1 ? (size_t)body_len : remaining - 1;
    memcpy(out + written, body, copy_len);
    written += (int)copy_len;

    /* 确保以换行结束 */
    if (written > 0 && out[written - 1] != '\n') {
        if ((size_t)written < out_size - 1) {
            out[written] = '\n';
            written++;
        }
    }
    out[written] = '\0';

    return written;
}
