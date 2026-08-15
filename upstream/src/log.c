#include "../include/log.h"

#include <stdio.h>
#include <stdarg.h>

#define GSR_LOG_MESSAGE_MAX_SIZE 4096

static const char* log_level_to_string(gsr_log_level level) {
    switch(level) {
        case GSR_LOG_LEVEL_DEBUG:   return "debug";
        case GSR_LOG_LEVEL_INFO:    return "info";
        case GSR_LOG_LEVEL_WARNING: return "warning";
        case GSR_LOG_LEVEL_ERROR:   return "error";
    }
    return "unknown";
}

static void gsr_log_default_handler(gsr_log_level level, const char *message, void *userdata) {
    (void)userdata;
    fprintf(stderr, "gsr %s: %s\n", log_level_to_string(level), message);
}

static gsr_log_level log_level = GSR_LOG_LEVEL_INFO;
static gsr_log_handler log_handler = gsr_log_default_handler;
static void *log_handler_userdata = NULL;

void gsr_log(gsr_log_level level, const char *fmt, ...) {
    if(level < log_level)
        return;

    char message[GSR_LOG_MESSAGE_MAX_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    log_handler(level, message, log_handler_userdata);
}

void gsr_log_set_level(gsr_log_level level) {
    log_level = level;
}

void gsr_log_set_handler(gsr_log_handler handler, void *userdata) {
    log_handler = handler ? handler : gsr_log_default_handler;
    log_handler_userdata = userdata;
}
