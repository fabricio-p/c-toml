#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "lib.h"
#include "util.h"

#define throw(err) { status = TOML_E_##err; goto catch; }
#define try(thing) { status = (thing); if (status) { goto catch; } }
#define try_cond(cond, err) if (!(cond)) { try(TOML_E_##err); }
#define throw_if(cond, err) try_cond(!(cond), err)
#define CASE(c) case c:
#define INDENT_SIZE 2

void collect_millis(char const **offset_p, char *buff, int n)
{
  char const *offset = *offset_p;
  char c;
  for (int i = 1; (c = *offset) && is_digit(c); ++i, ++offset)
  {
    if (i <= n)
    {
      *(buff++) = *offset;
    }
  }
  *offset_p = offset;
}

void TOMLValue_destroy(TOMLValue *val)
{
  if (val->kind == TOML_STRING)
  {
    String_cleanup(val->string);
  } else if (val->kind == TOML_ARRAY || val->kind == TOML_TABLE_ARRAY)
  {
    TOMLArray_destroy(val->array);
  } else if (val->kind == TOML_TABLE || val->kind == TOML_INLINE_TABLE)
  {
    TOMLTable_destroy(val->table);
  }
}

void TOMLArray_destroy(TOMLArray array)
{
  for (int i = 0, len = TOMLArray_len(array); i < len; ++i)
  {
    TOMLValue_destroy(&(array[i]));
  }
  TOMLArray_cleanup(array);
}

TOMLPosition TOML_position(TOMLCtx const *ctx)
{
  const char *const offset = ctx->offset;
  int line_count = 1;
  char const *line_start = ctx->content;
  for (register char *chrp = ctx->content; chrp <= offset; ++chrp)
  {
    if (*chrp == '\n')
    {
      line_start = chrp + 1;
      ++line_count;
    }
  }
  return (TOMLPosition) {
    .offset = offset - ctx->content,
    .line = line_count,
    .column = offset - line_start
  };
}

static void print_time(TOMLTime const *const time)
{
  printf("%02d:%02d:%02d", time->hour, time->min, time->sec);
  if (time->millisec != 0)
  {
    printf(".%d", time->millisec);
  }
  if (time->z[0] != '\0')
  {
    putc(time->z[0], stdout);
    if (time->z[0] != 'Z')
    {
      printf("%02d", time->z[1]);
      if (time->z[2] != -1)
      {
        printf(":%02d", time->z[2]);
      }
    }
  }
}
__inline__
void print_date(TOMLDate const *const date)
{
  printf("%4d-%2d-%2d", date->year, date->month, date->day);
}

void TOMLValue_print(TOMLValue const *value, int indent, int inner_indent)
{
#define KIND(c) CASE(TOML_##c)
#define SIMPLE(c, f, ...) KIND(c) { printf(f, __VA_ARGS__); break; }    
  int const space_count = indent * INDENT_SIZE;
  for (int i = 0; i < space_count; ++i, putc(' ', stdout));
  switch (value->kind)
  {
    SIMPLE(INTEGER, "%li", value->integer);
    SIMPLE(FLOAT,   "%lF", value->float_);
    SIMPLE(BOOLEAN, "%s",  value->boolean ? "true" : "false");
    SIMPLE(STRING,  "%s",  value->string);
    KIND(DATETIME)
    {
      print_date(&(value->datetime.date));
      print_time(&(value->datetime.time));
      break;
    }
    KIND(DATE)
    {
      print_date(&(value->date));
      break;
    }
    KIND(TIME)
    {
      print_time(&(value->time));
      break;
    }
    KIND(ARRAY)
    KIND(TABLE_ARRAY)
    {
      TOMLValue const *current = &(value->array[0]);
      TOMLValue const *const end = current + TOMLArray_len(value->array);
      puts("[");
      for (; current < end; ++current)
      {
        TOMLValue_print(current, inner_indent, inner_indent + 1);
        puts(current < end - 1 ? "," : "");
      }
      for (int i = 0; i < space_count; ++i, putc(' ', stdout));
      putc(']', stdout);
      break;
    }
    KIND(TABLE)
    KIND(INLINE_TABLE)
    {
      TOMLEntry const *current = &(value->table[0]);
      TOMLEntry const *const end = current + TOMLTable_size(value->table);
      puts("{");
      for (int inner_space = inner_indent * INDENT_SIZE; current < end;
           ++current)
      {
        if (current->key != NULL)
        {
          printf("%*s = ", inner_space, current->key);
          TOMLValue_print(&(current->value), 0, inner_indent + 1);
          puts(current < end - 1 ? "," : "");
        }
      }
      for (int i = 0; i < space_count; ++i, putc(' ', stdout));
      putc('}', stdout);
      break;
    }
  }
}

