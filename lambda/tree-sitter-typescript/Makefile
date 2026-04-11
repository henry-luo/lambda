ifeq ($(OS),Windows_NT)
$(error Windows is not supported)
endif

LANGUAGE_NAME := tree-sitter-typescript

# repository
SRC_DIR := src

TS ?= tree-sitter

# source/object files
PARSER := $(SRC_DIR)/parser.c
EXTRAS := $(filter-out $(PARSER),$(wildcard $(SRC_DIR)/*.c))
OBJS := $(patsubst %.c,%.o,$(PARSER) $(EXTRAS))

# flags
ARFLAGS ?= rcs
override CFLAGS += -I$(SRC_DIR) -std=c11 -fPIC

all: lib$(LANGUAGE_NAME).a

lib$(LANGUAGE_NAME).a: $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

# scanner.c includes ../scanner_v2.h — track header dependency
$(SRC_DIR)/scanner.o: scanner_v2.h

clean:
	$(RM) $(OBJS) lib$(LANGUAGE_NAME).a

.PHONY: all clean
