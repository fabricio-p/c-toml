#include "testing.h"
#include "lib.h"
#include "util.h"
#include "errors.h"

static inline __attribute__((always_inline))
TOMLCtx make_toml(char const *const data, int const offset)
{
	return (TOMLCtx) {
		.file_path = (String)"<<memory>>",
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

	TOML_parse_number(&ctx, &value);
	position = TOML_position(&ctx);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(value.integer, 3475);
	CU_ASSERT_EQUAL_FATAL(position.offset, 5);
	CU_ASSERT_EQUAL_FATAL(position.column, 5);

	ctx = make_toml(" 84.378", 1);
	CU_ASSERT_EQUAL_FATAL(TOML_parse_number(&ctx, &value), TOML_E_OK);
  // puts(ctx.offset);
	position = TOML_position(&ctx);
  // puts(type_name(&value));
  // printf("%d\n", value.integer);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(value.float_, 84.378);
	CU_ASSERT_EQUAL_FATAL(position.offset, 7);
	CU_ASSERT_EQUAL_FATAL(position.column, 7);
	
	ctx = make_toml(" +126_583", 1);
	TOML_parse_number(&ctx, &value);
	position = TOML_position(&ctx);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(value.integer, 126583);
	CU_ASSERT_EQUAL_FATAL(position.offset, 9);
	CU_ASSERT_EQUAL_FATAL(position.column, 9);

	ctx = make_toml("0xff", 0);
	TOML_parse_number(&ctx, &value);
	position = TOML_position(&ctx);
	// printf("%s\n", type_name(&value));
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_INTEGER);
	// printf("%d %x\n", value.integer, value.integer);
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
  // puts(ctx.offset);
	position = TOML_position(&ctx);
  // puts(type_name(&value));
  // printf("%d\n", value.integer);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(value.float_, .1e3);
	CU_ASSERT_EQUAL_FATAL(position.offset, 5);
	CU_ASSERT_EQUAL_FATAL(position.column, 5);
}
void test_parse_sl_string(void)
{
	TOMLCtx ctx = make_toml("\"ah yes, parsing\\n\"", 0);
	TOMLPosition position;
	String str;
	CU_ASSERT_EQUAL_FATAL(TOML_parse_sl_string(&ctx, &str), TOML_E_OK);
	position = TOML_position(&ctx);
	CU_ASSERT_STRING_EQUAL_FATAL(str, "ah yes, parsing\n");
	CU_ASSERT_EQUAL_FATAL(position.offset, 19);
	CU_ASSERT_EQUAL_FATAL(position.column, 19);
	String_cleanup(str);

	ctx =  make_toml("'literal strings go brrr\\n'", 0);
	CU_ASSERT_EQUAL_FATAL(TOML_parse_sl_string(&ctx, &str), TOML_E_OK);
	position = TOML_position(&ctx);
	CU_ASSERT_STRING_EQUAL_FATAL(str, "literal strings go brrr\\n");
	CU_ASSERT_EQUAL_FATAL(position.offset, 27);
	CU_ASSERT_EQUAL_FATAL(position.column, 27);
	String_cleanup(str);
}

