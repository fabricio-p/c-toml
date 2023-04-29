# c-toml
--------

[c-toml]() is a TOML parsing library written in C with performance, ease of use and customizability.

## Content table
  1. [Usage](#Usage)
  2. [Requirements](#Requirements)
  3. [Examples](#Examples)
     1. [Parsing values](#Parsing-literals)
        1. [Literals](#Literals)
        2. [Compounds](#Ccompounds)
     2. [Parsing tables](#Parsing-tables)
     3. [Parsing files](#Parsing-tables)

## Usage
First clone the repository
```bash
git clone https://github.com/fabriciopashaj/c-toml
```
and then `#include` it into your project like this
```c
// ...
#include <c-toml/lib.h>
// ...
```
Make sure to have built the static library when you build your project.

## Requirements
The library has some minor requirements, utilities for it to function. They are
  - [c-vector]()
  - [c-string]()
  - [c-ansi-sequences]()

To have them installed you only need to run `setup.sh` and you don't need to worry about building them, since they are header-only libraries.

## Examples
Here are some examples on how to use the library. [c-toml]() provides you with functions to parse from whole TOMl files to individual key-value pairs and values.

### Parsing values
#### Literals
Integers:
```c
TOMLCtx ctx;
StringBuffer toml = StringBuffer_from_strlit("123456754");
TOML_init(&ctx, toml);
TOMLValue value;

/////////////////////////////////////////////////////
assert(TOML_parse_number(&ctx, &value) == TOML_S_OK);
/////////////////////////////////////////////////////

assert(value->kind == TOML_INTEGER);
assert(value->integer == 123456754);
```
Floats:
```c
TOMLCtx ctx;
StringBuffer toml = StringBuffer_from_strlit("745.236");
TOML_init(&ctx, toml);

/////////////////////////////////////////////////////
assert(TOML_parse_number(&ctx, &value) == TOML_S_OK);
/////////////////////////////////////////////////////

assert(value->kind == TOML_FLOAT);
assert(value->float_ == 745.236);
```
Strings:
```c
TOMLCtx ctx;
StringBuffer toml = StringBuffer_from_strlit("\"a string\"");
String str;
TOML_init(&ctx, toml);

//////////////////////////////////////////////////////
assert(TOML_parse_sl_string(&ctx, &str) == TOML_S_OK);
//////////////////////////////////////////////////////

assert(String_equal(String_from_strlit("a string"), str));
// TOMLValue_destroy(&value); // don't forget this when you are done
```
Booleans:
```c
TOMLCtx ctx;
StringBuffer toml = StringBuffer_from_strlit("true");
TOML_init(&ctx, toml);

////////////////////////////////////////////////////
assert(TOML_parse_value(&ctx, &value) == TOML_S_OK);
////////////////////////////////////////////////////

assert(value->kind == TOML_BOOLEAN);
assert(value->boolean);
```

#### Compounds
Arrays:
```c
TOMLCtx ctx;
StringBuffer toml = StringBuffer_from_strlit("[1, 2, 3]");
TOML_init(&ctx, toml);
TOMLArray arr;

//////////////////////////////////////////////////
assert(TOML_parse_array(&ctx, &arr) == TOML_S_OK);
//////////////////////////////////////////////////

assert(arr != NULL);
assert(TOMLArray_len(arr) == 3);
for (int i = 0; i < 3; ++i)
{
  assert(arr[i].kind == TOML_INTEGER);
  assert(arr[i].integer == i + 1);
}
// TOMLArray_destroy(arr); // don't forget this when you are done
```
Inline tables:
```c
TOMLCtx ctx;
StringBuffer toml = StringBuffer_from_strlit("{a = 1, b = 2, cd = 3}");
TOML_init(&ctx, toml);
TOMLTable table = TOMLTable_new();

///////////////////////////////////////////////////////////
assert(TOML_parse_inline_table(&ctx, &value) == TOML_S_OK);
///////////////////////////////////////////////////////////

assert(table != NULL);
assert(TOMLTable_count(table) == 3);
String keys[] = {
  String_from_strlit("a"),
  String_from_strlit("b"),
  String_from_strlit("cd")
};
for (int i = 0; i < 3; ++i)
{
  assert(TOMLTable_get(table, keys[i]).kind == TOML_INTEGER);
  assert(TOMLTable_get(table, keys[i]).integer == i + 1);
}
// TOMLTable_destroy(table); // don't forget this when you are done
```

### Parsing tables
Tables
```c
TOMLCtx ctx;
StringBuffer toml = StringBuffer_from_strlit("\
[foo]       \
bar = 1     \
baz = 4.5   \
");
TOML_init(&ctx, toml);
TOMLTable table = TOMLTable_new();

////////////////////////////////////////////////////
assert(TOML_parse_table(&ctx, &table) == TOML_S_OK);
////////////////////////////////////////////////////

String const foo_str = String_from_strlit("foo");
String const bar_str = String_from_strlit("bar");
String const baz_str = String_from_strlit("baz");
assert(TOMLTable_get(table, foo_str) != NULL);
assert(TOMLTable_get(table, foo_str)->kind == TOML_TABLE);
TOMLTable foo = TOMLTable_get(table, foo_str)->table;
assert(TOMLTable_get(foo, bar_str) != NULL);
assert(TOMLTable_get(foo, bar_str)->kind == TOML_INTEGER);
assert(TOMLTable_get(foo, bar_str)->integer == 1);
assert(TOMLTable_get(foo, baz_str) != NULL);
assert(TOMLTable_get(foo, baz_str)->kind == TOML_FLOAT);
assert(TOMLTable_get(foo, baz_str)->float_ == 4.5);
// TOMLTable_destroy(table); // don't forget this when you are done
```
Table arrays:
```c
TOMLCtx ctx;
StringBuffer toml = StringBuffer_from_strlit("\
[[foo]]     \
bar = 4.5   \
");
TOML_init(&ctx, toml);
TOMLTable table = TOMLTable_new();

////////////////////////////////////////////////////
assert(TOML_parse_table(&ctx, &table) == TOML_S_OK);
////////////////////////////////////////////////////

String const foo_str = String_from_strlit("foo");
String const bar_str = String_from_strlit("bar");
String const baz_str = String_from_strlit("baz");
assert(TOMLTable_get(table, foo_str) != NULL);
assert(TOMLTable_get(table, foo_str)->kind == TOML_TABLE_ARRAY);
assert(TOMLTable_get(table, foo_str)->array[0].kind == TOML_TABLE);
TOMLArray foo = TOMLTable_get(table, foo_str)->array[0].table;
assert(TOMLTable_get(foo, bar_str) != NULL);
assert(TOMLTable_get(foo, bar_str)->kind == TOML_INTEGER);
assert(TOMLTable_get(foo, bar_str)->integer == 1);
assert(TOMLTable_get(foo, baz_str) != NULL);
assert(TOMLTable_get(foo, baz_str)->kind == TOML_FLOAT);
assert(TOMLTable_get(foo, baz_str)->float_ == 4.5);
// TOMLTable_destroy(table); // don't forget this when you are done
```

### Parsing files
```c
FILE *toml_file = fopen("config.toml", "r");
fseek(toml_file, 0, SEEK_END);
int size = ftell(toml_file);
fseek(toml_file, 0, SEEK_SET);
StringBuffer toml = StringBuffer_with_length(size);
fread(toml, 1, size, toml_file);
fclose(toml_file);
TOMLCtx ctx;
TOML_init(&ctx, toml);
TOMLTable config = TOMLTable_new();

///////////////////////////////////////////////
assert(TOML_parse(&ctx, &config) == TOML_S_OK);
///////////////////////////////////////////////

// ...

// TOMLTable_destroy(config); // don't forget this when you are done
```

For more examples check the [tests]().

  [c-toml]: https://github.com/fabriciopashaj/c-toml
  [c-vector]: https://github.com/fabriciopashaj/c-vector
  [c-string]: https://github.com/fabriciopashaj/c-string
  [s-ansi-sequences]: https://github.com/fabriciopashaj/c-ansi-sequences
  [tests]: https://github.com/fabriciopashaj/c-toml/blob/main/test/lib_test.c
