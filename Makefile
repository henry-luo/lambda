CC = zig cc

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
SRC_DIR = src
OBJ_DIR = obj
TARGET = transpile

# Source files and object files
SRC = $(shell find $(SRC_DIR) lib -name '*.c')
EXCLUDE = lib/string_buffer/strbuf_test.c
SRC := $(filter-out $(EXCLUDE), $(SRC))  # Remove excluded files
OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))

all: run

# $(TARGET): transpile.o string_buffer.o
# 	$(CC) -o $(TARGET) transpile.o string_buffer.o  -L. -lz

# transpile.o: transpile.c
# 	$(CC) $(CFLAGS) $(OBJFLAGS) -c transpile.c -o transpile.o

# string_buffer.o: lib/string_buffer/string_buffer.c
# 	$(CC) $(CFLAGS) $(OBJFLAGS) -c lib/string_buffer/string_buffer.c -o string_buffer.o

# Linking target
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) -L. -lz

# Compile each .c file to .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Create object directory if it doesn't exist
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

run: transpile
	./transpile

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean