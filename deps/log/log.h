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

struct log_handle;

enum { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };

#define log_debug(handle, module, fmt, ...)    log_print(handle, LOG_DEBUG, module, fmt, ##__VA_ARGS__)
#define log_trace(handle, module, fmt, ...)    log_print(handle, LOG_TRACE, module,  fmt, ##__VA_ARGS__)
#define log_info(handle, module, fmt, ...)	    log_print(handle, LOG_INFO, module, fmt, ##__VA_ARGS__)
#define log_warn(handle, module, fmt, ...)		log_print(handle, LOG_WARN, module, fmt, ##__VA_ARGS__)
#define log_error(handle, module, fmt, ...)	log_print(handle, LOG_ERROR, module, fmt, ##__VA_ARGS__)
#define log_fatal(handle, module, fmt, ...)	log_print(handle, LOG_FATAL, module, fmt, ##__VA_ARGS__)

void log_print(struct log_handle *, int level, const char *module, const char *fmt, ...);
void log_options_set_enable(struct log_handle *, int enable);
void log_options_set_level(struct log_handle *, int level);

struct log_handle * log_handle_create(const char *file_path, int level);
void log_handle_destroy(struct log_handle *);

#ifdef __cplusplus
}
#endif

#endif
