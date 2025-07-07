# Makefile for building Windows dependencies
TARGET = x86_64-w64-mingw32
CC = $(TARGET)-gcc
CXX = $(TARGET)-g++
AR = $(TARGET)-ar
CFLAGS = -O2 -static -I../include
LDFLAGS = -static

# Example for building a simple static library
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libexample.a: example.o
	$(AR) rcs $@ $^

clean:
	rm -f *.o *.a
