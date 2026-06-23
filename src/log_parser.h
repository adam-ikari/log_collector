/* src/log_parser.h */
#ifndef LOG_PARSER_H
#define LOG_PARSER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 解析 syslog 消息，返回格式化后的行 (包含换行符)。
   输出缓冲区由调用者提供，out_size 指定大小。
   返回实际写入的字节数(不含 NUL)，-1 表示错误。 */
int log_parser_format(const struct sockaddr_storage *addr,
                      uint64_t recv_timestamp,
                      const char *raw_msg, uint32_t raw_len,
                      char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* LOG_PARSER_H */
