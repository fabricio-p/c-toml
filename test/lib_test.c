#include <c-ansi-sequences/graphics.h>
#include <c-ansi-sequences/cursor.h>
#include <c-ansi-sequences/screen.h>
#include "testing.h"
#include "lib.h"
#include "util.h"
#include "errors.h"

#define TBLGET(t, s) TOMLTable_get((t), String_fake(s))

// static inline __attribute__((always_inline))
TOMLCtx make_toml(char const *const data, int const offset)
{
  return (TOMLCtx) {
    .content = (StringBuffer)data,
    .end = (char *)data + strlen(data),
    .offset = (char *)data + offset
  };
}
static inline __attribute__((always_inline))
char const *type_name(TOMLValue const *const val)
{
  switch (val->kind)
  {
    case TOML_INTEGER:
      return "integer";
    case TOML_FLOAT:
      return "float";
    case TOML_STRING:
      return "string";
    case TOML_BOOLEAN:
      return "boolean";
    case TOML_ARRAY:
      return "array";
    case TOML_TABLE:
      return "table";
    case TOML_INLINE_TABLE:
      return "inline-table";
    case TOML_TABLE_ARRAY:
      return "table-array";
    default:
      return "unknown";
  }
}

void test_parse_number(void)
{
  TOMLCtx ctx = make_toml(" 3475", 1);
  TOMLPosition position;
  TOMLValue value;

  CU_ASSERT_EQUAL_FATAL(TOML_parse_number(&ctx, &value), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(value.integer, 3475);
  CU_ASSERT_EQUAL_FATAL(position.offset, 5);
  CU_ASSERT_EQUAL_FATAL(position.column, 5);

  ctx = make_toml(" 84.378", 1);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_number(&ctx, &value), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_FLOAT);
  CU_ASSERT_EQUAL_FATAL(value.float_, 84.378);
  CU_ASSERT_EQUAL_FATAL(position.offset, 7);
  CU_ASSERT_EQUAL_FATAL(position.column, 7);

  ctx = make_toml(" +126_583", 1);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_number(&ctx, &value), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(value.integer, 126583);
  CU_ASSERT_EQUAL_FATAL(position.offset, 9);
  CU_ASSERT_EQUAL_FATAL(position.column, 9);

  ctx = make_toml("0xff", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_number(&ctx, &value), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(value.integer, 0xff);
  CU_ASSERT_EQUAL_FATAL(position.offset, 4);
  CU_ASSERT_EQUAL_FATAL(position.column, 4);

  ctx = make_toml("12_345", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_number(&ctx, &value), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(value.integer, 12345);
  CU_ASSERT_EQUAL_FATAL(position.offset, 6);
  CU_ASSERT_EQUAL_FATAL(position.column, 6);

  ctx = make_toml(".1e+3", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_number(&ctx, &value), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_FLOAT);
  CU_ASSERT_EQUAL_FATAL(value.float_, .1e3);
  CU_ASSERT_EQUAL_FATAL(position.offset, 5);
  CU_ASSERT_EQUAL_FATAL(position.column, 5);
}
void test_parse_sl_string(void)
{
  TOMLCtx ctx = make_toml("\"ah yes, parsing\\n\"", 0);
  TOMLPosition position;
  String str = NULL;
  CU_ASSERT_EQUAL_FATAL(TOML_parse_sl_string(&ctx, &str), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_STRING_EQUAL_FATAL(str, "ah yes, parsing\n");
  CU_ASSERT_EQUAL_FATAL(position.offset, 19);
  CU_ASSERT_EQUAL_FATAL(position.column, 19);
  String_cleanup(str);
  str = NULL;

  ctx = make_toml("'literal strings go brrr\\n'", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_sl_string(&ctx, &str), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_STRING_EQUAL_FATAL(str, "literal strings go brrr\\n");
  CU_ASSERT_EQUAL_FATAL(position.offset, 27);
  CU_ASSERT_EQUAL_FATAL(position.column, 27);
  String_cleanup(str);
  str = NULL;

  ctx = make_toml("\"\\u33\"", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_sl_string(&ctx, &str), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_STRING_EQUAL_FATAL(str, "\x33");
  CU_ASSERT_EQUAL_FATAL(position.offset, 6);
  CU_ASSERT_EQUAL_FATAL(position.column, 6);
  String_cleanup(str);
}

void test_parse_ml_string(void)
{
  TOMLCtx ctx = make_toml("\"\"\"multi\nline\"\"\"", 0);
  String str = NULL;
  TOMLPosition position;
  CU_ASSERT_EQUAL_FATAL(TOML_parse_ml_string(&ctx, &str), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_STRING_EQUAL_FATAL(str, "multi\nline");
  CU_ASSERT_EQUAL_FATAL(position.offset, 16);
  CU_ASSERT_EQUAL_FATAL(position.column, 7);
  String_cleanup(str);
  str = NULL;

  ctx = make_toml("\"\"\"foo \\   \n bar\"\"\"", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_ml_string(&ctx, &str), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_STRING_EQUAL_FATAL(str, "foo bar");
  CU_ASSERT_EQUAL_FATAL(position.offset, 19);
  CU_ASSERT_EQUAL_FATAL(position.column, 7);
  String_cleanup(str);
  str = NULL;

  ctx = make_toml("'''multiline\nliteral\nstring'''", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_ml_string(&ctx, &str), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_STRING_EQUAL_FATAL(str, "multiline\nliteral\nstring");
  CU_ASSERT_EQUAL_FATAL(position.offset, 30);
  CU_ASSERT_EQUAL_FATAL(position.column, 9);
  String_cleanup(str);
}

void test_parse_time(void)
{
  TOMLCtx ctx = make_toml("17:53:23", 0);
  TOMLTime time;
  CU_ASSERT_EQUAL_FATAL(TOML_parse_time(&ctx, &time), TOML_E_OK);
  TOMLPosition position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 8);
  CU_ASSERT_EQUAL_FATAL(position.column, 8);
  CU_ASSERT_EQUAL_FATAL(time.hour, 17);
  CU_ASSERT_EQUAL_FATAL(time.min, 53);
  CU_ASSERT_EQUAL_FATAL(time.sec, 23);

  ctx = make_toml("18:24:13Z", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_time(&ctx, &time), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 9);
  CU_ASSERT_EQUAL_FATAL(position.column, 9);
  CU_ASSERT_EQUAL_FATAL(time.hour, 18);
  CU_ASSERT_EQUAL_FATAL(time.min, 24);
  CU_ASSERT_EQUAL_FATAL(time.sec, 13);
  CU_ASSERT_EQUAL_FATAL(time.z[0], 'Z');

  ctx = make_toml("18:24:13+01", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_time(&ctx, &time), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 11);
  CU_ASSERT_EQUAL_FATAL(position.column, 11);
  CU_ASSERT_EQUAL_FATAL(time.hour, 18);
  CU_ASSERT_EQUAL_FATAL(time.min, 24);
  CU_ASSERT_EQUAL_FATAL(time.sec, 13);
  CU_ASSERT_EQUAL_FATAL(time.z[0], '+');
  CU_ASSERT_EQUAL_FATAL(time.z[1], 1);

  ctx = make_toml("18:37:47-03:30", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_time(&ctx, &time), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 14);
  CU_ASSERT_EQUAL_FATAL(position.column, 14);
  CU_ASSERT_EQUAL_FATAL(time.hour, 18);
  CU_ASSERT_EQUAL_FATAL(time.min, 37);
  CU_ASSERT_EQUAL_FATAL(time.sec, 47);
  CU_ASSERT_EQUAL_FATAL(time.z[0], '-');
  CU_ASSERT_EQUAL_FATAL(time.z[1], 3);
  CU_ASSERT_EQUAL_FATAL(time.z[2], 30);

  ctx = make_toml("18:45:28.632", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_time(&ctx, &time), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 12);
  CU_ASSERT_EQUAL_FATAL(position.column, 12);
  CU_ASSERT_EQUAL_FATAL(time.hour, 18);
  CU_ASSERT_EQUAL_FATAL(time.min, 45);
  CU_ASSERT_EQUAL_FATAL(time.sec, 28);
  CU_ASSERT_EQUAL_FATAL(time.millisec, 632);

  ctx = make_toml("18:45:28.57453", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_time(&ctx, &time), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 14);
  CU_ASSERT_EQUAL_FATAL(position.column, 14);
  CU_ASSERT_EQUAL_FATAL(time.hour, 18);
  CU_ASSERT_EQUAL_FATAL(time.min, 45);
  CU_ASSERT_EQUAL_FATAL(time.sec, 28);
  CU_ASSERT_EQUAL_FATAL(time.millisec, 57453);

  ctx = make_toml("18:54:00.12Z", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_time(&ctx, &time), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 12);
  CU_ASSERT_EQUAL_FATAL(position.column, 12);
  CU_ASSERT_EQUAL_FATAL(time.hour, 18);
  CU_ASSERT_EQUAL_FATAL(time.min, 54);
  CU_ASSERT_EQUAL_FATAL(time.sec, 0);
  CU_ASSERT_EQUAL_FATAL(time.millisec, 12);
  CU_ASSERT_EQUAL_FATAL(time.z[0], 'Z');
}

void test_parse_datetime(void)
{
  TOMLCtx ctx = make_toml("2021-12-03", 0);
  TOMLValue value = {0};
  CU_ASSERT_EQUAL_FATAL(TOML_parse_datetime(&ctx, &value), TOML_E_OK);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_DATE);
  TOMLPosition position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 10);
  CU_ASSERT_EQUAL_FATAL(position.column, 10);
  CU_ASSERT_EQUAL_FATAL(value.date.year, 2021);
  CU_ASSERT_EQUAL_FATAL(value.date.month, 12);
  CU_ASSERT_EQUAL_FATAL(value.date.day, 3);

  ctx = make_toml("2021-12-03T00:00:00", 0);
  value = (TOMLValue) {0};
  CU_ASSERT_EQUAL_FATAL(TOML_parse_datetime(&ctx, &value), TOML_E_OK);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_DATETIME);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 19);
  CU_ASSERT_EQUAL_FATAL(position.column, 19);
  CU_ASSERT_EQUAL_FATAL(value.date.year, 2021);
  CU_ASSERT_EQUAL_FATAL(value.date.month, 12);

  ctx = make_toml("2021-12-03 00:00:00", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_datetime(&ctx, &value), TOML_E_OK);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_DATETIME);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 19);
  CU_ASSERT_EQUAL_FATAL(position.column, 19);
  CU_ASSERT_EQUAL_FATAL(value.date.year, 2021);
  CU_ASSERT_EQUAL_FATAL(value.date.month, 12);
}
void test_parse_array(void)
{
  TOMLCtx ctx = make_toml("[\"foo\", 3, 43.53]", 0);
  TOMLPosition position;
  TOMLArray arr = NULL;
  CU_ASSERT_EQUAL_FATAL(TOML_parse_array(&ctx, &arr), TOML_E_OK);
  position = TOML_position(&ctx);

  CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr), 3);

  CU_ASSERT_EQUAL_FATAL(arr[0].kind, TOML_STRING);
  CU_ASSERT_STRING_EQUAL_FATAL(arr[0].string, "foo");

  CU_ASSERT_EQUAL_FATAL(arr[1].kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(arr[1].integer, 3);

  CU_ASSERT_EQUAL_FATAL(arr[2].kind, TOML_FLOAT);
  CU_ASSERT_EQUAL_FATAL(arr[2].float_, 43.53);

  CU_ASSERT_EQUAL_FATAL(position.offset, 17);
  CU_ASSERT_EQUAL_FATAL(position.column, 17);
  TOMLArray_destroy(arr);

  arr = NULL;
  ctx = make_toml(
    "[44, 3433, [\"bar\", 3.4339], 232.9, 977, [[38, 88], \n"
    "[\"nested\", [\"more nested\", 34.38], 96.65, 836521], 232],\n"
    "\"a lot of nesting, don't you think so?\", [true, false]]",
    0
  );
  CU_ASSERT_EQUAL_FATAL(TOML_parse_array(&ctx, &arr), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr), 8);

  CU_ASSERT_EQUAL_FATAL(arr[0].kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(arr[0].integer, 44);

  CU_ASSERT_EQUAL_FATAL(arr[1].kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(arr[1].integer, 3433);

  // Asserting nested array and its items.
  CU_ASSERT_EQUAL_FATAL(arr[2].kind, TOML_ARRAY);
  TOMLArray arr1 = arr[2].array;
  CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr1), 2);

  CU_ASSERT_EQUAL_FATAL(arr1[0].kind, TOML_STRING);
  CU_ASSERT_STRING_EQUAL_FATAL(arr1[0].string, "bar");

  CU_ASSERT_EQUAL_FATAL(arr1[1].kind, TOML_FLOAT);
  CU_ASSERT_EQUAL_FATAL(arr1[1].float_, 3.4339);

  CU_ASSERT_EQUAL_FATAL(arr[3].kind, TOML_FLOAT);
  CU_ASSERT_EQUAL_FATAL(arr[3].float_, 232.9);

  CU_ASSERT_EQUAL_FATAL(arr[4].kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(arr[4].integer, 977);

  CU_ASSERT_EQUAL_FATAL(arr[5].kind, TOML_ARRAY);
  TOMLArray arr2 = arr[5].array;
  CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr2), 3);

  CU_ASSERT_EQUAL_FATAL(arr2[0].kind, TOML_ARRAY);
  TOMLArray arr3 = arr2[0].array;
  CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr3), 2);

  CU_ASSERT_EQUAL_FATAL(arr3[0].kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(arr3[0].integer, 38);

  CU_ASSERT_EQUAL_FATAL(arr3[1].kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(arr3[1].integer, 88);

  CU_ASSERT_EQUAL_FATAL(arr2[1].kind, TOML_ARRAY);
  TOMLArray arr4 = arr2[1].array;
  CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr4), 4);

  CU_ASSERT_EQUAL_FATAL(arr4[0].kind, TOML_STRING);
  CU_ASSERT_STRING_EQUAL_FATAL(arr4[0].string, "nested");

  CU_ASSERT_EQUAL_FATAL(arr4[1].kind, TOML_ARRAY);
  TOMLArray arr5 = arr4[1].array;
  CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr5), 2);

  CU_ASSERT_EQUAL_FATAL(arr5[0].kind, TOML_STRING);
  CU_ASSERT_STRING_EQUAL_FATAL(arr5[0].string, "more nested");

  CU_ASSERT_EQUAL_FATAL(arr5[1].kind, TOML_FLOAT);
  CU_ASSERT_EQUAL_FATAL(arr5[1].float_, 34.38);

  CU_ASSERT_EQUAL_FATAL(arr4[2].kind, TOML_FLOAT);
  CU_ASSERT_EQUAL_FATAL(arr4[2].float_, 96.65);

  CU_ASSERT_EQUAL_FATAL(arr4[3].kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(arr4[3].integer, 836521);

  CU_ASSERT_EQUAL_FATAL(arr2[2].kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(arr2[2].integer, 232);

  CU_ASSERT_EQUAL_FATAL(arr[6].kind, TOML_STRING);
  CU_ASSERT_STRING_EQUAL_FATAL(arr[6].string,
                               "a lot of nesting, don't you think so?");

  CU_ASSERT_EQUAL_FATAL(arr[7].kind, TOML_ARRAY);
  TOMLArray arr6 = arr[7].array;
  CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr6), 2);
  CU_ASSERT_EQUAL_FATAL(arr6[0].kind, TOML_BOOLEAN);
  CU_ASSERT_TRUE_FATAL(arr6[0].boolean);
  CU_ASSERT_EQUAL_FATAL(arr6[1].kind, TOML_BOOLEAN);
  CU_ASSERT_TRUE_FATAL(!arr6[1].boolean);

  TOMLArray_destroy(arr);

  CU_ASSERT_EQUAL_FATAL(position.offset, 164);
  CU_ASSERT_EQUAL_FATAL(position.column, 55);
  CU_ASSERT_EQUAL_FATAL(position.line, 3);

  ctx = make_toml("[2021-12-11, 17:16:00+01]", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_array(&ctx, &arr), TOML_E_OK);
  position = TOML_position(&ctx);
  CU_ASSERT_EQUAL_FATAL(position.offset, 25);
  CU_ASSERT_EQUAL_FATAL(position.column, 25);
  CU_ASSERT_EQUAL_FATAL(position.line, 1);

  CU_ASSERT_EQUAL_FATAL(arr[0].kind, TOML_DATE);
  CU_ASSERT_EQUAL_FATAL(arr[0].date.year, 2021);
  CU_ASSERT_EQUAL_FATAL(arr[0].date.month, 12);
  CU_ASSERT_EQUAL_FATAL(arr[0].date.day, 11);

  CU_ASSERT_EQUAL_FATAL(arr[1].kind, TOML_TIME);
  CU_ASSERT_EQUAL_FATAL(arr[1].time.hour, 17);
  CU_ASSERT_EQUAL_FATAL(arr[1].time.min, 16);
  CU_ASSERT_EQUAL_FATAL(arr[1].time.sec, 0);
  CU_ASSERT_EQUAL_FATAL(arr[1].time.z[0], '+');
  CU_ASSERT_EQUAL_FATAL(arr[1].time.z[1], 1);
  TOMLArray_destroy(arr);
}

