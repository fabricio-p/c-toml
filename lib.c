#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <c-ansi-sequences/graphics.h>
#include "lib.h"
#include "util.h"

// TODO: Implement my owm integer and double parsing functions
//       instead of using strtol and strtod

#define throw(err) { status = TOML_E_##err; goto catch; }
#define try(thing) { status = (thing); if (status) { goto catch; } }
#define try_cond(cond, err) if (!(cond)) { try(TOML_E_##err); }
#define throw_if(cond, err) try_cond(!(cond), err)
#define CASE(c) case c:
#define INDENT_SIZE 2
#define skip_comment(offset)                  \
  do {                                        \
    char *nln = strchr(offset, '\n');         \
    offset = nln == NULL ? ctx->end : (nln);  \
  } while (0);

void collect_millis(char const **offset_p, char *buff, int n)
{
  char const *offset = *offset_p;
  char c;
  for (int i = 1; (c = *offset) && is_digit(c); ++(i), ++(offset))
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
  for (int i = 0, len = TOMLArray_len(array); i < len; ++(i))
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
  for (register char *chrp = ctx->content; chrp <= offset; ++(chrp))
  {
    if (*chrp == '\n')
    {
      line_start = chrp + 1;
      ++(line_count);
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
    putchar(time->z[0]);
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

void TOMLValue_print(TOMLValue const *value, int indent)
{
#define KIND(c) CASE(TOML_##c)
#define SIMPLE(c, f, ...) KIND(c) { printf(f, __VA_ARGS__); break; }
  int const inner_space_count = (indent + 1) * INDENT_SIZE;
  int const outer_space_count = indent * INDENT_SIZE;
  switch (value->kind)
  {
    SIMPLE(INTEGER,
           ANSIQ_SETFG_YELLOW "%li" ANSIQ_GR_RESET,
           value->integer);
    SIMPLE(FLOAT,
           ANSIQ_SETFG_YELLOW "%lF" ANSIQ_GR_RESET,
           value->float_);
    SIMPLE(BOOLEAN,
           ANSIQ_SETFG_RED "%s" ANSIQ_GR_RESET,
           value->integer ? "1" : "0");
    SIMPLE(STRING,
           ANSIQ_SETFG_GREEN "\"%.*s\"" ANSIQ_GR_RESET,
           String_len(value->string), value->string);
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
      for (; current < end; ++(current))
      {
        for (int i = 0; i < inner_space_count; ++(i), putchar(' '));
        TOMLValue_print(current, indent + 1);
        puts(current < end - 1 ? "," : "");
      }
      for (int i = 0; i < outer_space_count; ++(i), putchar(' '));
      putchar(']');
      break;
    }
    KIND(TABLE)
    KIND(INLINE_TABLE)
    {
      TOMLTable_Bucket const *current = &(value->table[0]);
      TOMLTable_Bucket const *const end = current +
                                          TOMLTable_size(value->table);
      puts("{");
      for (; current < end; ++(current))
      {
        if (current->key != NULL)
        {
          for (int i = 0; i < inner_space_count; ++(i))
          {
            putchar(' ');
          }
          printf(ANSIQ_SETFG_CYAN "%.*s" ANSIQ_GR_RESET " = ",
                 String_len(current->key), current->key);
          TOMLValue_print(&(current->value), indent + 1);
          puts(current < end - 1 ? "," : "");
        }
      }
      for (int i = 0; i < outer_space_count; ++(i))
      {
        putchar(' ');
      }
      putchar('}');
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
  int info = 0;
#define SIGNED       (1 << 0x0)
#define IS_FLOAT     (1 << 0x1)
#define HAS_EXP      (1 << 0x2)
#define EXP_VAL_STRT (1 << 0x3)
  int base = 10;
  int i = 0;
metadata:
  switch (*offset)
  {
    CASE('0')
    {
      switch (*(++(offset)))
      {
        CASE('x')
        CASE('X')
        {
          ++(offset);
          base = 16;
          break;
        }
        CASE('o')
        CASE('O')
        {
          ++(offset);
          base = 8;
          break;
        }
        CASE('b')
        CASE('B')
        {
          ++(offset);
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
      ++(offset);
      goto metadata;
    }
    CASE('-')
    {
      ++(offset);
      info |= SIGNED;
      goto metadata;
    }
  }
  for (; i < 48 && offset < end; ++(i), ++(offset))
  {
    char chr = *offset;
    switch (chr)
    {
      CASE('0'...'9')
      {
        throw_if(chr > '7' && base == 8, INVALID_NUMBER);
        if ((info & HAS_EXP) && !(info & EXP_VAL_STRT))
        {
          info |= EXP_VAL_STRT;
        }
        break;
      }
      CASE('a'...'f')
      CASE('A'...'F')
      {
        if ((chr == 'e' || chr == 'E') && base == 10)
        {
          throw_if(info & HAS_EXP, INVALID_NUMBER);
          if (!(info & IS_FLOAT))
          {
            info |= IS_FLOAT;
          }
          info |= HAS_EXP;
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
        throw_if(info & EXP_VAL_STRT, INVALID_NUMBER);
        info |= EXP_VAL_STRT;
        break;
      }
      CASE('.')
      {
        if (base == 0)
        {
          base = 10;
        }
        throw_if(base != 10, INVALID_NUMBER);
        info |= IS_FLOAT;
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

  if (info & IS_FLOAT)
  {
    value->kind = TOML_FLOAT;
    value->float_ = strtod(buffer, NULL);
    throw_if(errno == ERANGE, FLOAT_OVERFLOW);
    if (info & SIGNED)
    {
      value->float_ = -(value->float_);
    }
  } else
  {
    value->kind = TOML_INTEGER;
    value->integer = strtol(buffer, NULL, base);
    throw_if(errno == ERANGE, INT_OVERFLOW);
    if (info & SIGNED)
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
    char c0 = *(++(offset));                                    \
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

  for (char chr; offset < end; ++(offset))

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
          switch (*(++(offset)))
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

  int trimming = 0;
  for (char chr; offset < str_end; ++(offset))
  {
    chr = *offset;
    if (trimming)
    {
      if (chr == ' ' || chr == '\n')
      {
        continue;
      } else
      {
        trimming = 0;
      }
    }
    if (chr == '\\' && quote == '"')
    {
      switch (*(++(offset)))
      {
        HANDLE_ESCAPE_CASES(chr, offset);
        CASE(' ')
        CASE('\n')
        {
          trimming = 1;
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
  ++(offset);

  int const min = strtoul(offset, (char **)&offset, 10);
  throw_if(*offset != ':', INVALID_TIME);

  char const *const old_offset = ++(offset);
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
      ++(offset);
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
        ++(offset);
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
          ++(offset);
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
  ++(offset);
  int const month = strtoul(offset, (char **)&offset, 10);
  throw_if(*offset != '-', INVALID_DATE);
  char const *const old_offset = ++(offset);
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
    ctx->offset = ++(offset);
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
  char const *const end = ctx->end;
  TOMLArray vec = TOMLArray_new();
  TOMLValue current;
  throw_if(vec == NULL, OOM);
  ++(ctx->offset);

  for (int expect_value = 1; ctx->offset < end; )
  {
    char const chr = *ctx->offset;
    if (chr == ']')
    {
      break;
    } else if (chr == ' ' || chr == '\n' || chr == '\t')
    {
      ++(ctx->offset);
    } else if (chr == '#')
    {
      char const *nl = strchr(ctx->offset, '\n');
      throw_if(nl == NULL, ARRAY);
      ctx->offset = nl + 1;
    } else if (chr == ',')
    {
      throw_if(expect_value, UNEXPECTED_CHAR);
      ++(ctx->offset);
      expect_value = 1;
    } else
    {
      throw_if(!expect_value, COMMA_OR_BRACKET);
      try(TOML_parse_value(ctx, &current));
      expect_value = 0;
      TOMLArray_push(&vec, current);
    }
  }

  throw_if(*ctx->offset != ']', ARRAY);
  ++(ctx->offset);
  *array = vec;

catch:
  if (status != TOML_E_OK && array != NULL)
  {
    TOMLArray_destroy(vec);
  }
  return status;
}

TOMLStatus TOML_parse_value(TOMLCtx *ctx, TOMLValue *value)
{
  TOMLStatus status = TOML_E_OK;
  char current = *ctx->offset;
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
      if (is_digit(ctx->offset[0]) && is_digit(ctx->offset[1]))
      {
        if (is_digit(ctx->offset[2]) && is_digit(ctx->offset[3]) &&
            ctx->offset[4] == '-')
        {
          return TOML_parse_datetime(ctx, value);
        } else if (ctx->offset[2] == ':')
        {
          value->kind = TOML_TIME;
          return TOML_parse_time(ctx, &(value->time));
        }
      }
      // Will just continue and try to parse it as number.
    }
    CASE('.')
    {
      return TOML_parse_number(ctx, value);
    }
    CASE('"')
    CASE('\'')
    {
      value->string = NULL;
      value->kind = TOML_STRING;
      if (ctx->offset[1] == current && ctx->offset[2] == current)
      {
        return TOML_parse_ml_string(ctx, &(value->string));
      } else
      {
        return TOML_parse_sl_string(ctx, &(value->string));
      }
    }
    CASE('[')
    {
      value->kind = TOML_ARRAY;
      return TOML_parse_array(ctx, &(value->array));
    }
    CASE('f')
    {
      if (memcmp(ctx->offset, "false", 5) == 0)
      {
        value->kind = TOML_BOOLEAN;
        value->integer = 0;
        ctx->offset += 5;
      } else
      {
        throw(INVALID_VALUE);
      }
      break;
    }
    CASE('t')
    {
      if (memcmp(ctx->offset, "true", 4) == 0)
      {
        value->kind = TOML_BOOLEAN;
        value->integer = 1;
        ctx->offset += 4;
      } else
      {
        throw(INVALID_VALUE);
      }
      break;
    }
    CASE('i')
    CASE('n')
    {
      goto inf_check;
    }
    CASE('+')
    CASE('-')
    {
      ++(ctx->offset);
      // TODO: Do nan and inf parsing without sign prefix
inf_check:
      if (ctx->offset[0] == 'i' && ctx->offset[1] == 'n' &&
          ctx->offset[2] == 'f')
      {
        value->kind = TOML_FLOAT;
        value->float_ = current == '-' ? -(__builtin_inff()) :
                                          (__builtin_inff());
        ctx->offset += 3;
      } else if (ctx->offset[0] == 'n' && ctx->offset[1] == 'a' &&
                 ctx->offset[2] == 'n')
      {
        value->kind = TOML_FLOAT;
        value->float_ = current == '-' ? -(__builtin_nanf("")) :
                                          (__builtin_nanf(""));
        ctx->offset += 3;
      } else if (current == '.' || in_range(current, '0', '9'))
      {
        --(ctx->offset);
        status = TOML_parse_number(ctx, value);
      } else
      {
        throw(INVALID_VALUE);
      }
      break;
    }
    CASE('{')
    {
      value->kind = TOML_INLINE_TABLE;
      value->table = TOMLTable_new();
      return TOML_parse_inline_table(ctx, &(value->table));
    }
    default:
    {
      throw(INVALID_VALUE);
    }
  }

catch:
  return status;
}

__attribute__((unused))
static TOMLStatus parse_key(TOMLCtx *ctx, String *key)
{
  char c = *ctx->offset;
  if (c == '\'' || c == '"')
  {
    if (ctx->offset[1] == c && ctx->offset[2] == c)
    {
      return TOML_parse_ml_string(ctx, key);
    } else
    {
      return TOML_parse_sl_string(ctx, key);
    }
  } else
  {
    *key = (String)StringBuffer_new();
    char const *offset = ctx->offset;
    char const *const end = ctx->end;
    for (; (offset < end) &&
           (is_letter(c) || is_digit(c) || c == '_' || c == '-'); )
    {
      StringBuffer_push(key, c);
      c = *(++(offset));
    }
    ctx->offset = offset;
    StringBuffer_transform_to_string(key);
    return TOML_E_OK;
  }
}

TOMLStatus TOML_parse_entry(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  char const *offset = ctx->offset;

  String key = NULL;
  for (; offset <= end; ctx->offset = offset)
  {
    try(parse_key(ctx, &key));
    offset = ctx->offset;
    for (int running = 1; running; )
    {
      ctx->offset = offset;
      char chr = *offset;
      TOMLValue *val_p = TOMLTable_put(table_p, key);
      if (chr == '.')
      {
        if (val_p->kind == 0)
        {
          val_p->kind = TOML_TABLE;
          val_p->table = TOMLTable_new();
        } else
        {
          throw_if(val_p->kind != TOML_TABLE, EXPECTED_TABLE);
          String_cleanup(key);
        }
        table_p = &(val_p->table);
        ++(offset);
      } else
      {
        throw_if(val_p->kind != 0, DUPLICATE_KEY);
        if (chr == ' ' || chr == '\t')
        {
          ++(offset);
          continue;
        } else if (chr == '=')
        {
          ctx->offset = ++(offset);
          return TOML_parse_value(ctx, val_p);
        }
      }
      running = 0;
    }
  }

catch:
  if (status != TOML_E_OK)
  {
    if (key != NULL)
    {
      String_cleanup(key);
    }
  }
  ctx->offset = offset;
  return status;
}

TOMLStatus TOML_parse_inline_table(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  ++(ctx->offset);
  for (int expect_entry = 1; ctx->offset < end; )
  {
    char __c = *ctx->offset;
    if (is_empty(__c))
    {
      ++(ctx->offset);
    } else if (__c == ',')
    {
      throw_if(expect_entry, ENTRY_EXPECTED);
      expect_entry = 1;
      ++(ctx->offset);
    } else if (__c == '}')
    {
      break;
    } else if (__c == '#')
    {
      skip_comment(ctx->offset);
    } else if (is_letter(__c) || is_digit(__c) ||
               __c == '"' || __c == '\'' || __c == '-' || __c == '_')
    {
      throw_if(!expect_entry, ENTRY_UNEXPECTED);
      expect_entry = 0;
      try(TOML_parse_entry(ctx, table_p));
    } else
    {
      throw(UNEXPECTED_CHAR);
    }
  }
  if (*ctx->offset != '}')
  {
    status = TOML_E_INLINE_TABLE;
  } else
  {
    ++(ctx->offset);
  }
catch:
  return status;
}

TOMLStatus TOML_parse_table_header(TOMLCtx *ctx, TOMLTable *table_p,
                                   TOMLTable **out_pp, int is_tblarr)
{
  TOMLStatus status = TOML_E_OK;
  String key = NULL;
  for (; *ctx->offset != ']'; )
  {
    try(parse_key(ctx, &(key)));
    if (*ctx->offset == '.')
    {
      ++(ctx->offset);
      TOMLValue *val_p = TOMLTable_put(table_p, key);
      if (val_p->kind == 0)
      {
        val_p->kind = TOML_TABLE;
        val_p->table = TOMLTable_new();
      } else
      {
        String_cleanup(key);
        key = NULL;
        throw_if(val_p->kind != TOML_TABLE, EXPECTED_TABLE);
      }
      table_p = &(val_p->table);
    } else if (*ctx->offset == ' ' || *ctx->offset == '\t')
    {
      ++(ctx->offset);
    } else if (*ctx->offset == '#')
    {
      skip_comment(ctx->offset);
    } else
    {
      throw(INVALID_HEADER);
    }
  }
  if (ctx->offset[1] == ']')
  {
    throw_if(!is_tblarr, TABLE_ARRAY_HEADER);
    ++(ctx->offset);
    TOMLValue *arrval_p = TOMLTable_put(table_p, key);
    TOMLValue *tblval_p = NULL;
    if (arrval_p->kind != 0)
    {
      String_cleanup(key);
      throw_if(arrval_p->kind != TOML_TABLE_ARRAY, EXPECTED_TABLE_ARRAY);
    } else
    {
      arrval_p->kind = TOML_TABLE_ARRAY;
      arrval_p->array = TOMLArray_new();
    }
    tblval_p = TOMLArray_push_empty(&(arrval_p->array));
    tblval_p->kind = TOML_TABLE;
    tblval_p->table = TOMLTable_new();
    *out_pp = &(tblval_p->table);
  } else
  {
    throw_if(is_tblarr, TABLE_HEADER);
    TOMLValue *tblval_p = TOMLTable_put(table_p, key);
    if (tblval_p->kind != 0)
    {
      String_cleanup(key);
      throw_if(tblval_p->kind != TOML_TABLE, EXPECTED_TABLE);
    } else
    {
      tblval_p->kind = TOML_TABLE;
      tblval_p->table = TOMLTable_new();
    }
    *out_pp = &(tblval_p->table);
  }
  ++(ctx->offset);
catch:
  return status;
}

TOMLStatus TOML_parse_table(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  ++(ctx->offset);
  int is_tblarr = 0;
  if (ctx->offset[1] == ']')
  {
    ++(ctx->offset);
    is_tblarr = 1;
  }
  TOMLTable *this_table = NULL;
  try(TOML_parse_table_header(ctx, table_p, &this_table, is_tblarr));
  for (; *ctx->offset != '['; )
  {
    if (is_empty(*ctx->offset))
    {
      ++(ctx->offset);
    } else
    {
      try(TOML_parse_entry(ctx, this_table));
    }
  }
catch:
  return status;
}