TOMLStatus TOML_parse_number(TOMLCtx *ctx, TOMLValue *value)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ctx->offset;
  char const *const end = ctx->end;
  char buffer[48];
  // TODO: Doesn't look really good. Find a better way later.
  struct {
    bool is_float;
    // if the number has exponential
    bool has_exp;
    // if we have started buffering the exponential value
    bool exp_value_started;
  } float_info = {false, false, false};
  int base = 10;
  int i = 0;
  int sign = 0;
metadata:
  switch (*offset)
  {
    CASE('0')
    {
      switch (*(++offset))
      {
        CASE('x')
        CASE('X')
        {
          ++offset;
          base = 16;
          break;
        }
        CASE('o')
        CASE('O')
        {
          ++offset;
          base = 8;
          break;
        }
        CASE('b')
        CASE('B')
        {
          ++offset;
          base = 2;
          break;
        }
        CASE('\n')
        CASE('\t')
        CASE(' ')
        CASE('\0')
        CASE('#')
        {
          value->kind = TOML_INTEGER;
          value->integer = 0;
          // we just jump at the end
          throw(OK);
        }
        default:
        {
          throw(INVALID_NUMBER);
        }
      }
      break;
    }
    CASE('+')
    {
      ++offset;
      goto metadata;
    }
    CASE('-')
    {
      ++offset;
      sign = 1;
      goto metadata;
    }
  }
  for (; i < 48 && offset < end; ++i, ++offset)
  {
    char chr = *offset;
    switch (chr)
    {
      CASE('0'...'9')
      {
        throw_if(chr > '7' && base == 8, INVALID_NUMBER);
        if (float_info.has_exp && !float_info.exp_value_started)
        {
          float_info.exp_value_started = true;
        }
        break;
      }
      CASE('a'...'f')
      CASE('A'...'F')
      {
        if ((chr == 'e' || chr == 'E') && base == 10)
        {
          throw_if(float_info.has_exp, INVALID_NUMBER);
          if (!float_info.is_float)
          {
            float_info.is_float = true;
          }
          float_info.has_exp = true;
        } else if (base != 16)
        {
          throw(INVALID_NUMBER);
        }
        break;
      }
      CASE('_')
      {
        throw_if(offset[-1] == '_', INVALID_NUMBER);
        --i;
        goto skip_buffering;
      }
      CASE('+')
      CASE('-')
      {
        throw_if(float_info.exp_value_started, INVALID_NUMBER);
        float_info.exp_value_started = true;
        break;
      }
      CASE('.')
      {
        if (base == 0)
        {
          base = 10;
        }
        throw_if(base != 10, INVALID_NUMBER);
        float_info.is_float = true;
        break;
      }
      default:
      {
        goto out;
      }
    }
    buffer[i] = chr;

skip_buffering:;
  }

out:
  // So that the string is terminated for strtol() and strtod().
  buffer[i] = '\0';

  if (float_info.is_float)
  {
    value->kind = TOML_FLOAT;
    value->float_ = strtod(buffer, NULL);
    throw_if(errno == ERANGE, FLOAT_OVERFLOW);
    if (sign)
    {
      value->float_ = -(value->float_);
    }
  } else
  {
    value->kind = TOML_INTEGER;
    value->integer = strtol(buffer, NULL, base);
    throw_if(errno == ERANGE, INT_OVERFLOW);
    if (sign)
    {
      value->integer = -(value->integer);
    }
  }

catch:

  ctx->offset = offset;
  return status;
}

