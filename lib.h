#ifndef C_TOML_H
#define C_TOML_H
#include <stdint.h>
#include <stdbool.h>
#include <c-string/lib.h> // https://github.com/fabriciopashaj/c-string

// TOML data type ids
#ifdef DEBUG

typedef enum TOMLKind {
#define K(k) TOML_##k
  K(INTEGER) = 1,
  K(FLOAT),
  K(BOOLEAN),
  K(STRING),
  K(DATE),
  K(TIME),
  K(DATETIME),
  K(ARRAY),
  K(TABLE),
  K(INLINE_TABLE),
  K(TABLE_ARRAY)
#undef K
} TOMLKind;

#else

#define TOML_INTEGER      1
#define TOML_FLOAT        2
#define TOML_BOOLEAN      3
#define TOML_STRING       4
#define TOML_DATE         5
#define TOML_TIME         6
#define TOML_DATETIME     7
#define TOML_ARRAY        8
#define TOML_TABLE        9
#define TOML_INLINE_TABLE 10
#define TOML_TABLE_ARRAY  11

typedef uint8_t TOMLKind;

#endif

// Parser statusses
#define TOML_E_OK                   0
#define TOML_E_OOM                  1
#define TOML_E_INT_OVERFLOW         2
#define TOML_E_FLOAT_OVERFLOW       3
#define TOML_E_UNTERMINATED_STRING  4
#define TOML_E_MISSING_BRACKET      5
#define TOML_E_INVALID_NUMBER       6
#define TOML_E_INVALID_KEY          7
#define TOML_E_INVALID_VALUE        8
#define TOML_E_ARRAY                9
#define TOML_E_UNEXPECTED_CHAR      10
#define TOML_E_INVALID_ESCAPE       11
#define TOML_E_COMMA_OR_BRACKET     12
#define TOML_E_INVALID_DATE         13
#define TOML_E_INVALID_TIME         14
#define TOML_E_INVALID_DATETIME     15
#define TOML_E_INVALID_HEX_ESCAPE   16
#define TOML_E_EXPECTED_TABLE_VALUE 17
#define TOML_E_DUPLICATE_KEY        18
#define TOML_E_COMMA_EXPECTED       19
#define TOML_E_COMMA_NOT_EXPECTED   20
#define TOML_E_EOF                  21

// Typedefing the structs before defining their bodies
typedef struct TOMLDate     TOMLDate;
typedef struct TOMLTime     TOMLTime;
typedef struct TOMLDateTime TOMLDateTime;
typedef struct TOMLValue    TOMLValue;
typedef struct TOMLEntry    TOMLEntry; // table key-value pair
typedef struct TOMLCtx      TOMLCtx; // more like parsing state
typedef struct TOMLPosition TOMLPosition; // position of the cursor
// Typedefing array types
typedef struct TOMLValue*   TOMLArray;
//the table
typedef struct TOMLEntry*   TOMLTable; // similiar mechanics to c-vector
// and the status thing
typedef signed long TOMLStatus; // determines success or the error kind

void TOMLValue_print(TOMLValue const *, int, int);

struct TOMLTime {
  uint16_t millisec;
  uint8_t  hour;
  uint8_t  min;
  uint8_t  sec;
  // [0] = ("+" | "-")
  // [1] = <hour>
  // [2] = <minutes>
  int8_t   z[3];
};

struct TOMLDate {
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
};

struct TOMLDateTime {
  TOMLDate date;
  TOMLTime time;
};

struct TOMLValue {
  union {
    String       string;
    signed long  integer;
    double       float_; // it's still a float, just double precision
    bool         boolean;
    TOMLArray    array;
    TOMLTable    table;
    TOMLDate     date;
    TOMLTime     time;
    TOMLDateTime datetime;
  };
  TOMLKind kind;
};

struct TOMLEntry {
  String    key;
  TOMLValue value;
};

#define CVECTOR_POINTERMODE
#define CVECTOR_NO_TYPEDEF
#include <c-vector/lib.h> // https://github.com/fabriciopashaj/c-vector
CVECTOR_WITH_NAME(TOMLValue, TOMLArray);
void TOMLArray_destroy(TOMLArray);

struct TOMLCtx {
  String       file_path;
  StringBuffer content;
  char const   *end;
  char const   *offset;
};

struct TOMLPosition {
  int offset; // cursor (the index where we are at the buffer)
  int line;
  int column;
};

TOMLStatus  TOML_init           (TOMLCtx *, char *);
/* TOMLStatus  TOML_parse           (TOMLCtx *, TOMLTable *);
TOMLStatus  TOML_parse_table      (TOMLCtx *, TOMLTable *); */
TOMLStatus  TOML_prse_inline_table(TOMLCtx *, TOMLTable *);

// Checks and invokes the right parser function.
TOMLStatus  TOML_parse_value       (TOMLCtx *, TOMLValue *);
// It can be an integer or a float, we don't know before we parse it.
TOMLStatus  TOML_parse_number      (TOMLCtx *, TOMLValue *);
// Takes String * because we know it's a string.
TOMLStatus  TOML_parse_sl_string   (TOMLCtx *, String *);
TOMLStatus  TOML_parse_ml_string   (TOMLCtx *, String *);
TOMLStatus  TOML_parse_time        (TOMLCtx *, TOMLTime *);
TOMLStatus  TOML_parse_datetime    (TOMLCtx *, TOMLValue *);
// Like above, we know it's an array.
TOMLStatus  TOML_parse_array       (TOMLCtx *, TOMLArray *);
TOMLStatus  TOML_parse_entry       (TOMLCtx *, TOMLTable *);
TOMLStatus  TOML_parse_inline_table(TOMLCtx *, TOMLTable *);

void TOMLValue_destroy(TOMLValue *);
TOMLPosition TOML_position(TOMLCtx const *);

#include "table.h"

#endif /* C_TOML_H */
