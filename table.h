#ifndef TOML_TABLE_H
#define TOML_TABLE_H

TOMLTable TOMLTable_with_size (int);
TOMLValue *TOMLTable_get      (TOMLTable, char const *);
TOMLValue *TOMLTable_set_empty(TOMLTable *, char const *);
int       TOMLTable_set       (TOMLTable *, char const *, TOMLValue *);
void      TOMLTable_destroy   (TOMLTable);

#define TOML_TABLE_OK    0
#define TOML_TABLE_NOMEM -1
#define TOML_TABLE_OVERR 1

#define TOMLTable_new() (TOMLTable_with_size(2))
#define TOMLTable_size(table) (*((int *)(table) - 2))
#define TOMLTable_count(table) (*((int *)(table) - 1))
#define TOMLTable_cleanup(table) (free((void *)(table) - sizeof(int) * 2))

#endif /* TOML_TABLE_H */
