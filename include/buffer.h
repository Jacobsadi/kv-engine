#pragma once

#include <cstddef>
#include <cstdint>

struct Buffer {
    uint8_t *buffer_begin;
    uint8_t *buffer_end;
    uint8_t *data_begin;
    uint8_t *data_end;
};

void buf_init(Buffer *buf, size_t cap);
void buf_free(Buffer *buf);
size_t buf_size(const Buffer *buf);
void buf_append(Buffer *buf, const uint8_t *data, size_t len);
void buf_consume(Buffer *buf, size_t n);