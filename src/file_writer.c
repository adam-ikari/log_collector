/* src/file_writer.c */
#include "file_writer.h"

/*
 * file_writer.c — 日志文件写入器
 *
 * 将格式化后的日志按 client IP 分目录、按日期分文件写入磁盘。
 * 目录结构：<log_dir>/<ip>/YYYY-MM-DD.log
 * 自动创建目录、自动切换日期文件。
 */

static void get_current_date(char *buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(buf, buf_size, "%Y-%m-%d", &tm_info);
}

/*
 * ensure_directory — 递归创建目录
 *
 * mkdir 不会自动创建父目录。例如 mkdir("/var/log/collector/192.168.1.100")
 * 时如果 /var/log/collector 不存在会返回 ENOENT。需要先递归创建父目录。
 */
static int ensure_directory(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;

    /* 递归创建父目录 */
    char parent[320];
    snprintf(parent, sizeof(parent), "%s", dir);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
        if (ensure_directory(parent) < 0) return -1;
    }
    return mkdir(dir, 0755);
}

void file_writer_init(file_writer_t *fw, const char *log_dir) {
    snprintf(fw->log_dir, sizeof(fw->log_dir), "%s", log_dir);
    fw->fd = -1;
    fw->current_ip[0] = '\0';
    fw->current_date[0] = '\0';
    ensure_directory(fw->log_dir);
}

/*
 * file_writer_write — 将格式化后的日志写入文件
 *
 * 按客户端 IP 分目录，按日期分文件：<log_dir>/<ip>/YYYY-MM-DD.log
 * 检测 IP 或日期变化时自动关闭旧文件、创建新目录、打开新文件。
 */
int file_writer_write(file_writer_t *fw, const char *ip,
                      const char *data, int data_len) {
    char today[16];
    get_current_date(today, sizeof(today));

    /* IP 或日期变了 → 关闭旧文件，打开新文件 */
    if (fw->fd < 0 || strcmp(fw->current_ip, ip) ||
        strcmp(fw->current_date, today)) {

        if (fw->fd >= 0) close(fw->fd);

        char dir_path[320];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", fw->log_dir, ip);
        if (ensure_directory(dir_path) < 0) return -1;

        char file_path[384];
        snprintf(file_path, sizeof(file_path), "%s/%s.log", dir_path, today);
        fw->fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fw->fd < 0) return -1;

        snprintf(fw->current_ip, sizeof(fw->current_ip), "%s", ip);
        snprintf(fw->current_date, sizeof(fw->current_date), "%s", today);
    }

    const char *ptr = data;
    size_t remaining = (size_t)data_len;
    while (remaining > 0) {
        ssize_t written = write(fw->fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        ptr += written;
        remaining -= (size_t)written;
    }
    return 0;
}

void file_writer_close(file_writer_t *fw) {
    if (fw->fd >= 0) { close(fw->fd); fw->fd = -1; }
}
