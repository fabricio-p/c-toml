#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include "lib.h"
#include "util.h"

#define throw(err) { status = TOML_E_##err; goto catch; }
#define try(thing) { status = (thing); if (status) { goto catch; } }
#define try_cond(cond, err) if (!(cond)) { try(TOML_E_##err); }
#define throw_if(cond, err) try_cond(!(cond), err)
#define CASE(c) case c:

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
  for (; i < 32 && offset < end; ++i, ++offset)
  {
    char chr = *offset;
    switch (chr)
    {
      CASE('0'...'9')
      {
        throw_if(chr < '7' && base == 8, INVALID_NUMBER);
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
      CASE(' ')
      CASE('\t')
      CASE('\n')
      CASE('#')
      {
        goto out;
      }
      default:
      {
        throw(INVALID_NUMBER);
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
  int len = strchr(offset, quote) - offset;
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
  char z[3] = {0, 0, 0};
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

#if 0
// Untested
TOMLStatus TOML_parse_array(TOMLCtx *ctx, TOMLArray *array)
{
  TOMLStatus status = TOML_E_OK;
  char const *offset = ++(ctx->offset);
  char const *const end = ctx->end;
  TOMLArray vec = TOMLArray_new();
  TOMLValue current;
  throw_if(vec == NULL, OOM);

  for (bool expect_value = true; offset < end; ++offset)
  {
    char const chr = *offset;
    if (chr == ']')
    {
      break;
    } else if (chr == ' ' || chr == '\n' || chr == '\t')
    {
      continue;
    } else if (chr == '#')
    {
      char const *nl = strchr(offset, '\n');
      throw_if(nl == NULL, ARRAY);
      offset = nl + 1;
    } else if (chr == ',')
    {
      throw_if(expect_value, UNEXPECTED_CHAR);
      expect_value = true;
    } else
    {
      throw_if(!expect_value, COMMA_OR_BRACKET);
      ctx->offset = offset;
      status = TOML_parse_value(ctx, &current);
      offset = ctx->offset;
      try(status);
      expect_value = false;
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

  char current = *(offset++);
  switch (current)
  {
    CASE('0'...'9')
    {
      if (offset[3] == '-')
      {
        status = TOML_parse_datetime(ctx, value);
        break;
      } else if (offset[1] == ':')
      {
        status = TOML_parse_time(ctx, &(value->time));
        break;
      }
      // Will just continue and parse it as number.
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
      status = offset[0] == current && offset[1] == current ?
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
      if (strncmp(offset, "alse", 4) == 0)
      {
        value->kind = TOML_BOOLEAN;
        value->boolean = false;
      } else
      {
        status = TOML_E_INVALID_VALUE;
      }
    }
    CASE('t')
    {
      if (strncmp(offset, "rue", 3) == 0)
      {
        value->kind = TOML_BOOLEAN;
        value->boolean = true;
      } else
      {
        status = TOML_E_INVALID_VALUE;
      }
    }
    CASE('i')
    {
      --offset;
      goto inf_check;
    }
    CASE('+')
    CASE('-')
    {
inf_check:
      if (strncmp(offset, "inf", 3))
      {
        value->kind = TOML_FLOAT;
        value->float_ = current == '-' ? -(__builtin_inff()) :
                                          (__builtin_inff());
      } else if (strncmp(offset, "nan", 3))
      {
        value->kind = TOML_FLOAT;
        value->float_ = current == '-' ? -(__builtin_nanf("")) :
                                          (__builtin_nanf(""));
      } else if (current == '.' || in_range(current, '0', '9'))
      {
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
  current = *offset;
  throw_if(!is_empty(current) || current != '#', INVALID_VALUE);
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
    }
  }

catch:
  return status;
}
#endif
