CC = clang
CFLAGS = -Wall -Wextra -std=c11
TARGET = transpile
SRC = transpile.c lib/string_buffer/string_buffer.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean