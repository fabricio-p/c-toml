#include <string.h>
#include <malloc.h>
#include <xxhash.h>
#include "table.h"

TOMLTable TOMLTable_with_size(int count)
{
  int size = offsetof(TOMLTable_Header, items) +
             count * sizeof(TOMLTable_Bucket);
  TOMLTable_Header *hdr = malloc(size);
  memset(hdr, '\0', size);
  TOMLTable hmap = NULL;
  if (hdr != NULL)
  {
    hmap = &(hdr->items[0]);
    hdr->size = count;
  }
  return hmap;
}

#define TOMLTable_header(m)           \
  ((TOMLTable_Header *)((void *)(m) - \
    offsetof(TOMLTable_Header, items)))

static int TOMLTable_expand(TOMLTable *hmap_p)
{
  int status = 0;
  TOMLTable_Header *hdr = TOMLTable_header(*hmap_p);
  int new_size = (hdr->size == 0 ? 2 : hdr->size) * 2;
  TOMLTable new_map = TOMLTable_with_size(new_size);
  for (TOMLTable_Bucket *offset = *hmap_p, *end = &((*hmap_p)[hdr->size]);
       offset < end; ++offset)
  {
    if (offset->key != NULL)
    {
      if ((status = TOMLTable_insert(&new_map, offset->key,
                                     &(offset->value))))
      {
        TOMLTable_cleanup(new_map);
        return status;
      }
    }
  }
  TOMLTable_cleanup(*hmap_p);
  *hmap_p = new_map;
  return status;
}

static TOMLTable_Bucket *get_bucket(TOMLTable hmap, String key,
                                    uint32_t *hash_p)
{
  int size = TOMLTable_size(hmap);
  if (size == 0)
  {
    return NULL;
  }
  TOMLTable_Bucket *end = &(hmap[size]);
  uint32_t hash = XXH32(key, strlen(key), 0);
  size_t index = hash % size;
  TOMLTable_Bucket *offset = &(hmap[index]);
  if (hash_p != NULL)
  {
    *hash_p = hash;
  }
  for (; offset < end; ++offset)
  {
    if (!offset->hash ||
        (offset->hash == hash && strcmp(key, offset->key) == 0))
    {
      return offset;
    }
  }
  return NULL;
}

TOMLTable_Bucket *get_bucket_(TOMLTable hmap, String key,
                             uint32_t *hash_p)
{
  return get_bucket(hmap, key, hash_p);
}


TOMLValue const *TOMLTable_get(TOMLTable hmap, String key)
{
  TOMLTable_Bucket *bucket = get_bucket(hmap, key, NULL);
  return bucket == NULL || !bucket->value.kind ? NULL : &(bucket->value);
}

TOMLValue *TOMLTable_put_extra(TOMLTable *hmap_p, String key, int store)
{
  TOMLTable_Bucket *bucket = NULL;
  TOMLValue *val_p = NULL;
  uint32_t hash;
  TOMLTable_Header *hdr;
  do {
    // TODO: Prevent infinite loop
    bucket = get_bucket(*hmap_p, key, &hash);
    hdr = TOMLTable_header(*hmap_p);
  } while ((bucket == NULL || (hdr->count * 2) > hdr->size) &&
            TOMLTable_expand(hmap_p) == 0);
  if (bucket != NULL && (hdr->count * 2) <= hdr->size)
  {
    if (!bucket->hash && store)
    {
      bucket->hash = hash;
      bucket->key = key;
      ++(hdr->count);
      bucket->value.float_ = 0;
    }
    val_p = &(bucket->value);
  }
  return val_p;
}

int TOMLTable_insert(TOMLTable *hmap_p, String key,
                     TOMLValue const *val_ps)
{
  TOMLValue *val_pd = TOMLTable_put_extra(hmap_p, key, 0);
  TOMLTable_Bucket *bucket = (void *)val_pd -
                             offsetof(TOMLTable_Bucket, value);
  int status = 0;
  if (val_pd != NULL)
  {
    if (bucket->hash)
    {
      status = 1;
    }
    *val_pd = *val_ps;
  } else
  {
    status = -1;
  }
  return status;
}

int TOMLTable_has_key(TOMLTable hmap, String key)
{
  uint32_t hash;
  TOMLTable_Bucket *bucket = get_bucket(hmap, key, &hash);
  return bucket != NULL && bucket->hash == hash &&
         strcmp(key, bucket->key) == 0;
}

int TOMLTable_pop(TOMLTable hmap, String key, TOMLValue *val_p)
{
  int status = 0;
  uint32_t hash;
  TOMLTable_Bucket *bucket = get_bucket(hmap, key, &hash);
  if (bucket != NULL)
  {
    if (val_p != NULL)
    {
      memcpy(val_p, &(bucket->value), sizeof(TOMLValue));
    }
    TOMLTable_Bucket *c = bucket + 1;
    for (TOMLTable_Bucket *end = &(hmap[TOMLTable_header(hmap)->size]);
         c < end && c->hash && c->hash == hash; ++c);
    memcpy(bucket, bucket + 1, (int)((void *)c - (void *)bucket));
    memset(bucket - 1, '\0', sizeof(*bucket));
  } else
  {
    status = -1;
  }
  return status;
}

void TOMLTable_destroy(TOMLTable table)
{
  int const size = TOMLTable_header(table)->size;
  int const used = TOMLTable_header(table)->count;

  for (int i = 0, destroyed = 0; i < size && destroyed < used; ++i)
  {
    TOMLTable_Bucket *entry = &(table[i]);
    if (entry->key != NULL)
    {
      ++destroyed;
      String_cleanup(entry->key);
      TOMLValue_destroy(&(entry->value));
    }
  }

  TOMLTable_cleanup(table);
}
