#include <xxhash.h>
#include "lib.h"

typedef struct {
#define headerof(table)                                     \
  ((Header *)((void *)(table) - offsetof(Header, entries)))
  int       size;
  int       count;
  TOMLEntry entries[1];
} Header;

int expand(TOMLTable *);

TOMLTable TOMLTable_with_size(int size)
{
  TOMLTable table = NULL;
  Header *header = malloc(
                          offsetof(Header, entries) +
                          sizeof(TOMLEntry) * size
                         );
  if (header != NULL)
  {
    memset(header->entries, '\0', sizeof(TOMLEntry) * size);
    header->size = size;
    header->count = 0;
    table = &(header->entries[0]);
  }
  return table;
}

int TOMLTable_set(TOMLTable *table, char *key, TOMLValue *value)
{
  int status = TOML_TABLE_OK;
  Header *header = headerof(*table);
  if (header->count * 3 >= header->size)
  {
    if (expand(table) != TOML_TABLE_OK)
      return TOML_TABLE_NOMEM;
  }
  int len = strlen(key);
  XXH32_hash_t hash = XXH32(key, len, 0);
  int index = hash % header->size;
  for (
       TOMLEntry *entry = &((*table)[index]),
                 *const end = &((*table)[header->size]);
       entry < end; ++entry
      )
  {
    if (entry->key == NULL && entry->value.kind == 0)
    {
      ++(header->count);
      entry->key = key;
      // memcpy bad
      entry->value.kind = value->kind;
      // bc max width is 8 bytes and float_ is the only one guaranteed
      // to be of that size
      entry->value.float_ = value->float_;
      break;
    } else if (strcmp(entry->key, key) == 0)
    {
      status = TOML_TABLE_OVERR;
      entry->value.kind = value->kind;
      entry->value.float_ = value->float_;
      break;
    }
  }
  return status;
}

TOMLValue *TOMLTable_get(TOMLTable table, char *key)
{
  TOMLValue *value = NULL;
  Header *header = headerof(table);
  int len = strlen(key);
  XXH32_hash_t hash = XXH32(key, len, 0);
  int index = hash % header->size;
  for (
       TOMLEntry *entry = &(table[index]),
                 *const end = &(table[header->size]);
       entry < end; ++entry
      )
  {
    if (entry->key == NULL && entry->value.kind == 0)
    {
      break;
    } else if (strcmp(entry->key, key) == 0)
    {
      value = &(entry->value);
      break;
    }
  }
  return value;
}

int expand(TOMLTable *table)
{
  int status = TOML_TABLE_OK;
  Header *header = headerof(*table);
  TOMLTable new_table = TOMLTable_with_size(header->size * 2);
  if (new_table == NULL)
  {
    status = TOML_TABLE_NOMEM;
  } else
  {
    for (
         TOMLEntry *entry = &((*table)[0]),
                   *const end = &((*table)[header->size]);
         entry < end; ++entry
        )
    {
      if (entry->key == NULL && entry->value.kind == 0)
        continue;
      TOMLTable_set(&new_table, entry->key, &(entry->value));
    }
    TOMLTable_cleanup(*table);
    *table = new_table;
  }
  return status;
}

void TOMLTable_destroy(TOMLTable table)
{
  int const size = *((int *)table - 2);
  int const used = *((int *)table - 1);

  for (int i = 0, destroyed = 0; i < size && destroyed < used; ++i)
  {
    TOMLEntry *entry = &(table[i]);
    if (entry->key != NULL)
    {
      ++destroyed;
      String_cleanup(entry->key);
      TOMLValue_destroy(&(entry->value));
    }
  }

  TOMLTable_cleanup(table);
}
