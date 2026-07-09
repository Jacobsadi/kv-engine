#pragma once

#include <stddef.h>
#include <stdint.h>


struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0; // hash value 
};

struct HTab {
    HNode **tab = nullptr; // array od slots 
    size_t mask; // power of 2 array size 
    size_t size = 0; // number of keys 
};

// the real hashtable interface.
// it uses 2 hashtables for progressive rehashing.
struct HMap {
    HTab tab;
};

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void   hm_insert(HMap *hmap, HNode *node);
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void   hm_clear(HMap *hmap);
size_t hm_size(HMap *hmap);
uint64_t str_hash(const uint8_t *data, size_t len);