#ifndef BF_LOG_H
#define BF_LOG_H

#include <stdarg.h>

/* Leveled logging (BUG-100). One stream, four severities; lines below the
 * configured threshold are dropped. Format: "[timestamp] LEVEL category: message".
 * The level token lets `bifrost daemon log` color + filter by severity. */
typedef enum { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 } log_level_t;

void bf_log(log_level_t level, const char *fmt, ...);
void bf_logv(log_level_t level, const char *fmt, va_list ap);

void bf_log_set_level(log_level_t level);     /* threshold; default INFO */
log_level_t bf_log_get_level(void);
const char *bf_log_level_name(log_level_t level);            /* "DEBUG"/"INFO"/… */
int bf_log_parse_level(const char *s, log_level_t *out);     /* name → level, 0/-1 */

#define LOGD(...) bf_log(LOG_DEBUG, __VA_ARGS__)
#define LOGI(...) bf_log(LOG_INFO,  __VA_ARGS__)
#define LOGW(...) bf_log(LOG_WARN,  __VA_ARGS__)
#define LOGE(...) bf_log(LOG_ERROR, __VA_ARGS__)

#endif