#define HANDLE_ESCAPE_CASES(chr, offset)                      \
  CASE('n')                                                   \
  {                                                           \
    chr = '\n';                                               \
    break;                                                    \
  }                                                           \
  CASE('t')                                                   \
  {                                                           \
    chr = '\t';                                               \
    break;                                                    \
  }                                                           \
  CASE('r')                                                   \
  {                                                           \
    chr = '\r';                                               \
    break;                                                    \
  }                                                           \
  CASE('u')                                                   \
  CASE('U')                                                   \
  {                                                           \
    char c0 = *(++offset);                                    \
    char c1 = offset[1];                                      \
    char num[3] = {c0, c1, 0};                                \
    throw_if(!is_hex(c0) || !is_hex(c1), INVALID_HEX_ESCAPE); \
    chr = strtoul(num, NULL, 16);                             \
    break;                                                    \
  }                                                           \
  CASE('\\')                                                  \
  {                                                           \
    break;                                                    \
  }                                                           \
  CASE('"')                                                   \
  {                                                           \
    chr = '"';                                                \
    break;                                                    \
  }                                                           \
  CASE('b')                                                   \
  {                                                           \
    chr = '\b';                                               \
    break;                                                    \
  }

TOMLStatus TOML_parse_sl_string(TOMLCtx *ctx, String *string)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ctx->offset;
  char const *const end = ctx->end;

  char quote = *(offset++);
  int len;
  do {
    char *str_end = strchr(offset, quote);
    len = str_end == NULL ? 0 : str_end - offset;
  } while (0);
  StringBuffer buffer = StringBuffer_with_capacity(len + 1);
  throw_if(buffer == NULL, OOM);

  for (char chr; offset < end; ++offset)
  {
    chr = *offset;
    switch (chr)
    {
      CASE('\n')
      {
        throw(UNTERMINATED_STRING);
      }
      CASE('\\')
      {
        if (quote == '"')
        {
          switch (*(++offset))
          {
            HANDLE_ESCAPE_CASES(chr, offset);
          }
        }
        break;
      }
      default:
      {
        if (chr == quote)
        {
          goto out;
        }
      }
    }
    throw_if(StringBuffer_push(&buffer, chr) != 0, OOM);
  }

out:
  throw_if(*(offset++) != quote, UNTERMINATED_STRING);
  *string = StringBuffer_transform_to_string(&buffer);

catch:
  if (status != TOML_E_OK && buffer != NULL)
  {
    StringBuffer_cleanup(buffer);
  }
  ctx->offset = offset;
  return status;
}

TOMLStatus TOML_parse_ml_string(TOMLCtx *ctx, String *string)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ctx->offset;
  char const quote = *offset;
  offset += 3;

  char quotes[] = {quote, quote, quote, 0};
  char const *str_end = strstr(offset, quotes);
  throw_if(str_end == NULL, UNTERMINATED_STRING);
  int len = str_end - offset;

  StringBuffer buffer = StringBuffer_with_capacity(len + 1);
  throw_if(buffer == NULL, OOM);

  bool trimming = false;
  for (char chr; offset < str_end; ++offset)
  {
    chr = *offset;
    if (trimming)
    {
      if (chr == ' ' || chr == '\n')
      {
        continue;
      } else
      {
        trimming = false;
      }
    }
    if (chr == '\\' && quote == '"')
    {
      switch (*(++offset))
      {
        HANDLE_ESCAPE_CASES(chr, offset);
        CASE(' ')
        CASE('\n')
        {
          trimming = true;
          goto skip_buffering;
        }
      }
    }
    throw_if(StringBuffer_push(&buffer, chr) != 0, OOM);

skip_buffering:;
  }

  offset += 3;
  *string = StringBuffer_transform_to_string(&buffer);

catch:
  if (status != TOML_E_OK && buffer != NULL)
  {
    StringBuffer_cleanup(buffer);
  }
  ctx->offset = offset;
  return status;
}

#undef HANDLE_ESCAPE_CASES