void test_parse_ml_string(void)
{
	TOMLCtx ctx = make_toml("\"\"\"multi\nline\"\"\"", 0);
  String str;
  TOMLPosition position;
	CU_ASSERT_EQUAL_FATAL(TOML_parse_ml_string(&ctx, &str), TOML_E_OK);
	position = TOML_position(&ctx);
	CU_ASSERT_STRING_EQUAL_FATAL(str, "multi\nline");
	CU_ASSERT_EQUAL_FATAL(position.offset, 16);
	CU_ASSERT_EQUAL_FATAL(position.column, 7);
	String_cleanup(str);

	ctx = make_toml("\"\"\"foo \\   \n bar\"\"\"", 0);
	CU_ASSERT_EQUAL_FATAL(TOML_parse_ml_string(&ctx, &str), TOML_E_OK);
	position = TOML_position(&ctx);
	CU_ASSERT_STRING_EQUAL_FATAL(str, "foo bar");
	CU_ASSERT_EQUAL_FATAL(position.offset, 19);
	CU_ASSERT_EQUAL_FATAL(position.column, 7);
	String_cleanup(str);

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
  CU_ASSERT_EQUAL_FATAL(time.millisec, 574);

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
  TOMLValue value;
  CU_ASSERT_EQUAL_FATAL(TOML_parse_datetime(&ctx, &value), TOML_E_OK);
  CU_ASSERT_EQUAL_FATAL(value.kind, TOML_DATE);
  TOMLPosition position = TOML_position(&ctx);
	CU_ASSERT_EQUAL_FATAL(position.offset, 10);
	CU_ASSERT_EQUAL_FATAL(position.column, 10);
  CU_ASSERT_EQUAL_FATAL(value.date.year, 2021);
  CU_ASSERT_EQUAL_FATAL(value.date.month, 12);
  CU_ASSERT_EQUAL_FATAL(value.date.day, 3);

  ctx = make_toml("2021-12-03T00:00:00", 0);
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
/* void test_parse_array(void)
{
	TOMLCtx ctx = make_toml("[\"foo\", 3, 43.53]", 0);
	TOMLPosition position;
	TOMLValue arr;
	TOML_parse_array(&ctx, &arr);
	position = TOML_position(&ctx);

	CU_ASSERT_EQUAL_FATAL(arr.kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr.array), 3);
		
	CU_ASSERT_EQUAL_FATAL(arr.array[0].kind, TOML_STRING);
	CU_ASSERT_STRING_EQUAL_FATAL(arr.array[0].string, "foo");

	CU_ASSERT_EQUAL_FATAL(arr.array[1].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(arr.array[1].integer, 3);

	CU_ASSERT_EQUAL_FATAL(arr.array[2].kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(arr.array[2].float_, 43.53);

	CU_ASSERT_EQUAL_FATAL(position.offset, 17);
	CU_ASSERT_EQUAL_FATAL(position.column, 17);
	String_cleanup(arr.array[0].string);
	TOMLArray_cleanup(arr.array);

	ctx = make_toml(
		"[44, 3433, [\"bar\", 3.4339], 232.9, 977, [[38, 88], \n"
		"[\"nested\", [\"more nested\", 34.38], 96.65, 836521], 232],\n"
		"\"a lot of nesting, don't you think so?\", [true, false]]",
		0
	);
	TOML_parse_array(&ctx, &arr);
	position = TOML_position(&ctx);
	// print_value(&arr, 0);
	CU_ASSERT_EQUAL_FATAL(arr.kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr.array), 8);

	CU_ASSERT_EQUAL_FATAL(arr.array[0].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(arr.array[0].integer, 44);

	CU_ASSERT_EQUAL_FATAL(arr.array[1].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(arr.array[1].integer, 3433);

	// Asserting nested array and it's items.
	TOMLValue *arr1 = &arr.array[2];
	CU_ASSERT_EQUAL_FATAL(arr1->kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr1->array), 2);

	CU_ASSERT_EQUAL_FATAL(arr1->array[0].kind, TOML_STRING);
	CU_ASSERT_STRING_EQUAL_FATAL(arr1->array[0].string, "bar");

	CU_ASSERT_EQUAL_FATAL(arr1->array[1].kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(arr1->array[1].float_, 3.4339);

	TOMLArray_cleanup(arr1->array);

	CU_ASSERT_EQUAL_FATAL(arr.array[3].kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(arr.array[3].float_, 232.9);

	CU_ASSERT_EQUAL_FATAL(arr.array[4].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(arr.array[4].integer, 977);

	TOMLValue *arr2 = &arr.array[5];
	CU_ASSERT_EQUAL_FATAL(arr2->kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr2->array), 3);

	TOMLValue *arr3 = &arr2->array[0];
	CU_ASSERT_EQUAL_FATAL(arr3->kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr3->array), 2);

	CU_ASSERT_EQUAL_FATAL(arr3->array[0].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(arr3->array[0].integer, 38);

	CU_ASSERT_EQUAL_FATAL(arr3->array[1].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(arr3->array[1].integer, 88);

	TOMLArray_cleanup(arr3->array);

	TOMLValue *arr4 = &arr2->array[1];
	CU_ASSERT_EQUAL_FATAL(arr4->kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr4->array), 4);

	CU_ASSERT_EQUAL_FATAL(arr4->array[0].kind, TOML_STRING);
	CU_ASSERT_STRING_EQUAL_FATAL(arr4->array[0].string, "nested");
	String_cleanup(arr4->array[0].string);

	TOMLValue *arr5 = &arr4->array[1];
	CU_ASSERT_EQUAL_FATAL(arr5->kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr5->array), 2);

	CU_ASSERT_EQUAL_FATAL(arr5->array[0].kind, TOML_STRING);
	CU_ASSERT_STRING_EQUAL_FATAL(arr5->array[0].string, "more nested");
	String_cleanup(arr5->array[0].string);

	CU_ASSERT_EQUAL_FATAL(arr5->array[1].kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(arr5->array[1].float_, 34.38);

	TOMLArray_cleanup(arr5->array);

	CU_ASSERT_EQUAL_FATAL(arr4->array[2].kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(arr4->array[2].float_, 96.65);

	CU_ASSERT_EQUAL_FATAL(arr4->array[3].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(arr4->array[3].integer, 836521);

	TOMLArray_cleanup(arr4->array);

	CU_ASSERT_EQUAL_FATAL(arr2->array[2].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(arr2->array[2].integer, 232);

	TOMLArray_cleanup(arr2->array);

	CU_ASSERT_EQUAL_FATAL(arr.array[6].kind, TOML_STRING);
	CU_ASSERT_STRING_EQUAL_FATAL(arr.array[6].string,
			"a lot of nesting, don't you think so?");

	TOMLArray_cleanup(arr.array);

	TOMLValue *arr6 = &arr.array[7];
	CU_ASSERT_EQUAL_FATAL(arr6->kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(arr6->array), 2);
	CU_ASSERT_EQUAL_FATAL(arr6->array[0].kind, TOML_BOOLEAN);
	CU_ASSERT_FATAL(arr6->array[0].boolean);
	CU_ASSERT_EQUAL_FATAL(arr6->array[1].kind, TOML_BOOLEAN);
	CU_ASSERT_FATAL(!arr6->array[1].boolean);

	TOMLArray_cleanup(arr6->array);

	CU_ASSERT_EQUAL_FATAL(position.offset, 164);
	CU_ASSERT_EQUAL_FATAL(position.column, 55);
	CU_ASSERT_EQUAL_FATAL(position.line, 3);
}
void test_parse_value(void)
{
	TOMLCtx ctx = make_toml(
						" 68359\n34.37546\n\"foo bar\\\\\\n\"\n"
						"[34, 5.987893, \"I'm getting tired of this\"]",
						1
					);
	TOMLPosition position;
	TOMLValue value;

	TOML_parse_value(&ctx, &value);
	++(ctx.offset);
	position = TOML_position(&ctx);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(value.integer, 68359);
	CU_ASSERT_EQUAL_FATAL(position.offset, 7);
	CU_ASSERT_EQUAL_FATAL(position.column, 0);
	CU_ASSERT_EQUAL_FATAL(position.line, 2);

	TOML_parse_value(&ctx, &value);
	++(ctx.offset);
	position = TOML_position(&ctx);
	printf("Position {\n  offset: %d\n  line: %d\n  column: %d\n}\n",
			position);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(value.float_, 34.37546);
	CU_ASSERT_EQUAL_FATAL(position.offset, 16);
	CU_ASSERT_EQUAL_FATAL(position.column, 0);
	CU_ASSERT_EQUAL_FATAL(position.line, 3);

	TOML_parse_value(&ctx, &value);
	++(ctx.offset);
	position = TOML_position(&ctx);
	printf("Position {\n  offset: %d\n  line: %d\n  column: %d\n}\n",
			position);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_STRING);
	CU_ASSERT_STRING_EQUAL_FATAL(value.string, "foo bar\\\n");
	puts(ctx.offset);
	CU_ASSERT_EQUAL_FATAL(position.offset, 31);
	printf("%d\n", position.column);
	CU_ASSERT_EQUAL_FATAL(position.column, 0);
	CU_ASSERT_EQUAL_FATAL(position.line, 4);
	String_cleanup(value.string);

	TOML_parse_value(&ctx, &value);
	position = TOML_position(&ctx);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_ARRAY);
	CU_ASSERT_EQUAL_FATAL(TOMLArray_len(value.array), 3);

	CU_ASSERT_EQUAL_FATAL(value.array[0].kind, TOML_INTEGER);
	CU_ASSERT_EQUAL_FATAL(value.array[0].integer, 34);

	CU_ASSERT_EQUAL_FATAL(value.array[1].kind, TOML_FLOAT);
	CU_ASSERT_EQUAL_FATAL(value.array[1].float_, 5.987893);

	CU_ASSERT_EQUAL_FATAL(value.array[2].kind, TOML_STRING);
	CU_ASSERT_STRING_EQUAL_FATAL(value.array[2].string,
			"I'm getting tired of this");

	String_cleanup(value.array[2].string);
	TOMLArray_cleanup(value.array);

	CU_ASSERT_EQUAL_FATAL(position.offset, 73);
	CU_ASSERT_EQUAL_FATAL(position.column, 43);
	CU_ASSERT_EQUAL_FATAL(position.line, 4);

	ctx = make_toml("true", 0);
	TOML_parse_value(&ctx, &value);
	position = TOML_position(&ctx);
	CU_ASSERT_EQUAL_FATAL(value.kind, TOML_BOOLEAN);
	CU_ASSERT_FATAL(value.boolean);
	CU_ASSERT_EQUAL_FATAL(position.offset, 4);
	CU_ASSERT_EQUAL_FATAL(position.column, 4);
	CU_ASSERT_EQUAL_FATAL(position.line, 1);
} */

int main(int argc, char **argv)
{
	int status = 0;
	CU_TestInfo tests[] = {
		{ "#parse_number",    test_parse_number    },
		{ "#parse_sl_string", test_parse_sl_string },
		{ "#parse_ml_string", test_parse_ml_string },
		{ "#parse_time",      test_parse_time      },
		{ "#parse_datetime",  test_parse_datetime  },
    /* { "#parse_array",     test_parse_array     },
		{ "#parse_value",     test_parse_value     }, */
		CU_TEST_INFO_NULL
	};
	CU_SuiteInfo suites[] = {
		{ "TOML", NULL, NULL, NULL, NULL, tests },
		CU_SUITE_INFO_NULL
	};
	CU_initialize_registry();
	if (CU_register_suites(suites) != CUE_SUCCESS && (status = 1))
		goto cleanup;
	RUN_TESTS
cleanup:
	CU_cleanup_registry();
	return status;
}
