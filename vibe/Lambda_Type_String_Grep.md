# Lambda File-Based Find & Replace (grep/sed) Proposal

**Date:** February 28, 2026
**Parent:** `vibe/Lambda_Type_String_Pattern2.md` (String Pattern Enhancement v2)
**Status:** Not Started
**Prerequisites:** Phases 1–5 of String Pattern Enhancement (all completed)

---

## 1. Overview

This proposal extends Lambda's `find()` and `replace()` functions with a **file dimension**: when the first argument is a file path, the operation targets the file's text content instead of an in-memory string. This gives Lambda built-in **grep** and **sed** equivalents.

The in-memory string variants of `find()`, `replace()`, and `split()` are already implemented (see parent proposal). This document covers only the file-based extensions.

### 1.1 Current State

| # | Feature | Syntax | Status |
|---|---------|--------|--------|
| 1 | String find | `find(str, pattern)` | **Working** (parent proposal Phase 2) |
| 2 | String replace | `replace(str, pattern, repl)` | **Working** (parent proposal Phase 3) |
| 3 | String split | `split(str, pattern)` | **Working** (parent proposal Phase 4) |
| 4 | File find (grep) | `find(path, pattern)` | **Not Implemented** |
| 5 | File replace (sed) | `pn replace(path, pattern, repl)` | **Not Implemented** |

---

## 2. Feature Specifications

### 2.1 File-Based `find()` — grep

Search for pattern matches within a file or directory. The return shape is **uniform** with string `find()` — every match is `{value, index}` — except when multiple files are involved, in which case a `file` field is added.

```lambda
string digits = \d+
string todo_pat = "TODO" ":" \s* \w+

// --- Single file find (like string find, same return shape) ---
find(/src.'main.ls', digits)
// [{value: "42", index: 5}, {value: "100", index: 28}, ...]

find(.config.json, digits)                   // relative path
find(/readme.md, "Lambda")                   // plain string search in file
// [{value: "Lambda", index: 0}, {value: "Lambda", index: 312}]

// --- Directory find (recursive grep) ---
find(/src, todo_pat)
// [{file: /src.'main.ls', value: "TODO: refactor", index: 45},
//  {file: /src.'util.ls', value: "TODO: add tests", index: 12}, ...]

// --- Wildcard path ---
find(/src.*, digits)                         // all files under /src
find(/src.**, digits)                        // recursive
// [{file: /src.'main.ls', value: "42", index: 5}, ...]

// --- With options ---
find(/src, digits, {limit: 10})              // stop after 10 total matches
find(/src, todo_pat, {ignore_case: true})
```

**Dispatch logic:** `fn_find()` checks the type of the first argument:
- `LMD_TYPE_STRING` or `LMD_TYPE_SYMBOL` → **in-memory string find** (existing, parent proposal)
- `LMD_TYPE_PATH` pointing to a **file** → **single-file find** (returns `{value, index}` — same shape as string find)
- `LMD_TYPE_PATH` pointing to a **directory** or **wildcard** → **multi-file find** (recursively searches all files, returns `{file, value, index}`)

**Return type:**

| Scenario | Fields | Description |
|----------|--------|-------------|
| `find(string, pattern)` | `{value, index}` | In-memory string match (parent proposal) |
| `find(file_path, pattern)` | `{value, index}` | Single file — same shape as string find |
| `find(dir_or_wildcard, pattern)` | `{file, value, index}` | Multi-file — `file` is the matching path |

**Options map fields:**

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `limit` | int | 0 (unlimited) | Maximum number of matches to return |
| `ignore_case` | bool | false | Case-insensitive matching |

**Implementation:**

```cpp
Item fn_find(Item source, Item pattern_item, Item options) {
    TypeId source_type = get_type_id(source);

    if (source_type == LMD_TYPE_PATH) {
        Path* path = source.path;

        // Resolve: is it a file, directory, or wildcard?
        StrBuf* path_buf = strbuf_new();
        path_to_os_path(path, path_buf);
        struct stat st;
        bool is_dir = (stat(path_buf->str, &st) == 0 && S_ISDIR(st.st_mode));
        bool is_wildcard = path_ends_with_wildcard(path);

        if (is_dir || is_wildcard) {
            // Multi-file find: recursively collect files, search each
            // Returns [{file: path, value: str, index: int}, ...]
            return find_in_directory(path, path_buf->str, pattern_item, options, is_wildcard);
        } else {
            // Single-file find: read text and search (same shape as string find)
            char* text = read_text_file(path_buf->str);
            strbuf_free(path_buf);
            if (!text) return ItemNull;
            List* results = find_in_string_text(text, strlen(text), pattern_item, options);
            free(text);
            return {.list = results};  // [{value, index}, ...]
        }
    }

    // Otherwise: in-memory string find (existing behavior)
    return find_in_string(source, pattern_item);
}
```

**Helper `find_in_directory()`:** Uses `path_resolve_for_iteration()` to list directory children (or expand wildcards). For each child that is a regular file, reads its content and searches. Each match map gets an additional `file` field set to the child's `Path*`. Recurses into subdirectories.

### 2.2 File-Based `replace()` — sed (Procedural Only)

Replace pattern matches in file(s) on disk. Since this is inherently a side-effecting operation (modifying files), it is registered as a **separate procedural function** (`pn`), distinct from the pure `fn replace(str, pattern, repl)`.

```lambda
string ws_trail = \s+ "$"
string old_api = "getWidget" "(" \w+ ")"

// --- File replace is always procedural (pn context required) ---
pn main() {
    // Single file
    replace(/src.'main.ls', ws_trail, "")              // removes trailing whitespace
    replace(/config.json, "localhost", "0.0.0.0")      // plain string replace in file

    // Directory (recursive, like sed -i on a tree)
    replace(/src, old_api, "fetchWidget($1)")           // replaces in all files under /src
    replace(/src.**, ws_trail, "")                       // wildcard: all files recursively

    // With options
    replace(/src.'main.ls', old_api, "fetchWidget($1)", {limit: 1})  // replace first match only
    replace(/src, "v1.0", "v2.0", {ignore_case: true})              // case-insensitive
}

// This is a compile error — file replace is not allowed in fn context:
// fn transform() => replace(/src.main, "a", "b")  // ERROR: procedural function in fn context
```

**Two separate system functions:** The key design is that `fn replace(str, ...)` and `pn replace(path, ...)` are **separate registrations** dispatched by first-argument type:

| Function | Context | 1st arg | Behavior |
|----------|---------|---------|----------|
| `fn replace(str, pattern, repl)` | pure `fn` | string | In-memory replacement, returns new string |
| `pn replace(path, pattern, repl)` | procedural `pn` | path | Reads file(s), replaces, writes back. Side effect. |

The compiler resolves which overload to call based on the first argument's type. If the first arg is `LMD_TYPE_PATH`, the procedural variant is selected, and the compiler enforces `pn` context.

**Directory / wildcard support:** Same as `find()` — when the path points to a directory or wildcard, all matching files are processed recursively.

**Options map fields:**

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `limit` | int | 0 (unlimited) | Maximum replacements per file |
| `ignore_case` | bool | false | Case-insensitive matching |

**Return type:** `null` (void). The operation's purpose is its side effect (modifying files). Errors (file not found, permission denied) are raised.

**Implementation:**

