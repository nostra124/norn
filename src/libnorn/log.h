/**
 * @file log.h
 * @brief Leveled logging with severity filtering
 * 
 * Provides structured logging with four severity levels and threshold-based
 * filtering. Messages below the configured level are dropped.
 * 
 * @par Basic Usage
 * @code
 * #include "log.h"
 * 
 * int main(void) {
 *     bf_log_set_level(LOG_INFO);
 *     
 *     LOGD("Debug message (not shown)");  // Below threshold
 *     LOGI("Info message");                 // Shown
 *     LOGW("Warning message");             // Shown
 *     LOGE("Error message");                // Shown
 *     
 *     return 0;
 * }
 * @endcode
 * 
 * @par Log Levels
 * - LOG_DEBUG: Detailed debugging information
 * - LOG_INFO: Normal operational messages
 * - LOG_WARN: Warning conditions (non-fatal)
 * - LOG_ERROR: Error conditions (may be fatal)
 * 
 * @par Output Format
 * Format: "[timestamp] LEVEL category: message"
 * 
 * The level token enables `bifrost daemon log` to color and filter by severity.
 * 
 * @note Thread Safety: Thread-safe (uses vprintf internally)
 * @note Output: Single stream (stderr by default)
 */

#ifndef BF_LOG_H
#define BF_LOG_H

#include <stdarg.h>

/**
 * @brief Log severity levels
 */
typedef enum { 
    LOG_DEBUG = 0,  /**< Detailed debugging information */
    LOG_INFO  = 1,  /**< Normal operational messages */
    LOG_WARN  = 2,  /**< Warning conditions (non-fatal) */
    LOG_ERROR = 3   /**< Error conditions (may be fatal) */
} log_level_t;

/**
 * @brief Log a message with severity level (variadic)
 * 
 * Logs a message at the given severity level. Messages below the configured
 * threshold (bf_log_set_level) are dropped.
 * 
 * @param level Log severity level
 * @param fmt Printf-style format string
 * @param ... Format arguments
 * 
 * @note Thread Safety: Thread-safe
 * @note NULL-safe: Does nothing if fmt is NULL
 * 
 * @code
 * bf_log(LOG_INFO, "Connected to %s:%d", hostname, port);
 * bf_log(LOG_ERROR, "Failed to bind: %s", strerror(errno));
 * @endcode
 */
void bf_log(log_level_t level, const char *fmt, ...);

/**
 * @brief Log a message with severity level (va_list)
 * 
 * Same as bf_log() but accepts a va_list for wrapper functions.
 * 
 * @param level Log severity level
 * @param fmt Printf-style format string
 * @param ap Format arguments
 * 
 * @note Thread Safety: Thread-safe
 * @note NULL-safe: Does nothing if fmt is NULL
 */
void bf_logv(log_level_t level, const char *fmt, va_list ap);

/**
 * @brief Set the log level threshold
 * 
 * Messages below this level are dropped. Default is LOG_INFO.
 * 
 * @param level Minimum severity to display
 * 
 * @note Thread Safety: Thread-safe
 * 
 * @code
 * bf_log_set_level(LOG_DEBUG);  // Show all messages
 * bf_log_set_level(LOG_WARN);   // Show only warnings and errors
 * @endcode
 */
void bf_log_set_level(log_level_t level);

/**
 * @brief Get the current log level threshold
 * 
 * @return Current log level
 * 
 * @note Thread Safety: Thread-safe
 */
log_level_t bf_log_get_level(void);

/**
 * @brief Get the name of a log level
 * 
 * @param level Log level
 * @return Level name string ("DEBUG", "INFO", "WARN", or "ERROR")
 * 
 * @note Thread Safety: Thread-safe
 * @note NULL-safe: Returns "UNKNOWN" for invalid levels
 */
const char *bf_log_level_name(log_level_t level);

/**
 * @brief Parse a log level from string
 * 
 * @param s Level name ("DEBUG", "INFO", "WARN", or "ERROR")
 * @param out Output: parsed log level
 * @return 0 on success, -1 if not found
 * 
 * @note Thread Safety: Thread-safe
 * @note Case-insensitive
 * 
 * @code
 * log_level_t level;
 * if (bf_log_parse_level("WARN", &level) == 0) {
 *     bf_log_set_level(level);
 * }
 * @endcode
 */
int bf_log_parse_level(const char *s, log_level_t *out);

/** @brief Log debug message (convenience macro) */
#define LOGD(...) bf_log(LOG_DEBUG, __VA_ARGS__)

/** @brief Log info message (convenience macro) */
#define LOGI(...) bf_log(LOG_INFO,  __VA_ARGS__)

/** @brief Log warning message (convenience macro) */
#define LOGW(...) bf_log(LOG_WARN,  __VA_ARGS__)

/** @brief Log error message (convenience macro) */
#define LOGE(...) bf_log(LOG_ERROR, __VA_ARGS__)

#endif /* BF_LOG_H */