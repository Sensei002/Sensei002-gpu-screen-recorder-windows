#ifndef GSR_LOG_H
#define GSR_LOG_H

typedef enum {
    GSR_LOG_LEVEL_DEBUG,
    GSR_LOG_LEVEL_INFO,
    GSR_LOG_LEVEL_WARNING,
    GSR_LOG_LEVEL_ERROR
} gsr_log_level;

/* The handler may be called from any thread. |message| has no prefix, level name or trailing newline */
typedef void (*gsr_log_handler)(gsr_log_level level, const char *message, void *userdata);

void gsr_log(gsr_log_level level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Messages below |level| are discarded */
void gsr_log_set_level(gsr_log_level level);
/* A NULL |handler| restores the default handler which prints "gsr <level>: <message>" to stderr */
void gsr_log_set_handler(gsr_log_handler handler, void *userdata);

#endif /* GSR_LOG_H */