void test_parse_entry(void)
{
  TOMLTable table = TOMLTable_with_size(4);
  CU_ASSERT_PTR_NOT_NULL_FATAL(table);
  TOMLCtx ctx = make_toml("a = 45", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_entry(&ctx, &table), TOML_E_OK);
  TOMLValue const *val_p = TBLGET(table, "a");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(val_p->integer, 45);
  TOMLTable_destroy(table);

  table = TOMLTable_with_size(4);
  ctx = make_toml("foo.bar = \"bazzuka\"", 0);
  // system("clear");
  CU_ASSERT_EQUAL_FATAL(TOML_parse_entry(&ctx, &table), TOML_E_OK);
  val_p = TBLGET(table, "foo");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p->table);
  do {
    TOMLValue const *val_p2 = TBLGET(val_p->table, "bar");
    CU_ASSERT_PTR_NOT_NULL_FATAL(val_p2);
    CU_ASSERT_EQUAL_FATAL(val_p2->kind, TOML_STRING);
    CU_ASSERT_STRING_EQUAL_FATAL(val_p2->string, "bazzuka");

  } while (0);
  TOMLTable_destroy(table);

  table = TOMLTable_with_size(4);
  ctx = make_toml("\"hello world\".'hmmm\\n' = [true, false]", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_entry(&ctx, &table), TOML_E_OK);
  val_p = TBLGET(table, "hello world");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p->table);
  do {
    TOMLValue const *val_p2 = TOMLTable_get(val_p->table,
                                            String_fake("hmmm\\n"));
    CU_ASSERT_PTR_NOT_NULL_FATAL(val_p2);
    CU_ASSERT_EQUAL_FATAL(val_p2->kind, TOML_ARRAY);
    TOMLArray array = val_p2->array;
    CU_ASSERT_PTR_NOT_NULL_FATAL(array);
    CU_ASSERT_EQUAL_FATAL(TOMLArray_len(array), 2);
    CU_ASSERT_EQUAL_FATAL(array[0].kind, TOML_BOOLEAN);
    CU_ASSERT_EQUAL_FATAL(array[1].kind, TOML_BOOLEAN);
    CU_ASSERT_FATAL(array[0].boolean);
    CU_ASSERT_FATAL(!array[1].boolean);
  } while (0);

  TOMLTable_destroy(table);
}

