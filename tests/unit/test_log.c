/* SPDX-License-Identifier: MIT */
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static char last_log[2048];
static int log_count = 0;

__attribute__((unused))
static void test_sink(log_level_t level, const char *fmt, va_list ap) {
    (void)level;
    vsnprintf(last_log, sizeof(last_log), fmt, ap);
    log_count++;
}

static void reset(void) {
    last_log[0] = '\0';
    log_count = 0;
}

static void test_set_level(void) {
    bf_log_set_level(LOG_DEBUG);
    assert(bf_log_get_level() == LOG_DEBUG);
    
    bf_log_set_level(LOG_INFO);
    assert(bf_log_get_level() == LOG_INFO);
    
    bf_log_set_level(LOG_WARN);
    assert(bf_log_get_level() == LOG_WARN);
    
    bf_log_set_level(LOG_ERROR);
    assert(bf_log_get_level() == LOG_ERROR);
    
    printf("  test_set_level: OK\n");
}

static void test_level_name(void) {
    assert(strcmp(bf_log_level_name(LOG_DEBUG), "DEBUG") == 0);
    assert(strcmp(bf_log_level_name(LOG_INFO), "INFO") == 0);
    assert(strcmp(bf_log_level_name(LOG_WARN), "WARN") == 0);
    assert(strcmp(bf_log_level_name(LOG_ERROR), "ERROR") == 0);
    
    assert(strcmp(bf_log_level_name((log_level_t)99), "INFO") == 0);
    
    printf("  test_level_name: OK\n");
}

static void test_parse_level(void) {
    log_level_t level;
    
    assert(bf_log_parse_level("debug", &level) == 0);
    assert(level == LOG_DEBUG);
    
    assert(bf_log_parse_level("INFO", &level) == 0);
    assert(level == LOG_INFO);
    
    assert(bf_log_parse_level("warn", &level) == 0);
    assert(level == LOG_WARN);
    
    assert(bf_log_parse_level("WARNING", &level) == 0);
    assert(level == LOG_WARN);
    
    assert(bf_log_parse_level("error", &level) == 0);
    assert(level == LOG_ERROR);
    
    assert(bf_log_parse_level("invalid", &level) == -1);
    
    assert(bf_log_parse_level(NULL, &level) == -1);
    assert(bf_log_parse_level("debug", NULL) == -1);
    
    printf("  test_parse_level: OK\n");
}

static void test_log_debug(void) {
    reset();
    bf_log_set_level(LOG_DEBUG);
    
    bf_log(LOG_DEBUG, "test debug message");
    
    printf("  test_log_debug: OK\n");
}

static void test_log_info(void) {
    reset();
    bf_log_set_level(LOG_DEBUG);
    
    bf_log(LOG_INFO, "test info message");
    
    printf("  test_log_info: OK\n");
}

static void test_log_warn(void) {
    reset();
    bf_log_set_level(LOG_DEBUG);
    
    bf_log(LOG_WARN, "test warn message");
    
    printf("  test_log_warn: OK\n");
}

static void test_log_error(void) {
    reset();
    bf_log_set_level(LOG_DEBUG);
    
    bf_log(LOG_ERROR, "test error message");
    
    printf("  test_log_error: OK\n");
}

static void test_log_level_filtering(void) {
    reset();
    
    bf_log_set_level(LOG_WARN);
    
    bf_log(LOG_DEBUG, "should be filtered");
    bf_log(LOG_INFO, "should be filtered");
    bf_log(LOG_WARN, "should pass");
    bf_log(LOG_ERROR, "should pass");
    
    bf_log_set_level(LOG_INFO);
    printf("  test_log_level_filtering: OK\n");
}

static void test_log_format_string(void) {
    reset();
    bf_log_set_level(LOG_DEBUG);
    
    bf_log(LOG_INFO, "format %d %s", 42, "test");
    
    printf("  test_log_format_string: OK\n");
}

static void test_log_null_format(void) {
    reset();
    bf_log_set_level(LOG_DEBUG);
    
    bf_log(LOG_INFO, "%s", (char*)NULL);
    
    printf("  test_log_null_format: OK\n");
}

static void test_macros(void) {
    reset();
    bf_log_set_level(LOG_DEBUG);
    
    LOGD("debug macro test");
    LOGI("info macro test");
    LOGW("warn macro test");
    LOGE("error macro test");
    
    printf("  test_macros: OK\n");
}

int main(void) {
    printf("test_log:\n");
    
    test_set_level();
    test_level_name();
    test_parse_level();
    test_log_debug();
    test_log_info();
    test_log_warn();
    test_log_error();
    test_log_level_filtering();
    test_log_format_string();
    test_log_null_format();
    test_macros();
    
    printf("test_log: OK\n");
    return 0;
}