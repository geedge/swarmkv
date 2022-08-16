/**
 * Copyright (c) 2020 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `log.c` for details.
 */

#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

#define LOG_VERSION "0.1.0"

typedef	void* log_hander_t;

struct log_event
{
    int level;
    va_list ap;
    const char *fmt;
	const char *module;
	char *run_log_path;
    char *cur_log_file;
    void *user_data;
    pthread_mutex_t mutex;
};

typedef void (*log_func_t)(struct log_event *ev);
typedef void (*log_lock_func_t)(int lock, void *udata);

enum { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };

#define log_debug(handler, module, fmt, ...)    log_print(handler, LOG_DEBUG, module, fmt, ##__VA_ARGS__)
#define log_trace(handler, module, fmt, ...)    log_print(handler, LOG_TRACE, module,  fmt, ##__VA_ARGS__)
#define log_info(handler, module, fmt, ...)	    log_print(handler, LOG_INFO, module, fmt, ##__VA_ARGS__)
#define log_warn(handler, module, fmt, ...)		log_print(handler, LOG_WARN, module, fmt, ##__VA_ARGS__)
#define log_error(handler, module, fmt, ...)	log_print(handler, LOG_ERROR, module, fmt, ##__VA_ARGS__)
#define log_fatal(handler, module, fmt, ...)	log_print(handler, LOG_FATAL, module, fmt, ##__VA_ARGS__)

void log_print(log_hander_t, int level, const char *module, const char *fmt, ...);

int log_options_add_file_fp(log_hander_t, int level);
int log_options_add_fp(log_hander_t, FILE *fp, int level);

void log_options_set_lock_fn(log_hander_t, log_lock_func_t fn, void *user_data);
void log_options_set_enable(log_hander_t, int enable);
void log_options_set_level(log_hander_t, int level);

log_hander_t create_log_handle(const char *file_path, int level);
void log_handle_destroy(log_hander_t);

#ifdef __cplusplus
}
#endif

#endif
