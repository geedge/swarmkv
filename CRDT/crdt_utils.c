#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "crdt_utils.h"
#include "xxhash.h"

void double_hash_init(struct double_hash *rv, const void *buffer, int len)
{
    rv->a=XXH3_64bits_withSeed(buffer, len, 0x9747b28c);
    rv->b=XXH3_64bits_withSeed(buffer, len, rv->a);
	return;
}
int double_hash_generate(const struct double_hash *rv, int i, int m)
{
    return (rv->a + i * rv->b) % m;
}

/* -------------------------------------------------------------------------- */
/*                                 serializer                                 */
/* -------------------------------------------------------------------------- */
#define MALLOC_INIT 1024
struct serializer_reader {
    char *data;
    size_t cursor;

    size_t data_buffer_size;
};

struct serializer_writer {
    const char *data;
    size_t cursor;
    size_t data_buffer_size;
};

enum serializer_data_type {
    FS_DATA_TYPE_UINT,
    FS_DATA_TYPE_LONGLONG,
    FS_DATA_TYPE_DOUBLE,
    FS_DATA_TYPE_BIN,
    FS_DATA_TYPE_STR,
    FS_DATA_TYPE_ARRAY,
    FS_DATA_TYPE_INT_ARR,
    FS_DATA_TYPE_NIL,
};


/* --------------------------------- reader --------------------------------- */

void serializer_reader_check_and_realloc(struct serializer_reader *reader, size_t size) {
    if (reader->cursor + size < reader->data_buffer_size) {
        return;
    }

    while (reader->data_buffer_size <= reader->cursor + size) {
        reader->data_buffer_size *= 2;
        reader->data = realloc(reader->data, reader->data_buffer_size);
    }
}

void serializer_reader_pad(struct serializer_reader *reader) {
    if (reader->cursor % 4 != 0) {
        reader->cursor += 4 - (reader->cursor % 4);
        serializer_reader_check_and_realloc(reader, 0);
    }
}

void serializer_writer_pad(struct serializer_writer *writer) {
    if (writer->cursor % 4 != 0) {
        writer->cursor += 4 - (writer->cursor % 4);
    }
}

void serializer_reader_read_uint(struct serializer_reader *reader, uint32_t value) {
    serializer_reader_check_and_realloc(reader, sizeof(uint32_t) + sizeof(uint32_t));
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (FS_DATA_TYPE_UINT);
    reader->cursor += sizeof(uint32_t);
    *(uint32_t *) (reader->data + reader->cursor) = value;
    reader->cursor += sizeof(uint32_t);
}

void serializer_reader_read_longlong(struct serializer_reader *reader, uint64_t value)
{
    serializer_reader_check_and_realloc(reader, sizeof(uint32_t) + sizeof(uint64_t));
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (FS_DATA_TYPE_LONGLONG);
    reader->cursor += sizeof(uint32_t);
    *(uint64_t *) (reader->data + reader->cursor) = value;
    reader->cursor += sizeof(uint64_t);
}

void serializer_reader_read_double(struct serializer_reader *reader, double value) {
    serializer_reader_check_and_realloc(reader, sizeof(uint32_t) + sizeof(double));
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (FS_DATA_TYPE_DOUBLE);
    reader->cursor += sizeof(uint32_t);
    *(double *) (void *)(reader->data + reader->cursor) = value;
    reader->cursor += sizeof(double);
}

void serializer_reader_read_bin(struct serializer_reader *reader, const char *value, size_t size) {
    serializer_reader_check_and_realloc(reader, size + sizeof(uint32_t) + sizeof(uint32_t));
    
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (FS_DATA_TYPE_BIN);
    reader->cursor += sizeof(uint32_t);
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (size);
    reader->cursor += sizeof(uint32_t);
    memcpy(reader->data + reader->cursor, value, size);
    reader->cursor += size;
    serializer_reader_pad(reader);
}

void serializer_reader_read_nil(struct serializer_reader *reader) {
    serializer_reader_check_and_realloc(reader, sizeof(uint32_t));
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (FS_DATA_TYPE_NIL);
    reader->cursor += sizeof(uint32_t);
}

