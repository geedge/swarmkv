/*
 * Copyright (c) 2020 rxi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#include "log.h"

#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))

typedef enum {
    LOG_OP_IFACE_CONSOLE,
    LOG_OP_IFACE_FILE,
    RT_LOG_OP_IFACE_MAX,
}log_op_iface;

struct log_handle
{
    int level;
    int enable;
    FILE *fp;
    va_list ap;
	char defined_log_fn[1024];
    char runtime_log_fn[1024];
    pthread_mutex_t mutex;
    log_op_iface iface;
};

static unsigned char weekday_str[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static unsigned char month_str[12][4] = {"Jan", "Feb", "Mar", "Apr","May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int log_create_dir(const char *dir_path, int path_len)
{
    if(dir_path == NULL)
        return -1;

    char *buf = (char *)calloc(path_len+1, 1);
    int ret = -1;

    memcpy(buf, dir_path, path_len);
    if(access(buf, R_OK) != 0)
    {
        if(mkdir(buf, 0755)!= 0)
            ret = -1;
        else
            ret =  0;
    }
    else
        ret =  1;
    free(buf);
    buf = NULL;
    return ret;
}

static void log_close_file(struct log_handle *handle)
{
	pthread_mutex_lock(&handle->mutex);
	if(handle->fp != NULL)
	{
		fclose(handle->fp);
		handle->fp = NULL;
	}
	pthread_mutex_unlock(&handle->mutex);
	return;
}

int log_open_file(char *file_name, struct log_handle *handle)
{
	FILE *fp = NULL;
	log_close_file(handle);
	if(NULL == (fp = fopen(file_name, "a")))
	{
		return -1;
	}
    memcpy(handle->runtime_log_fn, file_name, strlen(file_name));
    handle->fp = fp;
	return 0;
}

static int log_create_path(const char *file_path)
{
    FILE *fp  = NULL;

	if(file_path == NULL)
		return 0;

    char *p_path = rindex(file_path, '/');
    if(p_path==0)
    {
        return 0;
    }

    const char *p_cur = file_path;
    int path_len = p_path - file_path;
    int i = 0;

    if(log_create_dir(file_path, path_len) >= 0)
        return 0;

    for(;i<=path_len;i++,p_cur++)
    {
        if(*p_cur == '/')
        {
            if(log_create_dir(file_path, i+1) < 0)
                return -1;
        }
    }
    if(NULL == (fp = fopen(file_path, "w")))
    {
		return 0;
    }
    fclose(fp);
    return 1;
}

int log_create_log_file(struct log_handle *handle)
{
    time_t t;
    struct tm local_time;
    char tmp_log_file_name[1024+128];

    time(&t);
    if(NULL == (localtime_r(&t, &local_time)))
    {
        return 0;
    }
    snprintf(tmp_log_file_name, sizeof(tmp_log_file_name), "%s.%04d-%02d-%02d", handle->defined_log_fn, local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday);

    if(handle->fp == NULL)
    {
	    if(0 != log_open_file(tmp_log_file_name, handle)) return 0;
    }
    else
    {
	    if (0 != memcmp(tmp_log_file_name, handle->runtime_log_fn, strlen(tmp_log_file_name)))
	    {
		    if(0 != log_open_file(tmp_log_file_name, handle))return 0;
	    }
    }

    return 1;
}

static void log_print_file(struct log_handle *handle, int level, const char *module, va_list ap, const char *fmt)
{
    char buf[64]={0};
	time_t t;
    struct tm local_time;
	const char *level_str_map[]= {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

	time(&t);
    if(NULL == (localtime_r(&t, &local_time))) return;
    snprintf(buf, sizeof(buf), "%s %s %d %02d:%02d:%02d %d", weekday_str[local_time.tm_wday],
             month_str[local_time.tm_mon], local_time.tm_mday, local_time.tm_hour, local_time.tm_min, local_time.tm_sec, local_time.tm_year+1900);

    log_create_log_file(handle);
    fprintf(handle->fp, "%s, %s, %s, ", buf, level_str_map[level], module);

    vfprintf(handle->fp, fmt, ap);
    fprintf(handle->fp, "\n");
    fflush(handle->fp);
}

static void log_print_console(struct log_handle *handle, int level, const char *module, va_list ap, const char *fmt)
{
    char buf[64]={0};
	time_t t;
    struct tm local_time;
	const char *level_str_map[]= {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

	time(&t);
    if(NULL == (localtime_r(&t, &local_time))) return;
    snprintf(buf, sizeof(buf), "%s %s %d %02d:%02d:%02d %d", weekday_str[local_time.tm_wday],
             month_str[local_time.tm_mon], local_time.tm_mday, local_time.tm_hour, local_time.tm_min, local_time.tm_sec, local_time.tm_year+1900);
    fprintf(handle->fp, "%s, %s, %s, ", buf, level_str_map[level], module);

    vfprintf(handle->fp, fmt, ap);
    fprintf(handle->fp, "\n");
    fflush(handle->fp);
}

void log_options_set_level(struct log_handle * handle, int level)
{
    if(handle != NULL)
    {
        handle->level = level;
    }
}

void log_options_set_enable(struct log_handle * handle, int enable)
{
    if(handle != NULL)
    {
        handle->enable = enable;
    }
}

struct log_handle *log_handle_create(const char *file_path, int level)
{
    struct log_handle *handle = ALLOC(struct log_handle, 1);
    if(!handle)
    {
        return NULL;
    }
	handle->enable=1;
    handle->level = level;
    strncpy(handle->defined_log_fn, file_path, 1024);
    pthread_mutex_init(&handle->mutex,NULL);

    if(handle->enable)
    {
        log_create_path(handle->defined_log_fn);
    }

    return handle;
}

void log_handle_destroy(struct log_handle * handle)
{
    if(!handle)
    {
        return;
    }

    if(handle->iface == LOG_OP_IFACE_FILE && handle->fp != NULL)
    {
        fclose(handle->fp);
        handle->fp=NULL;
    }

    pthread_mutex_destroy(&(handle->mutex));
    free(handle);
    handle = NULL;
    return;
}

void log_print(struct log_handle *handle, int level, const char *module, const char *fmt, ...)
{
    va_list ap;

    if(handle->enable != 1 && level >= handle->level)
    {
        handle->fp = stdout;
        handle->iface = LOG_OP_IFACE_CONSOLE;
        va_start(handle->ap, fmt);
        log_print_console(handle, level, module, ap, fmt);
        va_end(handle->ap);
    }
    if (handle->enable == 1 && level >= handle->level)
    {
        handle->iface = LOG_OP_IFACE_FILE;
        va_start(ap, fmt);
        log_print_file(handle, level, module, ap, fmt);
        va_end(ap);
    }
}

