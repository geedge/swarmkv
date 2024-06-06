#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <sys/time.h>
#include <stddef.h>

uint32_t *hlls_create_register(int precision, int sheet_size);
void hlls_free_register(uint32_t *registers);
uint32_t *hlls_duplicate(const uint32_t *registers, unsigned char precision, int sheet_size);
int hlls_add_hash(uint32_t *registers, unsigned char precision, int sheet_size, uint64_t hash, int bucket_index);
double hlls_count(const uint32_t *registers, int bucket_index, unsigned char precision, int sheet_size, long long reset_idx, long long time_window_ms);
size_t hlls_size(unsigned char precision, int sheet_size);
int hlls_should_slide(unsigned char precision, long long time_window_ms, struct timeval now, struct timeval reset_time);
void hlls_reset_registers(uint32_t *registers, unsigned char precision, int sheet_size, long long offset, long long count);
long long hlls_get_reset_register_count(unsigned char precision, long long time_window_ms, struct timeval now, struct timeval *reset_time);
void hlls_advance_reset_index(long long *reset_idx, long long reset_reg_count, unsigned char precision);
void hlls_merge(uint32_t *dst_registers, const uint32_t *src_registers, unsigned char precision, int sheet_size);

#ifdef __cplusplus
}
#endif