void serializer_reader_read_str(struct serializer_reader *reader, const char *value) {
    serializer_reader_check_and_realloc(reader, strlen(value) + sizeof(uint32_t) + sizeof(uint32_t));
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (FS_DATA_TYPE_STR);
    reader->cursor += sizeof(uint32_t);
    uint32_t slen = (uint32_t) (strlen(value));
    *(uint32_t *) (reader->data + reader->cursor) = slen;
    reader->cursor += sizeof(uint32_t);
    memcpy(reader->data + reader->cursor, value, slen);
    reader->cursor += strlen(value);
    serializer_reader_pad(reader);
}

void serializer_reader_start_bin_array(struct serializer_reader *reader, size_t size) {
    serializer_reader_check_and_realloc(reader, sizeof(uint32_t) + sizeof(uint32_t));
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (FS_DATA_TYPE_ARRAY);
    reader->cursor += sizeof(uint32_t);
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (size);
    reader->cursor += sizeof(uint32_t);
}

void serializer_reader_read_int_array(struct serializer_reader *reader, const int *value, size_t size) {
    size_t arr_v_len = size * sizeof(uint32_t);
    serializer_reader_check_and_realloc(reader, arr_v_len + sizeof(uint32_t) + sizeof(uint32_t));

    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (FS_DATA_TYPE_INT_ARR);
    reader->cursor += sizeof(uint32_t);
    *(uint32_t *) (reader->data + reader->cursor) = (uint32_t) (size);
    reader->cursor += sizeof(uint32_t);
    memcpy(reader->data + reader->cursor, value, arr_v_len);
    reader->cursor += arr_v_len;
}

struct serializer_reader *serializer_reader_new() {
    struct serializer_reader *reader = malloc(sizeof(struct serializer_reader));
    reader->data = malloc(MALLOC_INIT);
    reader->cursor = 0;
    reader->data_buffer_size = MALLOC_INIT;
    return reader;
}

const char *serializer_reader_unwrap(const struct serializer_reader *reader, size_t *size) {
    *size = reader->cursor;
    return reader->data;
}

void serializer_reader_finalize(struct serializer_reader *reader, char **blob, size_t *size) {
    *size = reader->cursor;
    *blob = reader->data;
    free(reader);
}

/* --------------------------------- writer --------------------------------- */

struct serializer_writer *serializer_writer_new(const char *blob, size_t size) {
    struct serializer_writer *writer = malloc(sizeof(struct serializer_writer));
    writer->data = blob;
    writer->cursor = 0;
    writer->data_buffer_size = size;
    return writer;
}

void serializer_writer_free(struct serializer_writer *writer) {
    free(writer);
}

int serializer_writer_write_uint(struct serializer_writer *writer, uint32_t *value) {
    if (writer->cursor + sizeof(uint32_t) + sizeof(uint32_t) > writer->data_buffer_size) {
        return -1;
    }

    enum serializer_data_type type = *(enum serializer_data_type *) (writer->data + writer->cursor);
    if (type != FS_DATA_TYPE_UINT) {
        return -1;
    }
    writer->cursor += sizeof(uint32_t);

    *value = *(uint32_t *) (writer->data + writer->cursor);
    writer->cursor += sizeof(uint32_t);
    return 0;
}

int serializer_writer_write_longlong(struct serializer_writer *writer, long long *value) {
    if (writer->cursor + sizeof(uint32_t) + sizeof(long long) > writer->data_buffer_size) {
        return -1;
    }

    enum serializer_data_type type = *(enum serializer_data_type *) (writer->data + writer->cursor);
    if (type != FS_DATA_TYPE_LONGLONG) {
        return -1;
    }
    writer->cursor += sizeof(uint32_t);

    *value = *(long long *) (writer->data + writer->cursor);
    writer->cursor += sizeof(long long);
    return 0;
}

int serializer_writer_write_double(struct serializer_writer *writer, double *value) {
    if (writer->cursor + sizeof(uint32_t) + sizeof(double) > writer->data_buffer_size) {
        return -1;
    }

    enum serializer_data_type type = *(enum serializer_data_type *) (writer->data + writer->cursor);
    if (type != FS_DATA_TYPE_DOUBLE) {
        return -1;
    }
    writer->cursor += sizeof(uint32_t);

    *value = *(double *) (void *) (writer->data + writer->cursor);
    writer->cursor += sizeof(double);
    return 0;
}

