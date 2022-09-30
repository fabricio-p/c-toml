#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <c-ansi-sequences/graphics.h>
#include "lib.h"
#include "util.h"

// TODO: Refactor [TOML_parse_time]

// TODO: Refactor [TOML_parse_number.metadata]

// TODO: [TOML_parse_number] Do more checks for value correctness

// TODO: In(TOML_parse_value.inf_check) ->
//        Do nan and inf parsing without sign prefix

// TODO: [TOML_parse_{time, datetime}] Ditch offset

// XXX: Consider unitype arrays


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

#define PARSE_INT_NEG (1 << 0)
#define PARSE_INT_ILZ (1 << 1) // (I)gnore (L)eading (Z)eroes
static TOMLStatus parse_int(
  char const **buf_p, char const *end, int base, int info, signed long *res_p
)
{
  TOMLStatus status = TOML_E_OK;
  char const *buf = *buf_p;
  signed long cutlim = ((info & PARSE_INT_NEG ? LONG_MIN : LONG_MAX) % base);
  signed long cutoff = (info & PARSE_INT_NEG ? LONG_MIN : LONG_MAX) - cutlim;
  register signed long acc = 0;
  if (info & PARSE_INT_ILZ)
  {
    for (; buf[0] == '0' && buf[1] == '0'; ++buf) {}
  }
  for (; buf < end; ++(buf))
  {
    char c = *buf;
    if (c == '_')
    {
      if (buf[-1] == '_')
      {
        --(buf);
        break;
      } else if (buf == *buf_p)
      {
        break;
      }
    } else if (base <= 10)
    {
      if (c >= '0' && c < '0' + base)
      {
        acc = acc * base + (c - '0');
      } else
      {
        break;
      }
    } else
    {
      if (c >= '0' && c <= '9')
      {
        c  -= '0';
      } else if (c >= 'A' && c < ('A' + base - 10))
      {
        c -= 'A' - 10;
      } else if (c >= 'a' && c < ('a' + base - 10))
      {
        c -= 'a' - 10;
      } else
      {
        break;
      }
      if (acc >= cutoff || c > cutlim)
      {
        status = TOML_E_INT_OVERFLOW;
      }
      if (c > base)
      {
        break;
      } else
      {
        acc *= base;
        acc += c;
      }
    }
  }
  *buf_p = buf;
  *res_p = info & PARSE_INT_NEG ? -acc : acc;
  return status;
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
           ANSIQ_SETBRIGHT_FG(RED) "%s" ANSIQ_GR_RESET,
           value->integer ? "true" : "false");
    SIMPLE(STRING,
           ANSIQ_SETFG_GREEN "\"%.*s\"" ANSIQ_GR_RESET,
           String_len(value->string), value->string);
    KIND(DATETIME)
    {
      print_date(&(value->datetime.date));
      print_time(&(value->datetime.time));
    } break;
    KIND(DATE)
    {
      print_date(&(value->date));
    } break;
    KIND(TIME)
    {
      print_time(&(value->time));
    } break;
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
    } break;
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
  if (indent == 0)
  {
    putchar('\n');
  }
}

TOMLStatus TOML_parse_number(TOMLCtx *ctx, TOMLValue *value)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  int neg = 0;
  int base = 10;
  // TODO: Refactor
