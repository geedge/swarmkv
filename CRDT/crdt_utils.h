#pragma once

#include <stdlib.h>	//calloc
#include <sys/time.h>//gettimeofday
#include <stdint.h>	//uint64_t

#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))
#define	FREE(p)	{free(*p);*p=NULL;}

#define	TRUE	1
#define	FALSE	0

#ifndef MAX
#define MAX(a, b)  	(((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b)  	(((a) < (b)) ? (a) : (b))
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({			 \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif
#define timeval_delta_ms(start, end) (((end).tv_sec-(start).tv_sec)*1000 + ((end).tv_usec-(start).tv_usec)/1000)
#define timeval_delta_us(start, end) (((end).tv_sec-(start).tv_sec)*1000*1000 + ((end).tv_usec-(start).tv_usec))
#define	timeval_to_ms(t)	((t).tv_sec*1000+(t).tv_usec/1000)
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)



// Double hashing for fast hashing, 
// reference: Kirsch, Adam, and Michael Mitzenmacher. "Less hashing, same performance: Building a better bloom filter." 
// Algorithms–ESA 2006: 14th Annual European Symposium, Zurich, Switzerland, September 11-13, 2006. Proceedings 14. Springer Berlin Heidelberg, 2006.
// https://www.eecs.harvard.edu/~michaelm/postscripts/rsa2008.pdf 
struct double_hash
{
    uint64_t a;
    uint64_t b;
};
void double_hash_init(struct double_hash *rv, const void *buffer, int len);
int double_hash_generate(const struct double_hash *rv, int i, int m);



struct serializer_reader;

struct serializer_reader *serializer_reader_new();
void serializer_reader_finalize(struct serializer_reader *reader, char **blob, size_t *size);
const char *serializer_reader_unwrap(const struct serializer_reader *reader, size_t *size);

void serializer_reader_read_uint(struct serializer_reader *reader, uint32_t value);
void serializer_reader_read_longlong(struct serializer_reader *reader, uint64_t value);
void serializer_reader_read_double(struct serializer_reader *reader, double value);
void serializer_reader_read_bin(struct serializer_reader *reader, const char *value, size_t size);
void serializer_reader_read_nil(struct serializer_reader *reader);
void serializer_reader_read_str(struct serializer_reader *reader, const char *value);
void serializer_reader_start_bin_array(struct serializer_reader *reader, size_t size);
void serializer_reader_read_int_array(struct serializer_reader *reader, const int *value, size_t size); 


/* ------------------------------------------------------------------------ */
struct serializer_writer;
struct serializer_writer *serializer_writer_new(const char *blob, size_t size);
void serializer_writer_free(struct serializer_writer *writer);

// for all below, return -1 if error
int serializer_writer_write_uint(struct serializer_writer *writer, uint32_t *value);
int serializer_writer_write_longlong(struct serializer_writer *writer, long long *value);
int serializer_writer_write_double(struct serializer_writer *writer, double *value);
int serializer_writer_write_bin(struct serializer_writer *writer, char **value, size_t *size);
int serializer_writer_expect_nil(struct serializer_writer *writer); // return 0 if not nil. return 1 if nil, and move cursor.
int serializer_writer_write_str(struct serializer_writer *writer, char **value);
int serializer_writer_write_bin_array(struct serializer_writer *writer, char ***array, size_t *n_arr, size_t **arr_len);
int serializer_writer_write_int_array(struct serializer_writer *writer, int **value, size_t *size);