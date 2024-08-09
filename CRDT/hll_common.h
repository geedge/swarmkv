#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <sys/time.h>//gettimeofday
#include <stddef.h>

uint32_t *hll_create_register(int precision);
void hll_free_register(uint32_t *registers);
uint32_t *hll_duplicate(const uint32_t *registers, unsigned char precision);
int hll_add_hash(uint32_t *registers, unsigned char precision, uint64_t hash);
double hll_count(const uint32_t *registers, unsigned char precision, long long reset_idx, long long time_window_ms);
size_t hll_size(unsigned char precision);
double hll_error(unsigned char precision);
int hll_should_slide(unsigned char precision, long long time_window_ms, struct timeval now, struct timeval reset_time);
void hll_reset_registers(uint32_t *registers, unsigned char precision, long long offset, long long count);
long long hll_get_reset_register_count(unsigned char precision, long long time_window_ms, struct timeval now, struct timeval *reset_time);
void hll_advance_reset_index(long long *reset_idx, long long reset_reg_count, unsigned char precision);
void hll_merge(uint32_t *dst_registers, const uint32_t *src_registers, unsigned char precision);

#ifdef __cplusplus
}
#endif