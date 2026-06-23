/* src/file_writer.h */
#ifndef FILE_WRITER_H
#define FILE_WRITER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Worker 文件写入上下文 */
typedef struct {
    int   fd;                 /* 当前文件句柄 */
    char  current_ip[INET6_ADDRSTRLEN];  /* 当前 IP */
    char  current_date[16];   /* 当前日期 YYYY-MM-DD */
    char  log_dir[256];       /* 日志根目录 */
} file_writer_t;

/* 初始化 file_writer */
void file_writer_init(file_writer_t *fw, const char *log_dir);

/* 写入一条格式化日志，自动处理目录创建和日期切换。
   返回 0 成功，-1 失败。 */
int file_writer_write(file_writer_t *fw, const char *ip,
                      const char *data, int data_len);

/* 关闭当前文件 */
void file_writer_close(file_writer_t *fw);

#ifdef __cplusplus
}
#endif

#endif /* FILE_WRITER_H */