metadata:
  switch (*ctx->offset)
  {
    CASE('0')
    {
      switch (*(++(ctx->offset)))
      {
        CASE('x')
        CASE('X')
        {
          ++(ctx->offset);
          base = 16;
        } break;
        CASE('o')
        CASE('O')
        {
          ++(ctx->offset);
          base = 8;
        } break;
        CASE('b')
        CASE('B')
        {
          ++(ctx->offset);
          base = 2;
        } break;
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
    } break;
    CASE('+')
    {
      ++(ctx->offset);
      goto metadata;
    }
    CASE('-')
    {
      ++(ctx->offset);
      neg = 1;
      goto metadata;
    }
  }
  try(
      parse_int(
        &(ctx->offset), end, base, neg ? PARSE_INT_NEG : 0, &(value->integer)
      )
  );
  if (
    base == 10
    && ctx->offset < ctx->end
    && *ctx->offset == '.'
    && is_digit(ctx->offset[1])
  )
  {
    ++(ctx->offset);
    double float_ = (double)value->integer;
    signed long frac_int = 0;
    char const *offset = ctx->offset;
    try(
        parse_int(
          &(ctx->offset),
          end,
          10,
          (neg ? PARSE_INT_NEG : 0)|PARSE_INT_ILZ,
          &(frac_int)
        )
    );
    double frac = (double)frac_int;
    for (int i = 0, frac_len = ctx->offset - offset; i < frac_len; ++i)
    {
      frac /= 10;
    }
    value->float_ = float_ + frac;
    value->kind = TOML_FLOAT;
  } else
  {
    value->kind = TOML_INTEGER;
  }
  if (
    ctx->offset < ctx->end
    && base == 10
    && (*ctx->offset == 'e' || *ctx->offset == 'E')
  )
  {
    ++(ctx->offset);
    if (value->kind == TOML_INTEGER)
    {
      value->float_ = (double)value->integer;
    }
    neg = 0;
    switch (*ctx->offset)
    {
      CASE('-')
      {
        neg = 1;
      } __attribute__((fallthrough));
      CASE('+')
      {
        ++(ctx->offset);
      }
    }
    signed long e = 0;
    try(parse_int(&(ctx->offset), ctx->end, 10, 0, &(e)));
    double float_ = (value->kind == TOML_INTEGER ?
                      (double)value->integer :
                      value->float_);
    for (int i = 0; i < e; ++(i))
    {
      float_ = (neg ? (float_ / 10) : (float_ * 10));
    }
    value->float_ = float_;
  }
catch:

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
    ++(offset);                                               \
    throw_if(                                                 \
        !is_hex(offset[0]) || !is_hex(offset[1]),             \
        INVALID_HEX_ESCAPE                                    \
    );                                                        \
    signed long charcode = 0;                                 \
    parse_int(&offset, offset + 2, 16, 0, &(charcode));       \
    chr = (char)charcode;                                     \
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
  signed long parsed_num = 0;

  try_cond(
      offset + 8 <= end && offset[2] == ':' && offset[5] == ':', INVALID_TIME
  );

  parse_int(&(offset), offset + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const hour = (int)parsed_num;
  throw_if(ctx->offset + 2 != offset, INVALID_TIME);
  ++(offset);

  parse_int(&(offset), offset + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const min = (int)parsed_num;
  throw_if(ctx->offset + 5 != offset, INVALID_TIME);
  ++(offset);

  parse_int(&(offset), offset + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const sec = (int)parsed_num;
  throw_if(ctx->offset + 8 != offset, INVALID_TIME);

  register int millisec = 0;
  int8_t z[3] = {0, -1, -1};
  if (*offset == '.')
  {
    ++(offset);
    parse_int(&(offset), offset + 5, 10, PARSE_INT_ILZ, &(parsed_num));
    millisec = (int)parsed_num;
  }
  switch (*offset)
  {
    CASE('z')
    CASE('Z')
    {
      // if it's a 'z'/'Z' we just put 'Z' and exit from switch
      z[0] = 'Z';
      ++(offset);
    } break;
    CASE('+')
    CASE('-')
    {
      // we put the one we have in buffer
      z[0] = *((offset)++);
      try_cond(offset + 2 <= end, INVALID_TIME);
      parse_int(&(offset), offset + 2, 10, PARSE_INT_ILZ, &(parsed_num));
      z[1] = (uint8_t)parsed_num;
      if (offset < end && *offset == ':')
      {
        ++(offset);
        try_cond(offset + 2 <= end, INVALID_TIME);
        parse_int(&(offset), offset + 2, 10, PARSE_INT_ILZ, &(parsed_num));
        z[2] = (uint8_t)parsed_num;
      }
    } break;
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
  signed long parsed_num = 0;
  try_cond(
      offset + 10 <= end && offset[4] == '-' && offset[7] == '-', INVALID_DATE
  );
  parse_int(&(offset), offset + 4, 10, 0, &(parsed_num));
  register int const year = (int)parsed_num;
  throw_if(ctx->offset + 4 != offset, INVALID_DATE);
  ++(offset);

  parse_int(&(offset), offset + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const month = (int)parsed_num;
  throw_if(ctx->offset + 7 != offset, INVALID_DATE);
  ++(offset);

  parse_int(&(offset), offset + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const day = (int)parsed_num;

  char const current = *offset;
  TOMLDate *date;
  if (offset < end && (current == 'T' || current == ' '))
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
      TOMLValue *val_p = TOMLArray_push_empty(&(vec));
      try(TOML_parse_value(ctx, val_p));
      expect_value = 0;
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
    } __attribute__((fallthrough));
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
    } break;
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
    } break;
    CASE('i')
    CASE('n')
    {
      goto inf_check;
    }
    CASE('+')
    CASE('-')
    {
      ++(ctx->offset);
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
    } break;
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
      StringBuffer_push((StringBuffer *)key, c);
      c = *(++(offset));
    }
    ctx->offset = offset;
    StringBuffer_transform_to_string((StringBuffer *)key);
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
    TOMLValue *val_p = TOMLTable_put(table_p, key);
    for (int running = 1; running; )
    {
      ctx->offset = offset;
      char chr = *offset;
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
          key = NULL;
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
  for (; ctx->offset < ctx->end && *ctx->offset != ']'; )
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
    } else if (*ctx->offset != ']')
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
  if (*ctx->offset == '[')
  {
    ++(ctx->offset);
    is_tblarr = 1;
  }
  TOMLTable *this_table = NULL;
  try(TOML_parse_table_header(ctx, table_p, &this_table, is_tblarr));
  for (; ctx->offset < ctx->end && *ctx->offset != '['; )
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

TOMLStatus TOML_parse(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  for (; ctx->offset < ctx->end; )
  {
    if (is_empty(*ctx->offset))
    {
      ++(ctx->offset);
    } else if (*ctx->offset == '#')
    {
      skip_comment(ctx->offset);
    } else if (*ctx->offset == '[')
    {
      try(TOML_parse_table(ctx, table_p));
    } else
    {
      try(TOML_parse_entry(ctx, table_p));
    }
  }
catch:
  return status;
}
