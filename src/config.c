/* src/config.c */
#include "config.h"
#include <ctype.h>

static void config_set_defaults(config_t *cfg) {
    strncpy(cfg->listen_addr, DEFAULT_LISTEN_ADDR, sizeof(cfg->listen_addr) - 1);
    cfg->tcp_port = DEFAULT_TCP_PORT;
    cfg->udp_port = DEFAULT_UDP_PORT;
    cfg->max_connections = DEFAULT_MAX_CONNS;
    cfg->worker_count = DEFAULT_WORKER_COUNT;
    cfg->slot_size = DEFAULT_SLOT_SIZE;
    cfg->slot_count = DEFAULT_SLOT_COUNT;
    strncpy(cfg->log_dir, DEFAULT_LOG_DIR, sizeof(cfg->log_dir) - 1);
}

static void trim_whitespace(char *line) {
    char *start = line;
    char *end;

    /* 跳过前导空白 */
    while (isspace((unsigned char)*start)) start++;

    if (*start == '\0') {
        *line = '\0';
        return;
    }

    /* 跳过尾部空白 */
    end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';

    /* 将去除前导空白后的内容移到缓冲区开头 */
    if (start != line) {
        memmove(line, start, strlen(start) + 1);
    }
}

static int parse_int(const char *str, int *out) {
    char *endptr;
    long val = strtol(str, &endptr, 10);
    if (*endptr != '\0' || val < 0 || val > INT_MAX) return -1;
    *out = (int)val;
    return 0;
}

static int parse_uint64(const char *str, uint64_t *out) {
    char *endptr;
    unsigned long long val = strtoull(str, &endptr, 10);
    if (*endptr != '\0' || val == ULLONG_MAX) return -1;
    *out = (uint64_t)val;
    return 0;
}

static void apply_key_value(config_t *cfg, const char *key, const char *value) {
    if (strcmp(key, "listen_addr") == 0) {
        strncpy(cfg->listen_addr, value, sizeof(cfg->listen_addr) - 1);
    } else if (strcmp(key, "tcp_port") == 0) {
        parse_int(value, &cfg->tcp_port);
    } else if (strcmp(key, "udp_port") == 0) {
        parse_int(value, &cfg->udp_port);
    } else if (strcmp(key, "max_connections") == 0) {
        parse_int(value, &cfg->max_connections);
    } else if (strcmp(key, "worker_count") == 0) {
        parse_int(value, &cfg->worker_count);
    } else if (strcmp(key, "buffer_slot_size") == 0) {
        parse_uint64(value, &cfg->slot_size);
    } else if (strcmp(key, "buffer_slot_count") == 0) {
        parse_uint64(value, &cfg->slot_count);
    } else if (strcmp(key, "log_dir") == 0) {
        strncpy(cfg->log_dir, value, sizeof(cfg->log_dir) - 1);
    }
}

int config_load(config_t *cfg, const char *path) {
    FILE *fp;
    char line[512];
    char key[128], value[384];

    config_set_defaults(cfg);

    if (path == NULL) {
        path = DEFAULT_CONF_PATH;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        /* 配置文件不存在不致命，使用默认值 */
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        /* 移除换行符 */
        line[strcspn(line, "\r\n")] = '\0';
        trim_whitespace(line);

        /* 跳过空行、注释、节头 */
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }

        /* 解析 key=value */
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;

        size_t key_len = (size_t)(eq - line);
        if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
        memcpy(key, line, key_len);
        key[key_len] = '\0';
        trim_whitespace(key);

        strncpy(value, eq + 1, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        trim_whitespace(value);

        apply_key_value(cfg, key, value);
    }

    fclose(fp);
    return 0;
}
