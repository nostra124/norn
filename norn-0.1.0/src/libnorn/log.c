#include "log.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static log_level_t g_threshold = LOG_INFO;

void bf_log_set_level(log_level_t level) { g_threshold = level; }
log_level_t bf_log_get_level(void) { return g_threshold; }

const char *bf_log_level_name(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
    }
    return "INFO";
}

int bf_log_parse_level(const char *s, log_level_t *out) {
    if (!s || !out) return -1;
    if      (!strcasecmp(s, "debug")) *out = LOG_DEBUG;
    else if (!strcasecmp(s, "info"))  *out = LOG_INFO;
    else if (!strcasecmp(s, "warn") || !strcasecmp(s, "warning")) *out = LOG_WARN;
    else if (!strcasecmp(s, "error")) *out = LOG_ERROR;
    else return -1;
    return 0;
}

void bf_logv(log_level_t level, const char *fmt, va_list ap) {
    if (level < g_threshold) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(stderr, "[%s] %s ", ts, bf_log_level_name(level));
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
}

void bf_log(log_level_t level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    bf_logv(level, fmt, ap);
    va_end(ap);
}