void test_parse_inline_table(void)
{
  TOMLCtx ctx = make_toml("{ foo = 69 }", 0);
  TOMLTable table = TOMLTable_new();
  CU_ASSERT_EQUAL_FATAL(TOML_parse_inline_table(&ctx, &table), TOML_E_OK);
  TOMLValue const *val_p = TBLGET(table, "foo");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_INTEGER);
  CU_ASSERT_EQUAL_FATAL(val_p->integer, 69);
  TOMLTable_destroy(table);

  ctx = make_toml("{ foo = \"bar\", baz = 420.0 }", 0);
  table = TOMLTable_new();
  CU_ASSERT_EQUAL_FATAL(TOML_parse_inline_table(&ctx, &table), TOML_E_OK);
  val_p = TBLGET(table, "foo");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_STRING);
  CU_ASSERT_STRING_EQUAL_FATAL(val_p->string, "bar");
  val_p = TBLGET(table, "baz");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_FLOAT);
  CU_ASSERT_EQUAL_FATAL(val_p->float_, 420);
  TOMLTable_destroy(table);

  ctx = make_toml("{ foo.bar = \"baz\" }", 0);
  table = TOMLTable_new();
  CU_ASSERT_EQUAL_FATAL(TOML_parse_inline_table(&ctx, &table), TOML_E_OK);
  val_p = TBLGET(table, "foo");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_TABLE);
  val_p = TBLGET(val_p->table, "bar");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_STRING);
  CU_ASSERT_STRING_EQUAL_FATAL(val_p->string, "baz");
  TOMLTable_destroy(table);

  fflush(stdout);

  ctx = make_toml("                            \n\
                   {                           \n\
                     foo = { bar = \"bag\" },  \n\
                     hello.world = true,       \n\
                     a.\"b\".'c' = [nan, -inf] \n\
                   }", 69);
  table = TOMLTable_new();
  CU_ASSERT_EQUAL_FATAL(TOML_parse_inline_table(&ctx, &table), TOML_E_OK);
  val_p = TBLGET(table, "foo");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_INLINE_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TOMLTable_get(val_p->table,
                               String_fake("bar"))); // foo.bar
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(val_p->table, "bar")->kind,
      TOML_STRING
  );
  CU_ASSERT_STRING_EQUAL_FATAL(
      TBLGET(val_p->table, "bar")->string,
      "bag"
  );
  val_p = TBLGET(table, "hello");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TOMLTable_get(val_p->table,
                               String_fake("world")));
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(val_p->table, "world")->kind,
      TOML_BOOLEAN
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(val_p->table, "world")->boolean,
      true
  );
  val_p = TBLGET(table, "a");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_TABLE);
  val_p = TBLGET(val_p->table, "b");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_TABLE);
  val_p = TBLGET(val_p->table, "c");
  CU_ASSERT_PTR_NOT_NULL_FATAL(val_p);
  CU_ASSERT_EQUAL_FATAL(val_p->kind, TOML_ARRAY);
  CU_ASSERT_EQUAL_FATAL(val_p->array[0].kind, TOML_FLOAT);
  CU_ASSERT_FATAL(__builtin_isnan(val_p->array[0].float_));
  CU_ASSERT_EQUAL_FATAL(val_p->array[1].kind, TOML_FLOAT);
  CU_ASSERT_FATAL(__builtin_isinf(val_p->array[1].float_));
  TOMLTable_destroy(table);
}

