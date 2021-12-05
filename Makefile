SRC = lib.c table.c
HEADER = $(wildcard include/**/*.h)
OBJS = $(SRC:%.c=build/%.o)
CC ?= clang
TESTS = $(wildcard test/*.c)
CFLAGS += -std=c99 -I . -I cake_libs/ -Ofast                         \
		  -Wall -Wextra -Wformat=2 -Wshadow                          \
          -Wwrite-strings -Wstrict-prototypes -Wold-style-definition \
          -Wredundant-decls -Wnested-externs -Wmissing-include-dirs  \
		  -Wno-unused-parameter -Wno-unused-command-line-argument    \
		  -Wno-missing-braces -Wno-unused-function

.PHONY: all tests clean

%.o: build/%.o

all: $(OBJS)

$(EXEC): $(OBJS)
	@mkdir -p bin
	$(CC) -o bin/$@ $^

build/%_test.o: test/%_test.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $^ -o $@

build/%.o: %.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $^ -o $@

lib_test: build/lib_test.o build/lib.o build/table.o
	@mkdir -p bin
	$(CC) -lcunit -lxxhash -o bin/$@ $^

tests: $(TESTS:test/%.c=%)

clean:
	@if [ -d build ]; then rm -rf build; fi
	@if [ -d bin ]; then rm -rf bin; fi
