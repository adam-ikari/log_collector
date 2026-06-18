/* src/file_writer.c */
#include "file_writer.h"

static void get_current_date(char *buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(buf, buf_size, "%Y-%m-%d", &tm_info);
}

static int ensure_directory(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(dir, 0755);
}

void file_writer_init(file_writer_t *fw, const char *log_dir) {
    strncpy(fw->log_dir, log_dir, sizeof(fw->log_dir) - 1);
    fw->log_dir[sizeof(fw->log_dir) - 1] = '\0';
    fw->fd = -1;
    fw->current_ip[0] = '\0';
    fw->current_date[0] = '\0';

    /* 确保日志根目录存在 */
    ensure_directory(fw->log_dir);
}

int file_writer_write(file_writer_t *fw, const char *ip,
                      const char *data, int data_len) {
    char today[16];
    char dir_path[320];
    char file_path[384];
    struct stat st;

    get_current_date(today, sizeof(today));

    /* 检查是否需要切换文件 */
    if (fw->fd < 0 ||
        strcmp(fw->current_ip, ip) != 0 ||
        strcmp(fw->current_date, today) != 0) {

        /* 关闭旧文件 */
        if (fw->fd >= 0) {
            close(fw->fd);
            fw->fd = -1;
        }

        /* 创建客户端目录 */
        snprintf(dir_path, sizeof(dir_path), "%s/%s", fw->log_dir, ip);
        if (ensure_directory(dir_path) < 0) return -1;

        /* 打开日志文件 */
        snprintf(file_path, sizeof(file_path), "%s/%s.log", dir_path, today);
        fw->fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fw->fd < 0) return -1;

        strncpy(fw->current_ip, ip, sizeof(fw->current_ip) - 1);
        strncpy(fw->current_date, today, sizeof(fw->current_date) - 1);
    }

    /* 写入 */
    ssize_t written = write(fw->fd, data, (size_t)data_len);
    return (written == (ssize_t)data_len) ? 0 : -1;
}

void file_writer_close(file_writer_t *fw) {
    if (fw->fd >= 0) {
        close(fw->fd);
        fw->fd = -1;
    }
}
