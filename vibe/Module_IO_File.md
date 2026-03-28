# Module IO: File (`lib/file.c` + `lib/file_utils.c`)

## Overview

Extend the existing file I/O modules (`lib/file.c` and `lib/file_utils.c`) into a comprehensive, cross-platform C file operations library serving all Lambda runtime backends: **Lambda Script**, **JavaScript**, **Python**, and **Bash** transpilation.

### Current State

**All functions are implemented.** The module is fully operational with 37+ public functions across `lib/file.c` and 8 functions in `lib/file_utils.c`. The entire codebase (`lambda/`, `radiant/`) has been migrated to use these modules instead of direct POSIX headers.

**`lib/file.c`** provides:
- Read/write: `read_text_file`, `read_binary_file`, `write_text_file`, `write_binary_file`, `append_text_file`, `append_binary_file`, `write_text_file_atomic`
- File ops: `file_copy`, `file_move`, `file_delete`, `file_delete_recursive`, `file_touch`, `file_symlink`, `file_chmod`, `file_rename`
- Queries: `file_exists`, `file_is_file`, `file_is_dir`, `file_is_symlink`, `file_is_readable`, `file_is_writable`, `file_is_executable`, `file_stat`, `file_size`, `file_realpath`, `file_getcwd`
- Streaming: `file_read_lines`
- Temp files: `file_temp_path`, `file_temp_create`, `dir_temp_create`
- Path utils: `file_path_join`, `file_path_dirname`, `file_path_basename`, `file_path_ext`
- Extras: `file_ensure_dir`, `file_cache_path`, `create_dir`

**`lib/file_utils.c`** provides:
- `create_dir_recursive` — `mkdir -p` equivalent
- `dir_list` — list directory children as `ArrayList` of `DirEntry*`
- `dir_walk` — recursive depth-first traversal with callback
- `dir_delete` — recursive rm (`rm -rf`)
- `dir_copy` — recursive directory copy
- `file_glob` — shell glob pattern expansion
- `file_find` — name pattern search under a directory
- `dir_entry_free` — cleanup helper

### Capability Status

| Capability | Status |
|------------|--------|
| Read file (text / binary) | ✅ Implemented |
| Write file (text) | ✅ Implemented |
| Write binary / append mode | ✅ Implemented |
| Atomic write (temp + rename) | ✅ Implemented |
| Create directory | ✅ Implemented (consolidated) |
| Copy / move / delete files | ✅ Implemented |
| Symlink / chmod / touch | ✅ Implemented |
| File metadata (stat) | ✅ Implemented (`FileStat` struct) |
| File existence / type check | ✅ Implemented |
| Directory listing | ✅ Implemented (`dir_list`) |
| Recursive directory walk | ✅ Implemented (`dir_walk`) |
| Glob / find | ✅ Implemented |
| Temporary file creation | ✅ Implemented (under `./temp/`) |
| Streaming read (line-by-line) | ✅ Implemented (`file_read_lines`) |
| Path utilities | ✅ Implemented |
| File locking | ❌ Not yet needed |
| Watch / notify | ❌ Out of scope (future) |

### Codebase Migration Status

All files under `lambda/` and `radiant/` have been migrated to use `lib/file.h` and `lib/shell.h` instead of direct POSIX file headers (`<unistd.h>`, `<sys/stat.h>`, `<dirent.h>`, `<direct.h>`).

**Migrated directories:**
- `lambda/input/` — all input parsers
- `lambda/format/` — all output formatters
- `lambda/` core — runner, target, main, lambda-proc, sysinfo, path, parse
- `lambda/bash/` — bash runtime
- `lambda/py/` — Python stdlib
- `lambda/js/` — JS filesystem
- `lambda/network/` — enhanced file cache
- `radiant/` — window, font_face, cmd_layout, webdriver

Build: 0 errors, 0 warnings. Tests: 716/716 baseline pass.

## Design Principles

