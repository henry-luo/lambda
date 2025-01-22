CC = clang

PLATFORM := $(shell uname)
COMPILER := $(shell ($(CC) -v 2>&1) | tr A-Z a-z )

ifdef DEBUG
	OPT = -O0 -DDEBUG=1 --debug -g -ggdb
else
	ifneq (,$(findstring clang,$(COMPILER)))
		OPT = -O3
	else
		OPT = -O4
		# TGTFLAGS = -fwhole-program
	endif
endif

CFLAGS = -Wall -Wextra -pedantic -std=c99 $(OPT)
OBJFLAGS = -fPIC
TARGET = transpile

all: $(TARGET)

$(TARGET): transpile.o string_buffer.o
	$(CC) -o $(TARGET) transpile.o string_buffer.o  -L. -lz

transpile.o: transpile.c lib/string_buffer/string_buffer.h
	$(CC) $(CFLAGS) $(OBJFLAGS) -c transpile.c -o transpile.o

string_buffer.o: lib/string_buffer/string_buffer.c lib/string_buffer/string_buffer.h lib/string_buffer/stream_buffer.h
	$(CC) $(CFLAGS) $(OBJFLAGS) -c lib/string_buffer/string_buffer.c -o string_buffer.o

clean:
	rm -rf *.o
	rm -f $(TARGET)

.PHONY: all clean