```cpp
// Registered as SYSPROC_REPLACE_FILE (is_proc=true)
Item fn_replace_file(Item path_item, Item pattern_item, Item replacement, Item options) {
    Path* path = path_item.path;
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path, path_buf);
    struct stat st;
    bool is_dir = (stat(path_buf->str, &st) == 0 && S_ISDIR(st.st_mode));
    bool is_wildcard = path_ends_with_wildcard(path);

    if (is_dir || is_wildcard) {
        // Recursively process all files
        replace_in_directory(path, path_buf->str, pattern_item, replacement, options, is_wildcard);
    } else {
        // Single file: read, replace, write back
        char* text = read_text_file(path_buf->str);
        if (!text) { strbuf_free(path_buf); return ItemError; }
        String* result = replace_in_text(text, pattern_item, replacement, options);
        write_text_file(path_buf->str, result->chars);
        free(text);
    }

    strbuf_free(path_buf);
    return ItemNull;  // void
}
```

### 2.3 Function Signatures

`find()` is a single pure function (`fn`) that dispatches on the first argument's type. `replace()` is split into two registrations: a pure `fn` for strings and a procedural `pn` for files.

```
// === find() — pure fn, dispatches on 1st arg type ===

// String find (existing, parent proposal)
fn find(str, pattern)            → [{value, index}, ...]
fn find(str, string)             → [{value, index}, ...]

// Single-file find
fn find(file_path, pattern)      → [{value, index}, ...]       // same shape as string find
fn find(file_path, string)       → [{value, index}, ...]

// Directory/wildcard find — recursive grep
fn find(dir_path, pattern)       → [{file, value, index}, ...]  // adds file field
fn find(wildcard, pattern)       → [{file, value, index}, ...]

// With options
fn find(path, pattern, options)  → [{...}, ...]

// === replace() — two separate registrations ===

// Pure string replace (existing, parent proposal)
fn replace(str, pattern, repl)   → str
fn replace(str, string, repl)    → str

// Procedural file replace — sed
pn replace(path, pattern, repl)            → null   // writes to file(s)
pn replace(path, string, repl)             → null
pn replace(path, pattern, repl, options)   → null
pn replace(path, string, repl, options)    → null
```

**Registration:**

```cpp
// build_ast.cpp registrations (additions to existing find/replace)

// Procedural file replace: 3 or 4 args, pn (NEW — is_proc=true)
{SYSPROC_REPLACE_FILE, "replace", 3, &TYPE_NULL, true, true, false, LMD_TYPE_PATH, true},
{SYSPROC_REPLACE_FILE4, "replace", 4, &TYPE_NULL, true, true, false, LMD_TYPE_PATH, true},
```

**Overload resolution:** The compiler selects the overload using the **first argument's type**:
- First arg type is `LMD_TYPE_STRING`/`LMD_TYPE_SYMBOL` → `SYSFUNC_REPLACE` (pure `fn`)
- First arg type is `LMD_TYPE_PATH` → `SYSPROC_REPLACE_FILE` (procedural `pn`, enforces `pn` context at compile time)

---

## 3. Syntax Summary

```lambda
// === File Find (grep) ===
find(/src.'main.ls', digits)             // [{value: "42", index: 5}, ...] — same shape as string find
find(/src, digits)                       // [{file: /src.'main.ls', value: "42", index: 5}, ...] — dir: adds file field
find(/src.**, "TODO")                    // recursive wildcard grep

// === File Replace (sed) — pn only ===
pn main() {
    replace(/readme.md, "v1.0", "v2.0")          // replace in single file
    replace(/src, old_api, "fetchWidget($1)")     // replace in all files under /src
}
// fn context: replace(/file, ...) is a compile ERROR
```

---

## 4. Design Decisions

1. **Uniform `find()` return shape:** `find(file, pattern)` returns `{value, index}` — the same shape as `find(str, pattern)`. No extra `line`/`col` fields for single files. This keeps the return type predictable and composable. When the source is a directory or wildcard (multiple files), a `file` field is added to identify which file each match came from.

2. **Directory / wildcard recursion:** When the first argument is a directory path, `find()` recursively searches all files under it. Wildcard paths (`/dir.*`, `/dir.**`) are also supported through Lambda's existing `path_resolve_for_iteration()` which handles wildcard expansion. This mirrors `grep -r`.