void test_parse_table_header(void)
{
  TOMLCtx ctx;
  TOMLTable table = TOMLTable_new();

  ctx = make_toml("[foo]", 1);
  TOMLTable *foo_table = NULL;
  CU_ASSERT_EQUAL_FATAL(
      TOML_parse_table_header(&ctx, &table, &foo_table, 0), TOML_E_OK
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(foo_table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "foo")->table);
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(table, "foo")->kind, TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(table, "foo")->table
  );
  CU_ASSERT_PTR_EQUAL_FATAL(
      foo_table,
      &(TBLGET(table, "foo")->table)
  );

  ctx = make_toml("[bar.baz]", 1);
  TOMLTable *baz_table = NULL;
  CU_ASSERT_EQUAL_FATAL(
      TOML_parse_table_header(&ctx, &table, &baz_table, 0), TOML_E_OK
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(baz_table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "bar")->table);
  CU_ASSERT_EQUAL_FATAL(TBLGET(table, "bar")->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "bar")->table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "baz")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "baz")->kind, TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "baz")->table);
  CU_ASSERT_PTR_EQUAL_FATAL(
      &(TBLGET(TBLGET(table, "bar")->table, "baz")->table), baz_table
  );

  ctx = make_toml("['hello'.'''\nworld'''.-._]", 1);
  TOMLTable *underscore_table = NULL;
  CU_ASSERT_EQUAL_FATAL(
      TOML_parse_table_header(&ctx, &table, &underscore_table, 0),
      TOML_E_OK
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(underscore_table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "hello")->table);
  CU_ASSERT_EQUAL_FATAL(TBLGET(table, "hello")->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "hello")->table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "hello")->table, "\nworld")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "hello")->table, "\nworld")->kind, TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "hello")->table, "\nworld")->table
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "hello")->table, "\nworld")->table, "-"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "hello")->table, "\nworld")->table, "-"
      )->kind,
      TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "hello")->table, "\nworld")->table, "-"
      )->table
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "hello")->table, "\nworld")->table, "-"
        )->table,
        "_"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "hello")->table, "\nworld")->table, "-"
        )->table,
        "_"
      )->kind,
      TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "hello")->table, "\nworld")->table, "-"
        )->table,
        "_"
      )->table
  );
  CU_ASSERT_PTR_EQUAL_FATAL(
      &(TBLGET(
         TBLGET(
           TBLGET(TBLGET(table, "hello")->table, "\nworld")->table, "-"
         )->table,
         "_"
       )->table),
      underscore_table
  );

  ctx = make_toml("[[\"\"\"a\\nb\"\"\"]]", 2);
  TOMLTable *ab_table = NULL;
  CU_ASSERT_EQUAL_FATAL(
      TOML_parse_table_header(&ctx, &table, &ab_table, 1), TOML_E_OK
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(ab_table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "a\nb")->table);
  CU_ASSERT_EQUAL_FATAL(TBLGET(table, "a\nb")->kind, TOML_TABLE_ARRAY);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "a\nb")->array);
  CU_ASSERT_EQUAL_FATAL(
      TOMLArray_len(TBLGET(table, "a\nb")->array), 1
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(table, "a\nb")->array[0].kind, TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "a\nb")->array[0].table);
  CU_ASSERT_EQUAL_FATAL(
      &(TBLGET(table, "a\nb")->array[0].table), ab_table
  );

  ctx = make_toml("[bar.buz]", 1);
  TOMLTable *buz_table = NULL;
  CU_ASSERT_EQUAL_FATAL(
      TOML_parse_table_header(&ctx, &table, &buz_table, 0), TOML_E_OK
  );

  CU_ASSERT_PTR_NOT_NULL_FATAL(buz_table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "buz")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "buz")->kind, TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "buz")->table
  );
  CU_ASSERT_EQUAL_FATAL(
      &(TBLGET(TBLGET(table, "bar")->table, "buz")->table), buz_table
  );

  ctx = make_toml("[[foo.boz]]", 2);
  TOMLTable *boz_table = NULL;
  CU_ASSERT_EQUAL_FATAL(
      TOML_parse_table_header(&ctx, &table, &boz_table, 1), TOML_E_OK
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(boz_table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "boz")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "boz")->kind, TOML_TABLE_ARRAY
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "boz")->array
  );
  CU_ASSERT_EQUAL_FATAL(
      TOMLArray_len(TBLGET(TBLGET(table, "foo")->table, "boz")->array), 1
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "boz")->array[0].kind, TOML_TABLE
  );
  CU_ASSERT_EQUAL_FATAL(
      &(TBLGET(TBLGET(table, "foo")->table, "boz")->array[0].table), boz_table
  );

  ctx = make_toml("[[foo.boz]]", 2);
  TOMLTable *boz2_table = NULL;
  CU_ASSERT_EQUAL_FATAL(
      TOML_parse_table_header(&ctx, &table, &boz2_table, 1), TOML_E_OK
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(boz2_table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "boz")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "boz")->kind, TOML_TABLE_ARRAY
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "boz")->array
  );
  CU_ASSERT_EQUAL_FATAL(
      TOMLArray_len(TBLGET(TBLGET(table, "foo")->table, "boz")->array), 2
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "boz")->array[1].kind, TOML_TABLE
  );
  CU_ASSERT_EQUAL_FATAL(
      &(TBLGET(TBLGET(table, "foo")->table, "boz")->array[1].table), boz2_table
  );

  TOMLTable_destroy(table);
}

