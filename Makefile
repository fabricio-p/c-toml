SRC = lib.c table.c
HEADER = $(wildcard include/**/*.h)
OBJS = $(SRC:%.c=build/obj/%.o)
CC ?= clang
TESTS = $(wildcard test/*.c)
CFLAGS += -std=c99 -I . -I cake_libs/																	\
					-Wall -Wextra -Wformat=2 -Wshadow														\
					-Wwrite-strings -Wstrict-prototypes -Wold-style-definition	\
					-Wredundant-decls -Wnested-externs -Wmissing-include-dirs		\
					-Wno-unused-parameter -Wno-unused-command-line-argument			\
					-Wno-missing-braces -Wno-unused-function										\
					-Wno-strict-prototypes -Wno-old-style-definition
LDFLAGS += -lcunit -lxxhash

ifeq ($(MODE),debug)
	CFLAGS += -DDEBUG -O0 -ggdb
else
	CFLAGS += -O3
endif

.PHONY: all lib_test clean

%.o: build/obj/%.o

all: $(OBJS)

$(EXEC): $(OBJS)
	@mkdir -p build/bin
	$(CC) -o build/bin/$@ $^ $(LDFLAGS)

build/obj/%_test.o: test/%_test.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $^ -o $@

build/obj/%.o: %.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $^ -o $@

lib_test: build/obj/lib_test.o build/obj/lib.o build/obj/table.o
	@mkdir -p build/bin
	$(CC) -o build/bin/$@ $^ $(LDFLAGS)
	tree build

clean:
	@if [ -d build ]; then rm -rf build; fi
