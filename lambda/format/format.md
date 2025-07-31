# Format Writer Guide

This document provides a guide for implementing new formatters in the Lambda project, incorporating lessons learned from implementing formatters for JSON, XML, HTML, YAML, TOML, etc.

## Overview

Formatters convert Lambda data structures (Items) into specific output formats. They follow a consistent pattern to ensure reliability and maintainability.

### Coding Guidelines
- Start comments in lowercase.
- **Add debug logging** for development and troubleshooting.
- **Test with comprehensive nested data structures** and use timeouts to catch hangs early
- **Back up the file** before major refactoring or rewrite. Remove the backup at the end of successful refactoring or rewrite.

## Architecture

### Core Components

**Primary Reference**: See `data_struct.md` for comprehensive documentation on Lambda data structures, types, and memory layout.
- **Type Detection**: Always use `get_type_id()` function from `LambdaItem` struct
- **Data Extraction**: Extract data pointer based on TypeId
- **Reference Implementation**: Always consult `print_item_with_depth()` for correct patterns

1. **Main Formatter Function**: `String* format_<name>(VariableMemPool* pool, Item root_item)`
2. **Registration**: Add to `format_data()` dispatcher in `format.c`
3. **Data Type Support**: Handle all Lambda data types (see `transpiler.h` for complete enum)

### Memory Management
- Use `strbuf_new_pooled(pool)` for all allocations
- Free temporary StrBuf objects with `strbuf_free()`
- Let `format_data()` handle final string registration

### File References
- **Basic Pattern**: `format-json.c` - Basic reference implementation
- **Direct Traversal**: `format-yaml.c` - Updated direct traversal implementation
- **Latest Best Practices**: `format-toml.c` - Most recent implementation with proper Lambda type handling
- **Registration**: `format.c` - Add new formatters to `format_data()`
- **Lambda Type Reference**: `print.c` - See `print_item_with_depth()` for authoritative type handling
