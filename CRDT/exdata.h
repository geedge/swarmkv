#pragma once

typedef void * (*exdata_new_cb)(void *arg);
typedef void (*exdata_free_cb)(void *exdata);
typedef void (*exdata_merge_cb)(void *dest, void *src);
typedef void (*exdata_reset_cb)(void *exdata);
typedef void * (*exdata_copy_cb)(void *exdata);