1. **Extend, don't replace** — existing `read_text_file`, `read_binary_file`, `write_text_file` signatures are kept; new functions are added alongside them
2. **Cross-platform** — POSIX and Win32 behind `#ifdef`, consistent behavior across macOS, Linux, Windows
3. **Zero std:: dependency** — pure C; uses `lib/` types (`StrBuf`, `ArrayList`) where collections are needed
4. **Structured results** — operations that can fail return `int` (0 = success, -1 = error) or `bool`; errors logged via `log_error()`
5. **Path-safe** — all functions accept `const char* path`; no assumptions about trailing slashes or encoding
6. **Consolidate duplication** — merge `create_dir` (file.c) and `create_dir_recursive` (file_utils.c) into a single implementation

## Data Structures

```c
// additions to lib/file.h

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// --- File metadata (mirrors stat but portable) ---
typedef struct {
    int64_t size;           // file size in bytes (-1 on error)
    time_t modified;        // last modification time
    time_t created;         // creation time (where available)
    uint16_t mode;          // Unix permission bits (0 on Windows)
    bool is_file;
    bool is_dir;
    bool is_symlink;
    bool exists;
} FileStat;

// --- Directory entry ---
typedef struct {
    const char* name;       // entry name (not full path)
    bool is_dir;            // true if directory
    bool is_symlink;        // true if symbolic link
} DirEntry;

// --- Callback for directory walk ---
// Return false to skip subtree (pre-order) or abort (flat listing).
typedef bool (*FileWalkCallback)(const char* path, const FileStat* stat, void* user_data);

// --- Callback for streaming line reads ---
// Return false to stop reading.
typedef bool (*FileLineCallback)(const char* line, size_t len, int line_number, void* user_data);

// --- Options for file copy ---
typedef struct {
    bool overwrite;         // overwrite destination if exists (default: false)
    bool preserve_metadata; // preserve permissions + timestamps (default: false)
} FileCopyOptions;
```

## API Surface

### Existing (Unchanged)

| Function | Signature | Notes |
|----------|-----------|-------|
| `read_text_file` | `char* read_text_file(const char* filename)` | Kept as-is |
| `read_binary_file` | `char* read_binary_file(const char* filename, size_t* out_size)` | Kept as-is |
| `write_text_file` | `void write_text_file(const char* filename, const char* content)` | Kept as-is |
| `create_dir` | `bool create_dir(const char* dir_path)` | Kept; implementation consolidated |

### New: Write Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `write_binary_file` | `int write_binary_file(const char* filename, const char* data, size_t len)` | Write raw bytes |
| `append_text_file` | `int append_text_file(const char* filename, const char* content)` | Append string to file |
| `append_binary_file` | `int append_binary_file(const char* filename, const char* data, size_t len)` | Append raw bytes |
| `write_text_file_atomic` | `int write_text_file_atomic(const char* filename, const char* content)` | Write to temp file in same dir, then rename (crash-safe) |

### New: File Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_copy` | `int file_copy(const char* src, const char* dst, const FileCopyOptions* opts)` | Copy file; creates parent dirs |
| `file_move` | `int file_move(const char* src, const char* dst)` | Rename/move; falls back to copy+delete across filesystems |
| `file_delete` | `int file_delete(const char* path)` | Delete file (not directory) |
| `file_touch` | `int file_touch(const char* path)` | Create empty file or update mtime |
| `file_symlink` | `int file_symlink(const char* target, const char* link_path)` | Create symbolic link |
| `file_chmod` | `int file_chmod(const char* path, uint16_t mode)` | Set permissions (no-op on Windows) |
| `file_rename` | `int file_rename(const char* old_path, const char* new_path)` | Rename within same directory |

### New: Queries & Metadata

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_exists` | `bool file_exists(const char* path)` | True if path exists (file or dir) |
| `file_is_file` | `bool file_is_file(const char* path)` | True if regular file |
| `file_is_dir` | `bool file_is_dir(const char* path)` | True if directory |
| `file_is_symlink` | `bool file_is_symlink(const char* path)` | True if symbolic link |
| `file_stat` | `FileStat file_stat(const char* path)` | Populate full metadata struct |
| `file_size` | `int64_t file_size(const char* path)` | File size in bytes (-1 on error) |
| `file_realpath` | `char* file_realpath(const char* path)` | Resolve to absolute canonical path (caller must free) |

### New: Directory Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `dir_list` | `ArrayList* dir_list(const char* dir_path)` | List immediate children as `DirEntry*` (caller must free list + entries) |
| `dir_walk` | `int dir_walk(const char* dir_path, FileWalkCallback cb, void* user_data)` | Recursive depth-first walk with callback |
| `dir_delete` | `int dir_delete(const char* dir_path)` | Recursive delete (`rm -rf`), use with caution |
| `dir_copy` | `int dir_copy(const char* src, const char* dst, const FileCopyOptions* opts)` | Recursive directory copy |

### New: Glob & Search

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_glob` | `ArrayList* file_glob(const char* pattern)` | Expand glob pattern to list of paths (caller must free) |
| `file_find` | `ArrayList* file_find(const char* dir, const char* name_pattern, bool recursive)` | Find files by name pattern under a directory |

