#include "buffer.h"
#include <errno.h>
#include<cassert>
#include <cstddef>
#include <cstdint>
#include<cstdlib>
#include<cstring>
#include <stdio.h>




// void buf_init(Buffer *buf, size_t cap);
// void buf_free(Buffer *buf);
// size_t buf_size(const Buffer *buf);
// void buf_append(Buffer *buf, const uint8_t *data, size_t len);
// void buf_consume(Buffer *buf, size_t n);
static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}
void buf_init(Buffer *buf, size_t cap){
    buf->buffer_begin = (uint8_t *)malloc(cap);
    buf->buffer_end = buf->buffer_begin + cap;
    buf->data_begin = buf->buffer_begin;
    buf->data_end = buf->buffer_begin;
}
void buf_free(Buffer *buf){
    free(buf->buffer_begin);
    buf->buffer_begin = nullptr;
    buf->buffer_end = nullptr;
    buf->data_begin = nullptr;
    buf->data_end = nullptr; 
}
static size_t buf_cap(const Buffer *buf){
    return (size_t)(buf->buffer_end - buf->buffer_begin);    
}
static size_t buf_free_back(const Buffer *buf){
    return (size_t)(buf->buffer_end - buf->data_end);
}
static size_t buf_free_front(const Buffer *buf){
    return (size_t)(buf->data_begin - buf->buffer_begin);
}
size_t buf_size(const Buffer *buf){
    return (size_t)(buf->data_end - buf->data_begin);
}

static void buf_compact(Buffer *buf){
    size_t used = buf_size(buf);
    
    if (used == 0) {
        buf->data_begin = buf->buffer_begin;
        buf->data_end = buf->buffer_begin;
        return;
    }
    if(buf->buffer_begin != buf->data_begin){
        memmove(buf->buffer_begin, buf->data_begin, used);
    }
    buf->data_begin = buf->buffer_begin;
    buf->data_end = buf->data_begin + used;
}
static void buf_grow(Buffer *buf, size_t len){
    size_t used = buf_size(buf);
    size_t cap = buf_cap(buf);
    size_t new_cap = cap ? cap * 2 : 64; 
    while(new_cap < (len + used) ){
        new_cap *= 2;
    }
    uint8_t *new_begin = (uint8_t *)malloc(new_cap);
    if(!new_begin) die("malloc");
    if(used > 0){
        memcpy(new_begin, buf->data_begin, used);
    }
    free(buf->buffer_begin);
    buf->buffer_begin = new_begin;
    buf->buffer_end = new_begin + new_cap;
    buf->data_begin = new_begin;
    buf->data_end = new_begin + used;
} 
void buf_append(Buffer *buf, const uint8_t *data, size_t len){
    size_t free_back = buf_free_back(buf);
    size_t free_front = buf_free_front(buf);

    if(free_back >= len){
        memcpy(buf->data_end, data, len);
        buf->data_end += len;
        return;
    } else if((free_back + free_front) >= len){
        buf_compact(buf);
    }else{
        buf_grow(buf, len);
    }
    memcpy(buf->data_end, data, len);
    buf->data_end += len;
}
void buf_consume(Buffer *buf, size_t n){
    assert(n <= buf_size(buf));
    buf->data_begin += n;
    if (buf->data_begin == buf->data_end) {
        buf->data_begin = buf->buffer_begin;
        buf->data_end = buf->buffer_begin;
    }
}