int serializer_writer_write_bin(struct serializer_writer *writer, char **value, size_t *size) {
    if (writer->cursor + sizeof(uint32_t) + sizeof(uint32_t) > writer->data_buffer_size) {
        return -1;
    }

    enum serializer_data_type type = *(enum serializer_data_type *) (writer->data + writer->cursor);
    if (type != FS_DATA_TYPE_BIN) {
        return -1;
    }
    writer->cursor += sizeof(uint32_t);

    *size = *(uint32_t *) (writer->data + writer->cursor);
    writer->cursor += sizeof(uint32_t);

    char *ret_v = (char *)malloc(*size);
    *value = ret_v;
    memcpy(ret_v, writer->data + writer->cursor, *size);

    writer->cursor += *size;
    serializer_writer_pad(writer);
    return 0;
}

int serializer_writer_expect_nil(struct serializer_writer *writer) {// return 0 if not nil. return 1 if nil, and move cursor.
    if (writer->cursor + sizeof(uint32_t) > writer->data_buffer_size) {
        return -1;
    }

    enum serializer_data_type type = *(enum serializer_data_type *) (writer->data + writer->cursor);
    if (type != FS_DATA_TYPE_NIL) {
        return 0;
    }
    writer->cursor += sizeof(uint32_t);
    return 1;
}

int serializer_writer_write_str(struct serializer_writer *writer, char **value) {
    if (writer->cursor + sizeof(uint32_t) + sizeof(uint32_t) > writer->data_buffer_size) {
        return -1;
    }

    enum serializer_data_type type = *(enum serializer_data_type *) (writer->data + writer->cursor);
    if (type != FS_DATA_TYPE_STR) {
        return -1;
    }
    writer->cursor += sizeof(uint32_t);

    uint32_t slen = *(uint32_t *) (writer->data + writer->cursor);
    writer->cursor += sizeof(uint32_t);

    char *ret_v = (char *)malloc(slen + 1);
    *value = ret_v;
    memcpy(ret_v, writer->data + writer->cursor, slen);
    ret_v[slen] = '\0';

    writer->cursor += slen;
    serializer_writer_pad(writer);
    return 0;
}

int serializer_writer_write_bin_array(struct serializer_writer *writer, char ***array, size_t *n_arr, size_t **arr_len)
{
    if (writer->cursor + sizeof(uint32_t) + sizeof(uint32_t) > writer->data_buffer_size) {
        return -1;
    }

    enum serializer_data_type type = *(enum serializer_data_type *) (writer->data + writer->cursor);
    if (type != FS_DATA_TYPE_ARRAY) {
        return -1;
    }
    writer->cursor += sizeof(uint32_t);

    size_t ret_size = *(uint32_t *) (writer->data + writer->cursor);
    *n_arr = ret_size;
    writer->cursor += sizeof(uint32_t);

    // start write the array
    char **ret_v = (char **)malloc(ret_size * sizeof(char *));
    size_t *ret_len = (size_t *)malloc(ret_size * sizeof(size_t));
    *array = ret_v;
    *arr_len = ret_len;

    for (size_t i = 0; i < ret_size; i++) {
        if (serializer_writer_write_bin(writer, &ret_v[i], &ret_len[i]) != 0) {
            // free the alloced memory
            for (size_t j = 0; j < i; j++) {
                free(ret_v[j]);
            }
            free(ret_v);
            free(ret_len);
            
            return -1;
        }
    }

    return 0;
}

int serializer_writer_write_int_array(struct serializer_writer *writer, int **value, size_t *size) {
    if (writer->cursor + sizeof(uint32_t) + sizeof(uint32_t) > writer->data_buffer_size) {
        return -1;
    }

    enum serializer_data_type type = *(enum serializer_data_type *) (writer->data + writer->cursor);
    if (type != FS_DATA_TYPE_INT_ARR) {
        return -1;
    }
    writer->cursor += sizeof(uint32_t);

    *size = *(uint32_t *) (writer->data + writer->cursor);
    writer->cursor += sizeof(uint32_t);

    int *ret_v = (int *)malloc(*size * sizeof(int));
    *value = ret_v;
    memcpy(ret_v, writer->data + writer->cursor, *size * sizeof(int));

    writer->cursor += *size * sizeof(int);
    return 0;
}