### New: Streaming

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_read_lines` | `int file_read_lines(const char* filename, FileLineCallback cb, void* user_data)` | Stream file line-by-line without loading entirely into memory |

### New: Temporary Files

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_temp_path` | `char* file_temp_path(const char* prefix, const char* suffix)` | Generate unique temp file path under `./temp/` (caller must free) |
| `file_temp_create` | `char* file_temp_create(const char* prefix, const char* suffix)` | Create temp file, return path (caller must free) |
| `dir_temp_create` | `char* dir_temp_create(const char* prefix)` | Create temp directory, return path (caller must free) |

### New: Path Utilities

File-level path helpers (complements `lambda/path.c` which handles Lambda's `Path` type):

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_path_join` | `char* file_path_join(const char* base, const char* relative)` | Join path segments with separator (caller must free) |
| `file_path_dirname` | `char* file_path_dirname(const char* path)` | Parent directory of path (caller must free) |
| `file_path_basename` | `const char* file_path_basename(const char* path)` | Filename component (pointer into input — no alloc) |
| `file_path_ext` | `const char* file_path_ext(const char* path)` | File extension including dot (pointer into input) |

## Module Layout

```
lib/
├── file.h              # extended header — all declarations
├── file.c              # core read/write, create_dir (existing + new write ops)
├── file_utils.c        # directory ops: dir_list, dir_walk, dir_delete, dir_copy, glob, find
├── file_utils.h        # directory and utility declarations
```

- **`file.c`** grows to include: `write_binary_file`, `append_*`, atomic write, `file_copy/move/delete/touch/symlink/chmod/rename`, metadata queries, streaming reads, temp files, path utilities
- **`file_utils.c`** grows to include: `dir_list`, `dir_walk`, `dir_delete`, `dir_copy`, `file_glob`, `file_find`
- Existing `create_dir_recursive` in `file_utils.c` is kept; `create_dir` in `file.c` is refactored to call it internally (eliminate duplication)

## Integration with Language Runtimes

### Lambda Script

Maps to `io.*` procedures already designed in `Lambda_Shell.md`:

```lambda
// file reads — functional context
let data = read("config.json")       // → read_text_file
let bytes = read_binary("image.png") // → read_binary_file, returns Binary

// file writes — procedural context
io.write("output.json", data)        // → write_text_file (format auto-detected)
io.write("data.bin", bytes)          // → write_binary_file
data |> output("result.yaml")        // → pipe operator, format from extension

// file operations
io.copy("a.txt", "b.txt")           // → file_copy
io.move("old.txt", "new.txt")       // → file_move
io.delete("temp.txt")               // → file_delete
io.mkdir("path/to/dir")             // → create_dir
io.touch("marker.flag")             // → file_touch
io.symlink("target", "link")        // → file_symlink
io.chmod("script.sh", 0o755)        // → file_chmod

// queries
exists("path/to/file")              // → file_exists (pure function)
let info = io.stat("file.txt")      // → file_stat, returns map

// directory listing
let entries = io.list("src/")       // → dir_list, returns array of maps
io.walk("project/", fn(path, stat) => ...) // → dir_walk

// glob
let files = io.glob("src/**/*.ls")  // → file_glob

// streaming
io.read_lines("big.log", fn(line, n) => ...) // → file_read_lines

// temp files (always under ./temp/ per project rules)
let tmp = io.temp("prefix", ".json") // → file_temp_create
```

### Bash (Transpiler)

Bash builtins map directly to file module functions:

| Bash | C function |
|------|-----------|
| `cat file` | `read_text_file(file)` |
| `echo "x" > file` | `write_text_file(file, "x")` |
| `echo "x" >> file` | `append_text_file(file, "x")` |
| `cp src dst` | `file_copy(src, dst, NULL)` |
| `mv src dst` | `file_move(src, dst)` |
| `rm file` | `file_delete(file)` |
| `rm -rf dir` | `dir_delete(dir)` |
| `mkdir -p path` | `create_dir(path)` |
| `touch file` | `file_touch(file)` |
| `ln -s target link` | `file_symlink(target, link)` |
| `chmod 755 file` | `file_chmod(file, 0755)` |
| `test -f file` | `file_is_file(file)` |
| `test -d dir` | `file_is_dir(dir)` |
| `test -e path` | `file_exists(path)` |
| `ls dir` | `dir_list(dir)` |
| `find dir -name "*.txt"` | `file_find(dir, "*.txt", true)` |
| `basename path` | `file_path_basename(path)` |
| `dirname path` | `file_path_dirname(path)` |
| `realpath path` | `file_realpath(path)` |
| `mktemp` | `file_temp_create(NULL, NULL)` |
| `while read line; do ...; done < file` | `file_read_lines(file, cb, data)` |

### JavaScript

Node.js `fs` module operations → file module:

```javascript
// fs.readFileSync('file.txt', 'utf8')  → read_text_file
// fs.writeFileSync('file.txt', data)   → write_text_file
// fs.appendFileSync('file.txt', data)  → append_text_file
// fs.copyFileSync(src, dst)            → file_copy
// fs.renameSync(old, new)              → file_move
// fs.unlinkSync(path)                  → file_delete
// fs.mkdirSync(path, {recursive:true}) → create_dir
// fs.existsSync(path)                  → file_exists
// fs.statSync(path)                    → file_stat
// fs.readdirSync(dir)                  → dir_list
// fs.symlinkSync(target, link)         → file_symlink
// fs.chmodSync(path, mode)             → file_chmod
// fs.realpathSync(path)                → file_realpath
// require('glob').globSync(pattern)    → file_glob
```

### Python

Python `os`, `os.path`, `shutil` → file module:

```python
# open(f).read()           → read_text_file
# open(f, 'w').write(s)    → write_text_file
# open(f, 'a').write(s)    → append_text_file
# shutil.copy2(src, dst)   → file_copy (preserve_metadata=true)
# shutil.move(src, dst)    → file_move
# os.remove(path)          → file_delete
# os.makedirs(path)        → create_dir
# os.path.exists(path)     → file_exists
# os.path.isfile(path)     → file_is_file
# os.path.isdir(path)      → file_is_dir
# os.stat(path)            → file_stat
# os.listdir(dir)          → dir_list
# os.walk(dir)             → dir_walk
# shutil.rmtree(dir)       → dir_delete
# os.symlink(target, link) → file_symlink
# os.chmod(path, mode)     → file_chmod
# os.path.realpath(path)   → file_realpath
# os.path.join(a, b)       → file_path_join
# os.path.dirname(path)    → file_path_dirname
# os.path.basename(path)   → file_path_basename
# os.path.splitext(path)   → file_path_ext
# glob.glob(pattern)       → file_glob
# tempfile.mkstemp()       → file_temp_create
```

## Security Considerations

1. **Path traversal prevention** — `file_realpath` resolves symlinks; callers should validate resolved paths stay within expected roots when processing untrusted input
2. **Temp files under `./temp/`** — per project rules, never write to `/tmp`; `file_temp_create` defaults to `./temp/`
3. **Atomic writes** — `write_text_file_atomic` prevents partial writes on crash by writing to a temp file, then renaming
4. **No shell expansion in paths** — all functions take literal paths; no glob or variable expansion unless explicitly calling `file_glob`
5. **Recursive delete safety** — `dir_delete` logs each deletion; callers should confirm the path before calling on user-supplied input

## Implementation Phases

### Phase 1: Write Extensions & Consolidation — ✅ Complete
- `write_binary_file`, `append_text_file`, `append_binary_file`
- `write_text_file_atomic` (temp + rename pattern)
- Consolidate `create_dir` / `create_dir_recursive` duplication
- Unit tests for all new write functions

### Phase 2: File Operations — ✅ Complete
- `file_copy`, `file_move`, `file_delete`, `file_touch`
- `file_symlink`, `file_chmod`, `file_rename`
- Cross-filesystem move (copy + delete fallback)
- Unit tests: copy overwrite behavior, move across mounts, symlink resolution

### Phase 3: Metadata & Queries — ✅ Complete
- `FileStat` struct, `file_stat`, `file_exists`, `file_is_file`, `file_is_dir`, `file_is_symlink`
- `file_size`, `file_realpath`
- Bonus: `file_is_readable`, `file_is_writable`, `file_is_executable`, `file_getcwd`
- Unit tests: stat fields, symlink detection, non-existent paths

### Phase 4: Directory Operations — ✅ Complete
- `dir_list` returning `ArrayList` of `DirEntry*`
- `dir_walk` with `FileWalkCallback` (depth-first, skip subtrees)
- `dir_delete` (recursive rm), `file_delete_recursive`
- `dir_copy` (recursive copy)
- Unit tests: listing order, walk depth, recursive delete safety

### Phase 5: Glob, Find & Streaming — ✅ Complete
- `file_glob` — POSIX `glob()` / Win32 `FindFirstFile` wrapper
- `file_find` — name pattern search (uses `dir_walk` internally)
- `file_read_lines` — buffered line streaming with callback
- Unit tests: glob patterns, find recursion, large file streaming

### Phase 6: Path Utilities & Temp Files — ✅ Complete
- `file_path_join`, `file_path_dirname`, `file_path_basename`, `file_path_ext`
- `file_temp_path`, `file_temp_create`, `dir_temp_create`
- Bonus: `file_ensure_dir`, `file_cache_path`
- Unit tests: join edge cases (trailing slashes, empty segments), temp uniqueness

### Phase 7: Codebase Migration — ✅ Complete
- All `lambda/` files migrated to use `lib/file.h` / `lib/shell.h`
- All `radiant/` files migrated to use `lib/file.h` / `lib/shell.h`
- `lib/mime-detect.h/c` extracted from input parsers
- Build: 0 errors, 0 warnings. Tests: 716/716 baseline pass.

### Phase 8: Runtime Integration — Not started
- Wire all functions to `io.*` system procedures in `sys_func_registry.c`
- Update Bash transpiler to use file module instead of inline implementations
- Add JS/Python transpiler mappings
- Integration tests with Lambda scripts
- Cross-platform CI (macOS, Linux, Windows)

## Relationship to Module IO: Shell

The File module and the Shell module (`lib/shell.c`) together provide complete I/O coverage:

```
┌────────────────────────────────────────────────────────────────┐
│                  Lambda Runtime I/O Layer                       │
├───────────────────────────┬────────────────────────────────────┤
│     Module IO: File       │       Module IO: Shell             │
│   lib/file.c              │       lib/shell.c                  │
│   lib/file_utils.c        │                                    │
├───────────────────────────┼────────────────────────────────────┤
│ • Read/write files        │ • Execute commands                 │
│ • Copy/move/delete        │ • Capture stdout/stderr            │
│ • Directory listing/walk  │ • Background processes             │
│ • Glob/find               │ • Environment variables            │
│ • File metadata/stat      │ • Program lookup (which)           │
│ • Symlinks/permissions    │ • Argument escaping                │
│ • Atomic writes           │ • Home/temp dir location           │
│ • Streaming reads         │ • Hostname                         │
│ • Temp file management    │ • Timeout/kill                     │
│ • Path utilities          │                                    │
├───────────────────────────┴────────────────────────────────────┤
│                   lambda/path.c                                │
│            Lambda Path type (segments, schemes, metadata)      │
├────────────────────────────────────────────────────────────────┤
│                   lib/url.c                                    │
│            URL parsing (WHATWG standard)                        │
└────────────────────────────────────────────────────────────────┘
```

Together, the two modules provide the file system and process primitives that every language runtime (Lambda, JS, Python, Bash) needs — without duplicating logic across transpiler backends.
