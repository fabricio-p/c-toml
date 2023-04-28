/*
 * @file lib.c
 * @brief Functions for parsing and printing TOML data.
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <c-ansi-sequences/graphics.h>
#include "lib.h"
#include "util.h"

#define __fallthrough__ __attribute__((fallthrough))
#define OFFSET (ctx->offset)

// TODO: [TOML_parse_number] Do more checks for value correctness,
//                           Check for float overflows

// TODO: [TOML_parse_{m,s}l_string] Don't allocate a new string if the value
//                                  pointed by `string` is not NULL.

// TODO: [TOML_parse_array] Don't allocate a new array if the value
//                                  pointed by `array` is not NULL.

// XXX: Consider implementing unitype arrays for memory efficiency,
//        may need to sacrifice some performance


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

/*
 * @brief Recursively free the TOML value.
 */
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

/**
 * @brief Calls @link TOMLValue_destroy @endlink on all the items and frees
 *        the array.
 */
void TOMLArray_destroy(TOMLArray array)
{
  for (int i = 0, len = TOMLArray_len(array); i < len; ++(i))
  {
    TOMLValue_destroy(&(array[i]));
  }
  TOMLArray_cleanup(array);
}

/**
 * @brief Gets the current possition of the parser.
 */
TOMLPosition TOML_position(TOMLCtx const *ctx)
{
  const char *const offset = OFFSET;
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

//TODO:: Make it print into @link FILE @endlink s
/**
 * @brief Prints a @link TOMLValue @endlink to the standard output.
 * @param value The value to be printed.
 * @param level The depth inside a container value determining indentation
 *              level, used for pretty printing container values.
 */
void TOMLValue_print(TOMLValue const *value, int level)
{
#define KIND(c) CASE(TOML_##c)
#define SIMPLE(c, f, ...) KIND(c) { printf(f, __VA_ARGS__); break; }
  int const inner_space_count = (level + 1) * INDENT_SIZE;
  int const outer_space_count = level * INDENT_SIZE;
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
        TOMLValue_print(current, level + 1);
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
          printf(
              ANSIQ_SETFG_CYAN "%.*s" ANSIQ_GR_RESET " = ",
              String_len(current->key), current->key
          );
          TOMLValue_print(&(current->value), level + 1);
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
  if (level == 0)
  {
    putchar('\n');
  }
}

/**
 * @brief Parses a numerical value.
 * @param value The pointer to the @link TOMLValue @endlink where the parsed
 *              number will be stored. If the number is a float, `value->kind`
 *              will be set to @link TOML_FLOAT @endlink and `value->float_`
 *              to the parsed value. If it is an integer, `value->kind` will
 *              be set to @link TOML_INTEGER @endlink and `value->integer` to
 *              the parsed value.
 * @returns @link TOML_E_INVALID_NUMBER @endlink when trying to parse an
 *          invalid numerical value,
 *          @link TOML_E_INT_OVERFLOW @endlink when trying to parse an integer
 *          that is too big.
 */
TOMLStatus TOML_parse_number(TOMLCtx *ctx, TOMLValue *value)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  int neg = 0;
  int base = 10;
  switch (*OFFSET)
  {
    CASE('-')
    {
      neg = 1;
    } __fallthrough__;
    CASE('+')
    {
      ++(OFFSET);
    } break;
  }
  if (*OFFSET == '0')
  {
    switch (*(++(OFFSET)))
    {
      CASE('x')
      CASE('X')
      {
        ++(OFFSET);
        base = 16;
      } break;
      CASE('o')
      CASE('O')
      {
        ++(OFFSET);
        base = 8;
      } break;
      CASE('b')
      CASE('B')
      {
        ++(OFFSET);
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
        // XXX: remove this if we do the check in every function that calls
        //      this function
        throw(INVALID_NUMBER);
      }
    }
  } else if (OFFSET[0] == 'i' && OFFSET[1] == 'n' && OFFSET[2] == 'f')
  {
    value->kind = TOML_FLOAT;
    value->float_ = __builtin_inff();
    OFFSET += 3;
    throw(OK);
  } else if (OFFSET[0] == 'n' && OFFSET[1] == 'a' && OFFSET[2] == 'n')
  {
    value->kind = TOML_FLOAT;
    value->float_ = __builtin_nanf("");
    OFFSET += 3;
    throw(OK);
  }
  try(
      parse_int(
        &(OFFSET), end, base, neg ? PARSE_INT_NEG : 0, &(value->integer)
      )
  );
  if (
      base == 10
      && OFFSET < ctx->end
      && *OFFSET == '.'
      && is_digit(OFFSET[1])
  )
  {
    ++(OFFSET);
    double float_ = (double)value->integer;
    signed long frac_int = 0;
    char const *offset = OFFSET;
    try(
        parse_int(
          &(OFFSET),
          end,
          10,
          (neg ? PARSE_INT_NEG : 0)|PARSE_INT_ILZ,
          &(frac_int)
        )
    );
    double frac = (double)frac_int;
    for (int i = 0, frac_len = OFFSET - offset; i < frac_len; ++i)
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
      OFFSET < ctx->end
      && base == 10
      && (*OFFSET == 'e' || *OFFSET == 'E')
  )
  {
    ++(OFFSET);
    if (value->kind == TOML_INTEGER)
    {
      value->float_ = (double)value->integer;
    }
    switch (*OFFSET)
    {
      CASE('-')
      {
        neg = 1;
      } __fallthrough__;
      CASE('+')
      {
        ++(OFFSET);
      }
    }
    int neg_exp = 0;
    signed long e = 0;
    try(parse_int(&(OFFSET), ctx->end, 10, 0, &(e)));
    double float_ = (value->kind == TOML_INTEGER ?
                    (double)value->integer :
                    value->float_);
    for (int i = 0; i < e; ++(i))
    {
      float_ = (neg_exp ? (float_ / 10) : (float_ * 10));
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
  CASE('x')                                                   \
  CASE('X')                                                   \
  {                                                           \
    ++(offset);                                               \
    throw_if(                                                 \
        !is_hex((offset)[0]) || !is_hex((offset)[1]),         \
        INVALID_HEX_ESCAPE                                    \
    );                                                        \
    signed long charcode = 0;                                 \
    parse_int(&(offset), offset + 2, 16, 0, &(charcode));     \
    chr = (char)charcode;                                     \
    --(offset);                                               \
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

/**
 * @brief Parses a single-line string.
 * @param string The address where the parsed string will be stored.
 */
TOMLStatus TOML_parse_sl_string(TOMLCtx *ctx, String *string)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = OFFSET;
  char const *const end = ctx->end;

  char quote = *(offset++);

  StringBuffer buffer = StringBuffer_with_capacity(8);
  throw_if(buffer == NULL, OOM);

  for (char chr; offset < end; ++(offset))
  {
    chr = *offset;
    throw_if(chr == '\n', UNTERMINATED_STRING);
    if (chr == '\\' && quote == '"')
    {
      ++offset;
      switch (*(offset))
      {
        HANDLE_ESCAPE_CASES(chr, offset);
      }
    } else if (chr == quote)
    {
      break;
    }
    throw_if(StringBuffer_push(&buffer, chr) != 0, OOM);
  }

  throw_if(*offset != quote, UNTERMINATED_STRING);
  ++offset;
  *string = StringBuffer_transform_to_string(&buffer);

catch:
  if (status != TOML_E_OK && buffer != NULL)
  {
    StringBuffer_cleanup(buffer);
  }
  OFFSET = offset;
  return status;
}

/**
 * @brief Parses a multi-line string.
 * @param string The address where the parsed string will be stored.
 */
TOMLStatus TOML_parse_ml_string(TOMLCtx *ctx, String *string)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = OFFSET;
  char const quote = *offset;
  offset += 3;

  char quotes[] = {quote, quote, quote, 0};
  char const *str_end = strstr(offset, quotes);
  StringBuffer buffer = NULL;
  throw_if(str_end == NULL, UNTERMINATED_STRING);
  int len = str_end - offset;

  buffer = StringBuffer_with_capacity(len + 1);
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
  OFFSET = offset;
  return status;
}

#undef HANDLE_ESCAPE_CASES

/**
 * @brief Parses a TOML time value.
 * @param time Pointer to a @link TOMLTime @endlink struct where the parsed
 *             data will be stored.
 */
TOMLStatus TOML_parse_time(TOMLCtx *ctx, TOMLTime *time)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  signed long parsed_num = 0;

  try_cond(
      OFFSET + 8 <= end && OFFSET[2] == ':' && OFFSET[5] == ':', INVALID_TIME
  );
  char const *old_offset = OFFSET;

  parse_int(&(OFFSET), OFFSET + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const hour = (int)parsed_num;
  throw_if(old_offset + 2 != OFFSET, INVALID_TIME);
  ++(OFFSET);

  parse_int(&(OFFSET), OFFSET + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const min = (int)parsed_num;
  throw_if(old_offset + 5 != OFFSET, INVALID_TIME);
  ++(OFFSET);

  parse_int(&(OFFSET), OFFSET + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const sec = (int)parsed_num;
  throw_if(old_offset + 8 != OFFSET, INVALID_TIME);

  register int millisec = 0;
  int8_t z[3] = {0, -1, -1};
  if (*OFFSET == '.')
  {
    ++(OFFSET);
    parse_int(&(OFFSET), OFFSET + 5, 10, PARSE_INT_ILZ, &(parsed_num));
    millisec = (int)parsed_num;
  }
  switch (*OFFSET)
  {
    CASE('z')
    CASE('Z')
    {
      // if it's a 'z'/'Z' we just put 'Z' and exit from switch
      z[0] = 'Z';
      ++(OFFSET);
    } break;
    CASE('+')
    CASE('-')
    {
      // we put the one we have in buffer
      z[0] = *((OFFSET)++);
      try_cond(OFFSET + 2 <= end, INVALID_TIME);
      parse_int(&(OFFSET), OFFSET + 2, 10, PARSE_INT_ILZ, &(parsed_num));
      z[1] = (uint8_t)parsed_num;
      if (OFFSET < end && *OFFSET == ':')
      {
        ++(OFFSET);
        try_cond(OFFSET + 2 <= end, INVALID_TIME);
        parse_int(&(OFFSET), OFFSET + 2, 10, PARSE_INT_ILZ, &(parsed_num));
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
  return status;
}

/*
 * @brief Parses a TOML date or date-time value.
 * @param value The pointer to the @link TOMLValue @endlink struct where the
 *              parsed data will be stored. The `kind` field will be set to
 *              either @link TOML_DATE @endlink or @link TOML_DATETIME @endlink
 *              according to the content provided for parsing.
 */
TOMLStatus TOML_parse_datetime(TOMLCtx *ctx, TOMLValue *value)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  signed long parsed_num = 0;
  try_cond(
      OFFSET + 10 <= end && OFFSET[4] == '-' && OFFSET[7] == '-', INVALID_DATE
  );
  char const *old_offset = OFFSET;
  parse_int(&(OFFSET), OFFSET + 4, 10, 0, &(parsed_num));
  register int const year = (int)parsed_num;
  throw_if(old_offset + 4 != OFFSET, INVALID_DATE);
  ++(OFFSET);

  parse_int(&(OFFSET), OFFSET + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const month = (int)parsed_num;
  throw_if(old_offset + 7 != OFFSET, INVALID_DATE);
  ++(OFFSET);

  parse_int(&(OFFSET), OFFSET + 2, 10, PARSE_INT_ILZ, &(parsed_num));
  register int const day = (int)parsed_num;

  char const current = *OFFSET;
  TOMLDate *date;
  if (OFFSET < end && (current == 'T' || current == ' '))
  {
    old_offset = ++(OFFSET);
    try(TOML_parse_time(ctx, &(value->datetime.time)));
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
  return status;
}

/**
 * @brief Parses a TOML array value.
 * @param array The address where the parsed array will be stored.
 */
TOMLStatus TOML_parse_array(TOMLCtx *ctx, TOMLArray *array)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  TOMLArray vec = TOMLArray_new();
  throw_if(vec == NULL, OOM);
  ++(OFFSET);

  for (int expect_value = 1; OFFSET < end; )
  {
    char const chr = *OFFSET;
    if (chr == ']')
    {
      break;
    } else if (chr == ' ' || chr == '\n' || chr == '\t')
    {
      ++(OFFSET);
    } else if (chr == '#')
    {
      char const *nl = strchr(OFFSET, '\n');
      throw_if(nl == NULL, ARRAY);
      OFFSET = nl + 1;
    } else if (chr == ',')
    {
      throw_if(expect_value, UNEXPECTED_CHAR);
      ++(OFFSET);
      expect_value = 1;
    } else
    {
      throw_if(!expect_value, COMMA_OR_BRACKET);
      TOMLValue *val_p = TOMLArray_push_empty(&(vec));
      try(TOML_parse_value(ctx, val_p));
      expect_value = 0;
    }
  }

  throw_if(*OFFSET != ']', ARRAY);
  ++(OFFSET);
  *array = vec;

catch:
  if (status != TOML_E_OK && array != NULL)
  {
    TOMLArray_destroy(vec);
  }
  return status;
}

/**
 * @brief Parses a TOML value.
 * @param value The address to the @link TOMLValue @endlink struct where the
 *              data parsed will be stored. The `kind` field will be set
 *              according to the content provided for parsing.
 */
TOMLStatus TOML_parse_value(TOMLCtx *ctx, TOMLValue *value)
{
  TOMLStatus status = TOML_E_OK;
  char current = *OFFSET;
  switch (current)
  {
    CASE(' ')
    CASE('\t')
    {
      ++(OFFSET);
      return TOML_parse_value(ctx, value);
    }
    CASE('0'...'9')
    {
      if (is_digit(OFFSET[0]) && is_digit(OFFSET[1]))
      {
        if (is_digit(OFFSET[2]) && is_digit(OFFSET[3]) &&
            OFFSET[4] == '-')
        {
          return TOML_parse_datetime(ctx, value);
        } else if (OFFSET[2] == ':')
        {
          value->kind = TOML_TIME;
          return TOML_parse_time(ctx, &(value->time));
        }
      }
      // Will just continue and try to parse it as number.
    } __fallthrough__;
    CASE('.')
    {
number:
      return TOML_parse_number(ctx, value);
    }
    CASE('"')
    CASE('\'')
    {
      value->string = NULL;
      value->kind = TOML_STRING;
      if (OFFSET[1] == current && OFFSET[2] == current)
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
      if (memcmp(OFFSET, "false", 5) == 0)
      {
        value->kind = TOML_BOOLEAN;
        value->integer = 0;
        OFFSET += 5;
      } else
      {
        throw(INVALID_VALUE);
      }
    } break;
    CASE('t')
    {
      if (memcmp(OFFSET, "true", 4) == 0)
      {
        value->kind = TOML_BOOLEAN;
        value->integer = 1;
        OFFSET += 4;
      } else
      {
        throw(INVALID_VALUE);
      }
    } break;
    CASE('i')
    CASE('n')
    CASE('+')
    CASE('-')
    {
      goto number;
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

static TOMLStatus parse_key(TOMLCtx *ctx, String *key)
{
  char c = *OFFSET;
  if (c == '\'' || c == '"')
  {
    if (OFFSET[1] == c && OFFSET[2] == c)
    {
      return TOML_parse_ml_string(ctx, key);
    } else
    {
      return TOML_parse_sl_string(ctx, key);
    }
  } else
  {
    *key = (String)StringBuffer_new();
    char const *offset = OFFSET;
    char const *const end = ctx->end;
    for (; (offset < end) &&
           (is_letter(c) || is_digit(c) || c == '_' || c == '-'); )
    {
      StringBuffer_push((StringBuffer *)key, c);
      c = *(++(offset));
    }
    OFFSET = offset;
    StringBuffer_transform_to_string((StringBuffer *)key);
    return TOML_E_OK;
  }
}

/**
 * @brief Parses a TOML entry of the form of `key = value` pair.
 * @param table_p The pointer to the table where the parsed entry will be
 *                stored.
 */
TOMLStatus TOML_parse_entry(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  char const *offset = OFFSET;

  String key = NULL;
  for (; offset <= end; OFFSET = offset)
  {
    try(parse_key(ctx, &key));
    offset = OFFSET;
    TOMLValue *val_p = TOMLTable_put(table_p, key);
    for (int running = 1; running; )
    {
      OFFSET = offset;
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
          OFFSET = ++(offset);
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
  OFFSET = offset;
  return status;
}

/**
 * @brief Parses a TOML inline-table value.
 * @param table_p The pointer to the table where the parsed entries of the
 *                    table value will be stored.
 */
TOMLStatus TOML_parse_inline_table(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  char const *const end = ctx->end;
  ++(OFFSET);
  for (int expect_entry = 1; OFFSET < end; )
  {
    char __c = *OFFSET;
    if (is_empty(__c))
    {
      ++(OFFSET);
    } else if (__c == ',')
    {
      throw_if(expect_entry, ENTRY_EXPECTED);
      expect_entry = 1;
      ++(OFFSET);
    } else if (__c == '}')
    {
      break;
    } else if (__c == '#')
    {
      skip_comment(OFFSET);
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
  if (*OFFSET != '}')
  {
    status = TOML_E_INLINE_TABLE;
  } else
  {
    ++(OFFSET);
  }
catch:
  return status;
}

/**
 * @brief Parses the header of a TOML table or table array.
 *        The opening bracket(s) should be skipped before the function is
 *        called.
 * @param table_p The pointer to the parent table.
 * @param out_pp The address of a variable that points to a table.
 *               That variable will be set to the address of the final table
 *               because table headers can take the form `[key1.key2]`.
 * @param is_tblarr `0` to parse a table header, `1` a table array header.
 */
TOMLStatus TOML_parse_table_header(TOMLCtx *ctx, TOMLTable *table_p,
                                   TOMLTable **out_pp, int is_tblarr)
{
  TOMLStatus status = TOML_E_OK;
  String key = NULL;
  for (; OFFSET < ctx->end && *OFFSET != ']'; )
  {
    try(parse_key(ctx, &(key)));
    if (*OFFSET == '.')
    {
      ++(OFFSET);
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
    } else if (*OFFSET == ' ' || *OFFSET == '\t')
    {
      ++(OFFSET);
    } else if (*OFFSET == '#')
    {
      skip_comment(OFFSET);
    } else if (*OFFSET != ']')
    {
      throw(INVALID_HEADER);
    }
  }
  if (OFFSET[1] == ']')
  {
    throw_if(!is_tblarr, TABLE_ARRAY_HEADER);
    ++(OFFSET);
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
  ++(OFFSET);
catch:
  return status;
}

/**
 * @brief Parses the entries of a table or table array.
 * @param table_p The pointer to the the parent table.
 */
TOMLStatus TOML_parse_table(TOMLCtx *ctx, TOMLTable *table_p)
{
  TOMLStatus status = TOML_E_OK;
  ++(OFFSET);
  int is_tblarr = 0;
  if (*OFFSET == '[')
  {
    ++(OFFSET);
    is_tblarr = 1;
  }
  TOMLTable *this_table = NULL;
  try(TOML_parse_table_header(ctx, table_p, &this_table, is_tblarr));
  for (; OFFSET < ctx->end && *OFFSET != '['; )
  {
    if (is_empty(*OFFSET))
    {
      ++(OFFSET);
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
  for (; OFFSET < ctx->end; )
  {
    char const chr = *OFFSET;
    if (is_empty(chr))
    {
      ++(OFFSET);
    } else if (chr == '#')
    {
      skip_comment(OFFSET);
    } else if (chr == '[')
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