void test_parse_table(void)
{
  TOMLCtx ctx;
  TOMLTable table = TOMLTable_new();

  ctx = make_toml("[foo]\n"
                  "bar = \"baz\"", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_table(&ctx, &table), TOML_E_OK);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "foo"));
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "foo")->table);
  CU_ASSERT_EQUAL_FATAL(TBLGET(table, "foo")->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "foo")->table);
  CU_ASSERT_EQUAL_FATAL(TOMLTable_count(TBLGET(table, "foo")->table), 1);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "bar")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "bar")->kind, TOML_STRING
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "bar")->string
  );
  CU_ASSERT_FATAL(
      String_equal(
        TBLGET(TBLGET(table, "foo")->table, "bar")->string, String_fake("baz")
      )
  );

  ctx = make_toml("[[foo.buz]]\n"
                  "boz.biz = 18:45:26+03\n"
                  "boz.bez = 42", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_table(&ctx, &table), TOML_E_OK);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "foo"));
  CU_ASSERT_EQUAL_FATAL(TBLGET(table, "foo")->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "foo")->table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "buz")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "buz")->kind, TOML_TABLE_ARRAY
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "buz")->array
  );
  CU_ASSERT_EQUAL_FATAL(
      TOMLArray_len(TBLGET(TBLGET(table, "foo")->table, "buz")->array), 1
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].kind,
      TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
        "boz"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
        "boz"
      )->kind,
      TOML_TABLE
  );
  CU_ASSERT_EQUAL_FATAL(
      TOMLTable_count(
        TBLGET(
          TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
          "boz"
        )->table
      ),
      2
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
          "boz"
        )->table,
        "biz"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
          "boz"
        )->table,
        "biz"
      )->kind,
      TOML_TIME
  );
  CU_ASSERT_EQUAL_FATAL(
      memcmp(
        &(
          TBLGET(
            TBLGET(
              TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
              "boz"
            )->table,
            "biz"
          )->time
        ),
        &(TOMLTime) {
          .millisec = 0,
          .hour     = 18,
          .min      = 45,
          .sec      = 26,
          .z        = {'+', 3, -1}
        },
        sizeof(TOMLTime)
      ),
      0
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
          "boz"
        )->table,
        "bez"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
          "boz"
        )->table,
        "bez"
      )->kind,
      TOML_INTEGER
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "foo")->table, "buz")->array[0].table,
          "boz"
        )->table,
        "bez"
      )->integer,
      42
  );

  ctx = make_toml("[[foo.buz]]\n"
                  "oof.rab = \"zab\"", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse_table(&ctx, &table), TOML_E_OK);
  CU_ASSERT_EQUAL_FATAL(
      TOMLArray_len(TBLGET(TBLGET(table, "foo")->table, "buz")->array), 2
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "buz")->array[1].kind,
      TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "foo")->table, "buz")->array[1].table
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "foo")->table, "buz")->array[1].table,
        "oof"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "foo")->table, "buz")->array[1].table,
        "oof"
      )->kind,
      TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "foo")->table, "buz")->array[1].table,
          "oof"
        )->table,
        "rab"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "foo")->table, "buz")->array[1].table,
          "oof"
        )->table,
        "rab"
      )->kind,
      TOML_STRING
  );
  CU_ASSERT_FATAL(
      String_equal(
        TBLGET(
          TBLGET(
            TBLGET(TBLGET(table, "foo")->table, "buz")->array[1].table,
            "oof"
          )->table,
          "rab"
        )->string,
        String_fake("zab")
      )
  );

  TOMLTable_destroy(table);
}

