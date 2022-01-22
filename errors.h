#ifndef C_TOML_ERRORS_H
#define C_TOML_ERRORS_H
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/cdefs.h>
#include "lib.h"

#define as_ctx(ctx) ((TOMLCtx *)(ctx))

#define PRINT_ERROR(ctx, pos, ...)                                    \
  {                                                                   \
    fprintf(stderr, "TOML parse error:\n  ");                         \
    fprintf(stderr, __VA_ARGS__);                                     \
    fprintf(stderr, "\n@ - file: [%s]\r\n"                            \
                    "    position: [line: %i, column: %i]\n",         \
            message, as_ctx(ctx)->file_path, pos.line, pos.column);   \
  }

__inline__
char const *format_of_error(TOMLStatus status)
{
  char const * fmt = NULL;
#define CASE(c, f) case TOML_E_##c: { fmt = (f); break; }
  switch (status)
  {
    CASE(OOM, "Amount of memory left is insufficient for the parser.");
    CASE(INT_OVERFLOW, "Integer value is too big.");
    CASE(FLOAT_OVERFLOW, "Float value is too big.");
    CASE(UNTERMINATED_STRING, "String is not terminated with a quote.");
    CASE(MISSING_BRACKET, "EOF occured before array's closing bracket.");
    CASE(INVALID_NUMBER,
                      "Number is invalid, therefore it can't be parsed.");
    CASE(INVALID_KEY, "Table key is invalid.");
    CASE(ARRAY, "A comma or closing bracket was expected.");
    // CASE(UNEXPECTED_CHAR, "
    CASE(INVALID_ESCAPE, "Escape sequence in string is invalid.");
    // CASE(INVALID_VALUE, "
    CASE(INVALID_DATE, "Date value is invalid.");
    CASE(INVALID_TIME, "Time value is invalid.");
    CASE(INVALID_DATETIME, "Datetime value is invalid.");
    CASE(INVALID_HEX_ESCAPE, "Hexadecimal escape sequence is invalid.");
  }
#undef CASE
  return fmt;
}

#endif /* TOML_ERRORS_H */
