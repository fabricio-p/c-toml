#ifndef __TOML_TOMLTABLE_H__
#define __TOML_TOMLTABLE_H__
#include <stddef.h>
#ifndef C_TOML_H
#include "lib.h"
#endif

struct TOMLTable_Bucket {
  TOMLValue value;
  String    key;
  uint32_t  hash;
  uint8_t   __padd[sizeof(int) == 4 ? 4 : 0];
};

typedef struct TOMLTable_Header {
  int              size;
  int              count;
  TOMLTable_Bucket items[1];
} TOMLTable_Header;

// typedef TOMLTable_Bucket *TOMLTable;

TOMLTable TOMLTable_with_size(int);
#define TOMLTable_new() TOMLTable_with_size(0)
TOMLValue const *TOMLTable_get(TOMLTable, String);
TOMLValue *TOMLTable_put_extra(TOMLTable *, String, int);
#define TOMLTable_put(hmap, key) TOMLTable_put_extra(hmap, key, 1)
int TOMLTable_insert(TOMLTable *, String, TOMLValue const *);
int TOMLTable_has_key(TOMLTable, String);
int TOMLTable_pop(TOMLTable, String, TOMLValue *);
void TOMLTable_destroy(TOMLTable);
#define TOMLTable_delete(hmap, key) TOMLTable_pop(hmap, key, NULL)
#define TOMLTable_cleanup(hmap)                              \
  free(hmap == NULL ? NULL :                              \
       ((void *)(hmap) - offsetof(TOMLTable_Header, items)))
#define TOMLTable_size(t)                                       \
  (((TOMLTable_Header *)                                        \
    (((void *)(t)) - offsetof(TOMLTable_Header, items)))->size)
#define TOMLTable_count(t)                                      \
  (((TOMLTable_Header *)                                        \
    (((void *)(t)) - offsetof(TOMLTable_Header, items)))->count)

TOMLTable_Bucket *get_bucket_(TOMLTable hmap, String key,
                                    uint32_t *hash_p);


#endif /* __TOML_TOMLTABLE_H__ */