3. **`pn replace()` for files — no pure file replace:** File-based `replace()` is always procedural because it writes to disk. There is no "return modified text" mode for files — if you want that, use `input(file) | replace(~, pattern, repl)` to read the file and do an in-memory replace. The `pn`/`fn` separation is enforced at compile time via `is_proc=true` in the sys func registration.

4. **Path type required for file operations:** String arguments are always treated as in-memory string content, never as file paths. Users must use Lambda path syntax (`/path.to.file`, `.relative.path`) to trigger file mode. This avoids ambiguity.

5. **Leverages existing infrastructure:** Uses `path_to_os_path()` for path resolution, `path_resolve_for_iteration()` for directory listing and wildcard expansion, `read_text_file()` / `write_text_file()` from `lib/file.c` for I/O.

6. **Options map:** Both `find()` and `pn replace()` accept an options map as the last argument: `{limit, ignore_case}`. This follows Lambda's convention for optional parameters (see `input()` options map pattern).

---

## 5. RE2 API Reference

| RE2 Method | Use Case | Lambda Feature |
|-----------|----------|----------------|
| `RE2::FindAndConsume(&input, re2, &match)` | Iterate all matches | `find(path, pattern)` |
| `RE2::GlobalReplace(&str, re2, repl)` | Replace all matches | `pn replace(path, pattern, repl)` |
| `RE2::Replace(&str, re2, repl)` | Replace first match | `pn replace(path, pattern, repl, {limit: 1})` |

---

## 6. Implementation Plan

1. Add file/path dispatch to `fn_find()` → single-file returns `{value, index}`, directory/wildcard returns `{file, value, index}`
2. Implement `fn_replace_file()` as a separate procedural function (`SYSPROC_REPLACE_FILE`, `is_proc=true`)
3. Add directory recursion: use `path_resolve_for_iteration()` to list children, recurse into subdirectories
4. Support options map (`{limit, ignore_case}`) as last argument for both
5. Add test scripts for file find, directory find, file replace

---

## 7. Files to Modify

| File | Changes |
|------|---------|
| `lambda/lambda-data.hpp` | Add `SYSPROC_REPLACE_FILE`, `SYSPROC_REPLACE_FILE4` enums (already reserved) |
| `lambda/lambda-eval.cpp` | Add `LMD_TYPE_PATH` dispatch to `fn_find2()`/`fn_find3()`; implement `fn_replace_file()` (procedural); add `find_in_directory()` / `replace_in_directory()` helpers |
| `lambda/re2_wrapper.hpp/cpp` | Possibly add `create_match_map_ext()` with `file` field for multi-file results |
| `lambda/build_ast.cpp` | Register `SYSPROC_REPLACE_FILE` (3-arg, `is_proc=true`), `SYSPROC_REPLACE_FILE4` (4-arg) |
| `lambda/mir.c` | Register `fn_replace_file` function pointers |
| `lambda/lambda.h` | Add `fn_replace_file3()`, `fn_replace_file4()` declarations |
| `lambda/lambda-embed.h` | Regenerate via `xxd -i lambda/lambda.h` |
| `lambda/path.c` | Possibly expose `path_to_os_path` as extern (already used internally) |
| `lib/file.h` / `lib/file.c` | Already has `read_text_file()` / `write_text_file()` — no changes expected |
| `test/lambda/` | Add test scripts + expected results for file find, directory find, file replace |

---

## References

- [Lambda String Pattern Enhancement v2](./Lambda_Type_String_Pattern2.md) — parent proposal (Phases 1–5)
- [Lambda System Functions](../doc/Lambda_Sys_Func.md)
- [RE2 Documentation](https://github.com/google/re2/wiki/Syntax)
- [RE2 C++ API](https://github.com/google/re2/blob/main/re2/re2.h)