TOMLStatus TOML_parse_time(TOMLCtx *ctx, TOMLTime *time)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ctx->offset;
  char const *const end = ctx->end;
  try_cond(offset + 8 <= end && offset[2] == ':' && offset[5] == ':',
           INVALID_TIME);

  int const hour = strtoul(offset, (char **)&offset, 10);
  throw_if(*offset != ':', INVALID_TIME);
  ++offset;

  int const min = strtoul(offset, (char **)&offset, 10);
  throw_if(*offset != ':', INVALID_TIME);

  char const *const old_offset = ++offset;
  int const sec = strtoul(offset, (char **)&offset, 10);

  int millisec = 0;
  int8_t z[3] = {0, -1, -1};
  if (offset == NULL)
  {
    offset = end;
  } else
  {
    throw_if(offset < old_offset + 2, INVALID_TIME);
    if (*offset == '.')
    {
      ++offset;
      char _ms_buff[4] = { [3] = '\0' };
      collect_millis(&offset, _ms_buff, 3);
      millisec = strtoul(_ms_buff, NULL, 10);
      throw_if(errno == ERANGE, INT_OVERFLOW);
    }
    switch (*offset)
    {
      CASE('z')
      CASE('Z')
      {
        // if it's a 'z'/'Z' we just put 'Z' and exit from switch
        z[0] = 'Z';
        ++offset;
        break;
      }
      CASE('+')
      CASE('-')
      {
        // we put the one we have in buffer
        z[0] = *offset++;
        // the buffer that will be used by strtoul()
        char _b[4] = { [2] = 0 };
        char *_p = NULL;
        _b[0] = *offset++;
        _b[1] = *offset++;
        if (_b[0] == '0')
        {
          _b[0] = _b[1];
          _b[1] = '\0';
        }
        z[1] = strtoul(_b, &_p, 10);
        if (_p != _b + (_b[1] == '\0' ? 1 : 2))
        {
          throw(INVALID_TIME);
        } else if (errno == ERANGE)
        {
          throw(INT_OVERFLOW);
        }
        if (*offset == ':')
        {
          ++offset;
          _b[0] = *offset++;
          _b[1] = *offset++;
          if (_b[0] == '0')
          {
            _b[0] = _b[1];
            _b[1] = '\0';
          }
          z[2] = strtoul(_b, &_p, 10);
          if (_p != _b + (_b[1] == '\0' ? 1 : 2))
          {
            throw(INVALID_TIME);
          } else if(errno == ERANGE)
          {
            throw(INT_OVERFLOW);
          }
        }
        break;
      }
    }
  }
  time->hour = hour;
  time->min = min;
  time->sec = sec;
  time->millisec = millisec;
  memcpy(&(time->z), z, 3);

catch:
  ctx->offset = offset;
  return status;
}

TOMLStatus TOML_parse_datetime(TOMLCtx *ctx, TOMLValue *value)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ctx->offset;
  char const *const end = ctx->end;
  try_cond(offset + 10 <= end && offset[4] == '-' && offset[7] == '-',
           INVALID_DATE);
  int const year = strtoul(offset, (char **)&offset, 10);
  throw_if(*offset != '-', INVALID_DATE);
  ++offset;
  int const month = strtoul(offset, (char **)&offset, 10);
  throw_if(*offset != '-', INVALID_DATE);
  char const *const old_offset = ++offset;
  int const day = strtoul(offset, (char **)&offset, 10);
  if (offset == NULL) // NULL means the EOS has been reached
  {
    offset = end;
  } else
  {
    throw_if(offset < old_offset + 2, INVALID_DATE);
  }
  char const current = *offset;
  TOMLDate *date;
  if (current == 'T' || current == ' ')
  {
    ctx->offset = ++offset;
    status = TOML_parse_time(ctx, &(value->datetime.time));
    offset = ctx->offset;
    try(status);
    value->kind = TOML_DATETIME;
    date = &(value->datetime.date);
  } else
  {
    value->kind = TOML_DATE;
    date = &(value->date);
  }
  date->year = year;
  date->month = month;
  date->day = day;

catch:
  ctx->offset = offset;
  return status;
}

