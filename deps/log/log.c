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

#define MAX_CALLBACKS 32

#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))

typedef struct {
  log_func_t fn;
  void *user_data;
  int level;
} Callback;

struct log_handle
{
    int level;
    int enable;
    void *user_data;
    log_lock_func_t lock;
	char run_log_path[1024];
    char cur_log_file[1024];
    pthread_mutex_t mutex;
    Callback callbacks[MAX_CALLBACKS];
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

static void log_close_file(struct log_event *event)
{
	pthread_mutex_lock(&event->mutex);
    FILE *fp=(FILE *)event->user_data;
	if(fp != NULL)
	{
		fclose(fp);
		event->user_data = NULL;
	}
	pthread_mutex_unlock(&event->mutex);
	return;
}

int log_open_file(char *file_name, struct log_event *event)
{
	FILE *fp = NULL;
	log_close_file(event);
	if(NULL == (fp = fopen(file_name, "a")))
	{
		return -1;
	}
    memcpy(event->cur_log_file, file_name, strlen(file_name));
    event->user_data = fp;
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

int log_create_log_file(struct log_event *event)
{
    time_t t;
    struct tm local_time;
    char tmp_log_file_name[512];

    FILE *fp = (FILE *)event->user_data;

    time(&t);
    if(NULL == (localtime_r(&t, &local_time)))
    {
        return 0;
    }
    sprintf(tmp_log_file_name, "%s.%04d-%02d-%02d", event->run_log_path, local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday);

    if(fp == NULL)
    {
	    if(0 != log_open_file(tmp_log_file_name, event)) return 0;
    }
    else
    {
	    if (0 != memcmp(tmp_log_file_name, event->cur_log_file, strlen(tmp_log_file_name)))
	    {
		    if(0 != log_open_file(tmp_log_file_name, event))return 0;
	    }
    }

    return 1;
}

static void log_print_file(struct log_event *event)
{
    char buf[64];
    time_t t;
    struct tm local_time;
    const char *level_str_map[]= {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

	time(&t);
    if(NULL == (localtime_r(&t, &local_time))) return;
    snprintf(buf, sizeof(buf), "%s %s %d %02d:%02d:%02d %d", weekday_str[local_time.tm_wday],
             month_str[local_time.tm_mon], local_time.tm_mday, local_time.tm_hour, local_time.tm_min, local_time.tm_sec, local_time.tm_year+1900);

    log_create_log_file(event);
    fprintf((FILE *)event->user_data, "%s, %s, %s, ", buf, level_str_map[event->level], event->module);

    vfprintf((FILE *)event->user_data, event->fmt, event->ap);
    fprintf((FILE *)event->user_data, "\n");
    fflush((FILE *)event->user_data);
}

static void log_print_console(struct log_event *event)
{
    char buf[64]={0};
	time_t t;
    struct tm local_time;
	const char *level_str_map[]= {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

	time(&t);
    if(NULL == (localtime_r(&t, &local_time))) return;
    snprintf(buf, sizeof(buf), "%s %s %d %02d:%02d:%02d %d", weekday_str[local_time.tm_wday],
             month_str[local_time.tm_mon], local_time.tm_mday, local_time.tm_hour, local_time.tm_min, local_time.tm_sec, local_time.tm_year+1900);
    fprintf((FILE *)event->user_data, "%s, %s, %s, ", buf, level_str_map[event->level], event->module);

    vfprintf((FILE *)event->user_data, event->fmt, event->ap);
    fprintf((FILE *)event->user_data, "\n");
    fflush((FILE *)event->user_data);
}

void log_options_set_lock_fn(struct log_handle * handle, log_lock_func_t fn, void *user_data)
{
    struct log_handle *_handle_t = (struct log_handle *)handle;
    if(_handle_t != NULL)
    {
        _handle_t->lock = fn;
        _handle_t->user_data = user_data;
    }
}

void log_options_set_level(struct log_handle * handle, int level)
{
    struct log_handle *_handle_t = (struct log_handle *)handle;
    if(_handle_t != NULL)
    {
        _handle_t->level = level;
    }
}

void log_options_set_enable(struct log_handle * handle, int enable)
{
    struct log_handle *_handle_t = (struct log_handle *)handle;
    if(_handle_t != NULL)
    {
        _handle_t->enable = enable;
    }
}

int log_add_method_callback(void *handle, log_func_t fn, void *udata, int level)
{
    int i=0;

    struct log_handle *_handle_t = (struct log_handle *)handle;
    if(_handle_t == NULL)
    {
        return -1;
    }

    for (i = 0; i < MAX_CALLBACKS; i++)
    {
        if (!_handle_t->callbacks[i].fn)
        {
            _handle_t->callbacks[i] = (Callback) { fn, udata, level };
            return 0;
        }
    }

    return -1;
}

int log_options_add_file_fp(struct log_handle *handle, int level)
{
    int ret=0;
    FILE *fp=NULL;

    struct log_handle *_handle_t = (struct log_handle *)handle;
    if(!_handle_t)
    {
        return -1;
    }
    ret =log_create_path(_handle_t->run_log_path);
    if(ret==0)
    {
        //fp=stderr;
        //What are you doing?!
    }
    return log_add_method_callback(handle, log_print_file, fp, level);
}

int log_options_add_fp(struct log_handle * handle, FILE *fp, int level)
{
    return log_add_method_callback(handle, log_print_file, fp, level);
}

static void log_lock(void *handle)
{
    struct log_handle *_handle_t = (struct log_handle *)handle;
    if(_handle_t != NULL && _handle_t->lock)
    {
        _handle_t->lock(1, _handle_t->user_data);
    }
}

static void log_unlock(void *handle)
{
    struct log_handle *_handle_t = (struct log_handle *)handle;
    if(_handle_t != NULL && _handle_t->lock)
    {
        _handle_t->lock(0, _handle_t->user_data);
    }
}

static void log_init_event(struct log_event *event, void *user_data)
{
    event->user_data = user_data;
}

struct log_handle *log_handle_create(const char *file_path, int level)
{
    struct log_handle *_handle_t = ALLOC(struct log_handle, 1);
    if(!_handle_t)
    {
        return NULL;
    }
	_handle_t->enable=1;
    _handle_t->level = level;
    strncpy(_handle_t->run_log_path, file_path, 1024);
    pthread_mutex_init(&_handle_t->mutex,NULL);

    log_options_add_file_fp(_handle_t, level);

    return _handle_t;
}

void log_handle_destroy(struct log_handle * handle)
{
    int i=0;
    struct log_handle *_handle_t = (struct log_handle *)handle;
    if(!_handle_t)
    {
        return;
    }
    if (_handle_t->user_data != NULL)
    {
        free(_handle_t->user_data);
       _handle_t->user_data = NULL;
    }
    for(i=0; i<MAX_CALLBACKS; i++)
    {
        if(_handle_t->callbacks && _handle_t->callbacks->user_data)
        {
            fclose(_handle_t->callbacks->user_data);
		    _handle_t->callbacks->user_data = NULL;
        }
    }
    pthread_mutex_destroy(&(_handle_t->mutex));
    free(handle);
    handle = NULL;
    return;
}

void log_print(struct log_handle *handle, int level, const char *module, const char *fmt, ...)
{
    struct log_event event;
    memset(&event, 0, sizeof(event));

    struct log_handle *_handle_t = (struct log_handle *)handle;

    event.fmt=fmt;
    event.level=level;
	event.module=module;
    event.mutex=_handle_t->mutex;
    event.cur_log_file=_handle_t->cur_log_file;
    event.run_log_path=_handle_t->run_log_path;

    if(_handle_t->enable != 1 && level >= _handle_t->level)
    {
        log_init_event(&event, stderr);
        va_start(event.ap, fmt);
        log_print_console(&event);
        va_end(event.ap);
    }

    log_lock(handle);

    int i=0;
    for(i=0; i<MAX_CALLBACKS && _handle_t->callbacks[i].fn; i++)
    {
        Callback *cb = &_handle_t->callbacks[i];
        if (level >= cb->level)
        {
            log_init_event(&event, cb->user_data);
            va_start(event.ap, fmt);
            cb->fn(&event);
            va_end(event.ap);
            cb->user_data = event.user_data;
        }
    }

    log_unlock(handle);
}