void test_parse(void)
{
  TOMLCtx ctx;
  TOMLTable table = TOMLTable_new();

  ctx = make_toml("foo = \"bar\"\n"
                  "# comment\n"
                  "[bar]\n"
                  "baz = 42\n"
                  "[['a'.\"b c\"]]\n"
                  "d.e = 13.62\n"
                  "off = true\n"
                  "[['a'.\"b c\"]]\n"
                  "on = false\n"
                  "inf = inf\n"
                  "nan = nan", 0);
  CU_ASSERT_EQUAL_FATAL(TOML_parse(&ctx, &table), TOML_E_OK);
  CU_ASSERT_EQUAL_FATAL(TOMLTable_count(table), 3);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "foo"));
  CU_ASSERT_EQUAL_FATAL(TBLGET(table, "foo")->kind, TOML_STRING);
  CU_ASSERT_FATAL(
      String_equal(TBLGET(table, "foo")->string, String_fake("bar"))
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "bar"));
  CU_ASSERT_EQUAL_FATAL(TBLGET(table, "bar")->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "bar")->table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "baz")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "baz")->kind, TOML_INTEGER
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "bar")->table, "baz")->integer, 42
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "a"));
  CU_ASSERT_EQUAL_FATAL(TBLGET(table, "a")->kind, TOML_TABLE);
  CU_ASSERT_PTR_NOT_NULL_FATAL(TBLGET(table, "a")->table);
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "a")->table, "b c")
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "a")->table, "b c")->kind, TOML_TABLE_ARRAY
  );
  CU_ASSERT_EQUAL_FATAL(
      TOMLArray_len(TBLGET(TBLGET(table, "a")->table, "b c")->array), 2
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "a")->table, "b c")->array[0].kind,
      TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "d"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "d"
      )->kind,
      TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "d"
      )->table
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "d"
        )->table,
        "e"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "d"
        )->table,
        "e"
      )->kind,
      TOML_FLOAT
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(
          TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "d"
        )->table,
        "e"
      )->float_,
      13.62
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "d"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "off"
      )->kind,
      TOML_BOOLEAN
  );
  CU_ASSERT_TRUE_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[0].table, "off"
      )->boolean
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(TBLGET(table, "a")->table, "b c")->array[1].kind,
      TOML_TABLE
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "on"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "on"
      )->kind,
      TOML_BOOLEAN
  );
  CU_ASSERT_FALSE_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "on"
      )->boolean
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "inf"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "inf"
      )->kind,
      TOML_FLOAT
  );
  CU_ASSERT_FATAL(
      __builtin_isinf(
        TBLGET(
          TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "inf"
        )->float_
      )
  );
  CU_ASSERT_PTR_NOT_NULL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "nan"
      )
  );
  CU_ASSERT_EQUAL_FATAL(
      TBLGET(
        TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "nan"
      )->kind,
      TOML_FLOAT
  );
  CU_ASSERT_FATAL(
      __builtin_isnan(
        TBLGET(
          TBLGET(TBLGET(table, "a")->table, "b c")->array[1].table, "nan"
        )->float_
      )
  );

  TOMLTable_destroy(table);
}

int main(int argc, char **argv)
{
  int status = 0;
  CU_TestInfo tests[] = {
    { "#parse_number",       test_parse_number       },
    { "#parse_sl_string",    test_parse_sl_string    },
    { "#parse_ml_string",    test_parse_ml_string    },
    { "#parse_time",         test_parse_time         },
    { "#parse_datetime",     test_parse_datetime     },
    { "#parse_array",        test_parse_array        },
    { "#parse_entry",        test_parse_entry        },
    { "#parse_inline_table", test_parse_inline_table },
    { "#parse_table_header", test_parse_table_header },
    { "#parse_table",        test_parse_table        },
    { "#parse",              test_parse              },
    CU_TEST_INFO_NULL
  };
  CU_SuiteInfo suites[] = {
    { "TOML", NULL, NULL, NULL, NULL, tests },
    CU_SUITE_INFO_NULL
  };
  CU_initialize_registry();
  if (CU_register_suites(suites) != CUE_SUCCESS)
  {
    status = 1;
  } else
  {
    RUN_TESTS
  }
cleanup:
  CU_cleanup_registry();
  return status;
}
