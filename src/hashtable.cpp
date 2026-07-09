#include "hashtable.h"
#include <assert.h>
#include <cstddef>
#include <cstdlib>
#include <stdlib.h> // calloc(), free()

static void h_init(HTab *htab, size_t n) {
  assert(n > 0 && ((n - 1) & n) == 0);
  htab->tab = (HNode **)calloc(n, sizeof(HNode *));
  htab->mask = n - 1;
  htab->size = 0;
}

static void h_insert(HTab *htab, HNode *node){
  size_t pos = htab->mask & node->hcode;
  node->next = htab->tab[pos];
  htab->tab[pos] = node;
  htab->size++;

}


static HNode **h_lookup(HTab *htab, HNode *key, bool (*eq)(HNode *, HNode *)){
  if(!htab->tab){
    return NULL;
  }
  size_t pos = htab->mask & key->hcode;
  HNode **from = &htab->tab[pos]; // address of the first element in the tab array
  for(HNode *cur; (cur = *from) != NULL; from = &cur->next){
    if(cur->hcode == key->hcode && eq(cur, key)){
      return from;
    }
  }
  return NULL;

}


static HNode *h_detach(HTab *htab, HNode **from) {
  HNode *node = *from;
  // the target node
  *from = node->next;
  // update the incoming pointer to the target
  htab->size--;
  return node;
}

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
  HNode **from = h_lookup(&hmap->tab, key, eq);
  return from ? *from : NULL;
}

void hm_insert(HMap *hmap, HNode *node) {
  if (!hmap->tab.tab) {
    h_init(&hmap->tab, 4);
  }
  h_insert(&hmap->tab, node);
}

HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
  HNode **from = h_lookup(&hmap->tab, key, eq);
  if (from) {
    return h_detach(&hmap->tab, from);
  }
  return NULL;
}

void hm_clear(HMap *hmap) {
  free(hmap->tab.tab);
  hmap->tab = HTab{};
}

size_t hm_size(HMap *hmap) {
  return hmap->tab.size;
}

uint64_t str_hash(const uint8_t *data, size_t len){
  uint64_t h =  0x811C9DC5; 
  for(size_t i=0; i<len; i++){
      h = (h + data[i]) * 0x01000193; 
  }
  return h;
}