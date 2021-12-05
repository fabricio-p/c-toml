#ifndef CAKE_UTIL_H
#define CAKE_UTIL_H
#include <stdio.h>
#include <stdbool.h>
#include <c-string/lib.h>
#include "lib.h"
#define __inline__ static inline __attribute__((always_inline)) \
                   __attribute__((unused))
#undef bool

typedef _Bool bool;

__inline__
StringBuffer read_file(char *path, int *size_p)
{
  FILE *fp = fopen(path, "r");
  if (fp == NULL)
    return NULL;
  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  StringBuffer text = StringBuffer_with_length(size + 1);
  fread(text, size, 1, fp);
  text[size] = '\0';
  fclose(fp);
  *size_p = size;
  return text;
}
#define in_range(c, x, y) (((c) >= (x)) && ((c) <= (y)))

#define is_letter(c)                              \
  (in_range(c, 'A', 'Z') || in_range(c, 'a', 'z'))

#define is_digit(c) in_range(c, '0', '9')

#define is_hex(c)                                                 \
  (is_digit(c) || in_range(c, 'A', 'F') || in_range(c, 'a', 'f'))

#define is_empty(c)                                         \
  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\0')

#endif /* CAKE_UTIL_H */