TOMLStatus TOML_parse_array(TOMLCtx *ctx, TOMLArray *array)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ++(ctx->offset);
  char const *const end = ctx->end;
  TOMLArray vec = TOMLArray_new();
  TOMLValue current;
  throw_if(vec == NULL, OOM);

  for (bool expect_value = true; offset < end;)
  {
    char const chr = *offset;
    if (chr == ']')
    {
      break;
    } else if (chr == ' ' || chr == '\n' || chr == '\t')
    {
      ++offset;
    } else if (chr == '#')
    {
      char const *nl = strchr(offset, '\n');
      throw_if(nl == NULL, ARRAY);
      offset = nl + 1;
    } else if (chr == ',')
    {
      throw_if(expect_value, UNEXPECTED_CHAR);
      ++offset;
      expect_value = true;
    } else
    {
      throw_if(!expect_value, COMMA_OR_BRACKET);
      ctx->offset = offset;
      status = TOML_parse_value(ctx, &current);
      offset = ctx->offset;
      try(status);
      expect_value = false;
      TOMLArray_push(&vec, current);
    }
  }

  throw_if(offset == end, ARRAY);
  ++offset;
  *array = vec;

catch:
  if (status != TOML_E_OK && array != NULL)
  {
    TOMLArray_destroy(vec);
  }
  ctx->offset = offset;
  return status;
}

TOMLStatus TOML_parse_value(TOMLCtx *ctx, TOMLValue *value)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ctx->offset;
  char const *const end = ctx->end;

  char current = *offset;
  switch (current)
  {
    CASE(' ')
    CASE('\t')
    {
      ++(ctx->offset);
      return TOML_parse_value(ctx, value);
    }
    CASE('0'...'9')
    {
      if (offset[4] == '-')
      {
        status = TOML_parse_datetime(ctx, value);
        break;
      } else if (offset[2] == ':')
      {
        value->kind = TOML_TIME;
        status = TOML_parse_time(ctx, &(value->time));
        break;
      }
      // Will just continue and try to parse it as number.
    }
    CASE('.')
    {
      status = TOML_parse_number(ctx, value);
      break;
    }
    CASE('"')
    CASE('\'')
    {
      value->kind = TOML_STRING;
      status = offset[1] == current && offset[2] == current ?
                TOML_parse_ml_string(ctx, &(value->string)) :
                TOML_parse_sl_string(ctx, &(value->string));
      break;
    }
    CASE('[')
    {
      value->kind = TOML_ARRAY;
      status = TOML_parse_array(ctx, &(value->array));
      break;
    }
    CASE('f')
    {
      if (memcmp(offset, "false", 5) == 0)
      {
        value->kind = TOML_BOOLEAN;
        value->boolean = false;
        offset += 5;
      } else
      {
        throw(INVALID_VALUE);
      }
      break;
    }
    CASE('t')
    {
      if (memcmp(offset, "true", 4) == 0)
      {
        value->kind = TOML_BOOLEAN;
        value->boolean = true;
        offset += 4;
      } else
      {
        throw(INVALID_VALUE);
      }
      break;
    }
    CASE('i')
    {
      goto inf_check;
    }
    CASE('+')
    CASE('-')
    {
      ++offset;
inf_check:
      if (strncmp(offset, "inf", 3))
      {
        value->kind = TOML_FLOAT;
        value->float_ = current == '-' ? -(__builtin_inff()) :
                                          (__builtin_inff());
        offset += 3;
      } else if (strncmp(offset, "nan", 3))
      {
        value->kind = TOML_FLOAT;
        value->float_ = current == '-' ? -(__builtin_nanf("")) :
                                          (__builtin_nanf(""));
        offset += 3;
      } else if (current == '.' || in_range(current, '0', '9'))
      {
        --offset;
        status = TOML_parse_number(ctx, value);
      } else
      {
        throw(INVALID_VALUE);
      }
      break;
    }
    default:
    {
      throw(INVALID_VALUE);
    }
  }
  if (ctx->offset > offset)
  {
    offset = ctx->offset;
  }
  current = *offset;
  throw_if(!is_empty(current) &&
           !(current == '#' || current == ',' || current == ']' ||
             current == '}'),
           INVALID_VALUE);
  for (; offset < end;)
  {
    switch (current)
    {
      CASE('\n')
      CASE(' ')
      CASE('\t')
      CASE('\0')
      {
        ++offset;
        break;
      }
      CASE('#')
      {
        char const *nl = strchr(offset, '\n');
        offset = nl == NULL ? end : (nl + 1);
        break;
      }
      default:
      {
        goto catch;
      }
    }
  }

catch:
  ctx->offset = offset;
  return status;
}

static TOMLStatus parse_key(TOMLCtx *ctx, StringBuffer *key)
{
  char c = *ctx->offset;
  if (c == '\'' || c == '"')
  {
    return TOML_parse_sl_string(ctx, (String *)key);
  } else
  {
    char const *offset = ctx->offset;
    char const *const end = ctx->end;
    for (; (offset < end) &&
           (is_letter(c) || is_digit(c) || c == '_' || c == '-'); )
    {
      StringBuffer_push(key, c);
      c = *(++offset);
    }
    StringBuffer_transform_to_string(key);
    ctx->offset = offset;
    return TOML_E_OK;
  }
}

TOMLStatus TOML_parse_entry(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ctx->offset;
  char const *const end = ctx->end;
  String key = NULL;
  char c = *offset;
  if (c == '"' || c == '\'')
  {
    try((offset[1] == c && offset[2] == c) ?
        TOML_parse_ml_string(ctx, &key) :
        TOML_parse_sl_string(ctx, &key));
    offset = ctx->offset;
  } else
  {
    StringBuffer buff = StringBuffer_with_capacity(4);
    throw_if(buff == NULL, OOM);
    for (; offset < end; ++offset)
    {
      c = *offset;
      switch (c)
      {
        CASE('a'...'z')
        CASE('A'...'Z')
        CASE('0'...'9')
        CASE('_')
        CASE('-')
        {
          char prev = offset[-1];
          throw_if((StringBuffer_len(buff) != 0 &&
                    (prev == ' ' || prev == '\t')),
                   INVALID_KEY);
          StringBuffer_push(&buff, c);
        }
        CASE(' ')
        {
          // just ignore
          break;
        }
        CASE('.')
        CASE('=')
        {
          throw_if(StringBuffer_len(buff) == 0, INVALID_KEY);
          key = StringBuffer_transform_to_string(&buff);
          goto out;
        }
        default:
        {
          throw(INVALID_KEY);
        }
      }
    }
  }
  c = *offset;
out:
  if (c == '.')
  {
    ctx->offset = ++offset;
    TOMLValue *val_p = TOMLTable_get(*table_p, key);
    if (val_p == NULL)
    {
      val_p = TOMLTable_set_empty(table_p, key);
      throw_if(val_p == NULL, OOM);
      val_p->kind = TOML_TABLE;
      val_p->table = TOMLTable_new();
    } else
    {
      String_cleanup(key);
      throw_if(val_p->kind != TOML_TABLE, EXPECTED_TABLE_VALUE);
    }
    return TOML_parse_entry(ctx, &(val_p->table));
  } else if (c == '=')
  {
    if (TOMLTable_get(*table_p, key) != NULL)
    {
      status = TOML_E_DUPLICATE_KEY;
    } else
    {
      ctx->offset = ++offset;
      TOMLValue *val_p = TOMLTable_set_empty(table_p, key);
      return TOML_parse_value(ctx, val_p);
    }
  } else if (c == ' ' || c == '\t')
  {
    for (; c == ' ' || c == '\t'; c = *(++offset));
    goto out;
  } else
  {
    status = ((c == '\0') ? TOML_E_EOF : TOML_E_UNEXPECTED_CHAR);
  }
catch:
  if (status != TOML_E_OK)
  {
    if (key != NULL)
    {
      String_cleanup(key);
    }
  }
  return status;
}

TOMLStatus TOML_parse_inline_table(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ctx->offset;
  char const *const end = ctx->end;
  int comma = false;
  char chr = *(++offset);
  for (; offset < end; ++offset)
  {
    if (!(chr == ' ' || chr == '\t' || chr == '\n'))
    {
      if (chr == ',')
      {
        if (comma)
        {
          comma = false;
          continue;
        } else
        {
          throw(COMMA_NOT_EXPECTED);
        }
      } else if (chr == '}')
      {
        break;
      } else
      {
        if (comma)
        {
          throw(COMMA_EXPECTED);
        } else
        {
          ctx->offset = offset;
          status = TOML_parse_entry(ctx, table_p);
          offset = ctx->offset;
          try(status);
        }
      }
    }
    chr = *offset;
  }
  if (chr != '}')
  {
    status = (chr == '\0') ? TOML_E_EOF : TOML_E_COMMA_EXPECTED;
  }
catch:
  ctx->offset = offset;
  return status;
}
