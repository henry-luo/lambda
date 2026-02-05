# Lambda Path Implementation: Lazy Loading for Files and Directories

## Overview

This document specifies how to implement proper lazy loading for Lambda paths, enabling paths to behave as first-class collection types that can be iterated, indexed, and queried without eagerly loading content.

### Design Goals

1. **Lazy evaluation**: Path content is loaded only when accessed
2. **Directory iteration**: `for item in dir_path` yields child paths (not characters)
3. **File iteration**: `for item in file_path` yields parsed content items
4. **Metadata access**: File metadata (name, size, modified) available without loading content
5. **Content caching**: Once loaded, content is cached in `path->result`
6. **Type-aware loading**: Different content types (JSON, text, binary) handled appropriately

---

## 1. Path Resolution Model

### 1.1 Path States

A Path can be in one of these states:

| State | `result` field | Description |
|-------|----------------|-------------|
| **Unresolved** | `0` | Path is just a reference, nothing loaded |
| **Resolved** | Non-zero `Item` | Content has been loaded and cached |

### 1.2 Resolution Semantics

**Non-existent paths resolve to `null`** (not error), following Lambda's optional chaining semantics:

```lambda
let f = file.nonexistent.path
f                    // null
f.name               // null (chained access on null)
for x in f { x }     // empty iteration (for-loop over null)
len(f)               // 0 (len of null)
```

**Access errors resolve to `error`** (path exists but cannot be opened):

```lambda
let f = file.root.protected_file   // file exists but no read permission
f                    // error
for x in f { x }     // error propagates
len(f)               // error
```

| Condition | Result | Rationale |
|-----------|--------|----------|
| Path doesn't exist | `null` | Optional chaining - graceful absence |
| Path exists, access denied | `error` | Runtime failure - should be noticed |
| Path exists, I/O error | `error` | Runtime failure - should be noticed |
| Empty directory | `[]` | Valid result - directory is empty |

**Use `exists(path)` to check if a path truly exists**:

```lambda
if exists(file.data.'config.json') {
    let config = file.data.'config.json'
    // use config...
}
```

### 1.3 Content Types

When a path is resolved, its `result` can be:

| Path Type | Resolved Content | Example |
|-----------|------------------|---------|
| Directory | `List` of child `Path` items | `file.src.*` → list of paths |
| Text file | `String` | `file.etc.hosts` → file text |
| JSON file | Parsed structure (`Map`, `List`, etc.) | `file.data.'config.json'` → parsed JSON |
| XML/HTML file | `Element` tree | `file.page.html` → DOM |
| Binary file | `Binary` | `file.data.image.png` → binary data |

---

## 2. Directory Path Resolution

### 2.1 Directory Listing Design

When iterating over a directory path, each entry should be a **new Path** extending the parent:

```
file.src.*  (directory path with wildcard)
    │
    ├── file.src.main.ls      (child path - file)
    ├── file.src.utils.ls     (child path - file)  
    ├── file.src.lib          (child path - directory)
    └── file.src.test         (child path - directory)
```

**Key principle**: Directory children are paths, not loaded content.

### 2.2 Path Metadata Structure

Each path can carry metadata without loading file content:

```c
// New: Path metadata structure (optional, allocated on demand)
typedef struct PathMeta {
    int64_t size;           // file size in bytes (-1 for dirs or unknown)
    DateTime modified;      // last modification time
    uint8_t flags;          // is_dir, is_link, is_file, etc.
    uint8_t mode;           // Unix permissions (compressed)
} PathMeta;

// Extended Path struct
struct Path {
    TypeId type_id;         // LMD_TYPE_PATH
    uint8_t flags;          // segment type + metadata_loaded flag
    uint16_t ref_cnt;       // reference count
    const char* name;       // segment name
    Path* parent;           // parent segment
    uint64_t result;        // cached resolved content (0 = not resolved)
    PathMeta* meta;         // optional metadata (NULL until stat'd)
};

// Flag bit for metadata
#define PATH_FLAG_META_LOADED  0x80  // bit 7: metadata has been loaded
```

### 2.3 resolve_path_for_iteration()

New function to resolve a path for iteration (directory or file):

```c
// In lambda-data-runtime.cpp or path.c

/**
 * Resolve path content for iteration.
 * - For directories: returns List of child Path items (with metadata)
 * - For files: returns parsed file content
 * - Caches result in path->result
 */
static Item resolve_path_for_iteration(Path* path) {
    if (!path) return ItemNull;
    
    // Already resolved?
    if (path->result != 0) {
        return {.item = path->result};
    }
    
    // Convert path to OS path string
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path, path_buf);
    const char* os_path = path_buf->str;
    
    // Check if directory or file
    struct stat st;
    if (stat(os_path, &st) != 0) {
        strbuf_free(path_buf);
        
        // Distinguish between "doesn't exist" and "access error"
        if (errno == ENOENT || errno == ENOTDIR) {
            // Path does not exist - return null (optional chaining semantics)
            log_debug("resolve_path: path does not exist: %s", os_path);
            return ItemNull;
        } else {
            // Path exists but cannot be accessed (EACCES, EIO, etc.) - return error
            log_error("resolve_path: access error for %s: %s", os_path, strerror(errno));
            return ItemError;
        }
    }
    
    Item result;
    if (S_ISDIR(st.st_mode)) {
        // Directory: list children as Path items
        result = resolve_directory_children(path, os_path);
    } else {
        // File: load and parse content
        result = resolve_file_content(path, os_path);
    }
    
    strbuf_free(path_buf);
    
    // Cache the result
    path->result = result.item;
    return result;
}
```

### 2.4 resolve_directory_children()

Create child paths for directory entries:

```c
/**
 * List directory children as Path items.
 * Each child is a new Path extending the parent.
 * File metadata (size, modified, mode) is loaded, but not file content.
 */
static Item resolve_directory_children(Path* parent_path, const char* dir_path) {
    DIR* dir = opendir(dir_path);
    if (!dir) {
        // Distinguish between "doesn't exist" and "access error"
        if (errno == ENOENT || errno == ENOTDIR) {
            log_debug("resolve_directory_children: directory does not exist: %s", dir_path);
            return ItemNull;
        } else {
            // Permission denied or other I/O error
            log_error("resolve_directory_children: access error for %s: %s", dir_path, strerror(errno));
            return ItemError;
        }
    }
    
    Pool* pool = eval_context_get_pool(context);
    List* children = list();  // Create result list (empty array for empty dirs)
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Create child path extending parent
        Path* child_path = path_extend(pool, parent_path, entry->d_name);
        if (!child_path) continue;
        
        // Load metadata for the child (but NOT content)
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat lst;
        if (lstat(full_path, &lst) == 0) {
            // Allocate and populate metadata
            PathMeta* meta = (PathMeta*)pool_calloc(pool, sizeof(PathMeta));
            if (meta) {
                meta->size = lst.st_size;
                // Convert st_mtime to DateTime
                meta->modified = *datetime_from_unix(pool, (int64_t)lst.st_mtime);
                meta->flags = S_ISDIR(lst.st_mode) ? 1 : 0;  // is_dir flag
                if (S_ISLNK(lst.st_mode)) meta->flags |= 2;  // is_link flag
                meta->mode = lst.st_mode & 0777;
                child_path->meta = meta;
                child_path->flags |= PATH_FLAG_META_LOADED;
            }
        }
        
        // Add child path to list
        list_push(children, (Item){.path = child_path});
    }
    
    closedir(dir);
    return (Item){.list = children};
}
```

### 2.5 resolve_file_content()

Load file content based on detected type:

```c
/**
 * Load and parse file content.
 * Auto-detects content type from extension/MIME.
 * Returns parsed structure (String, Map, Element, etc.)
 */
static Item resolve_file_content(Path* path, const char* file_path) {
    // Build URL string for input system
    StrBuf* url_buf = strbuf_new();
    strbuf_append_str(url_buf, "file://");
    strbuf_append_str(url_buf, file_path);
    
    String* url_str = heap_strcpy(url_buf->str, url_buf->length);
    strbuf_free(url_buf);
    
    // Use existing input system to load and parse
    Item content = fn_input1((Item){.item = s2it(url_str)});
    
    return content;
}
```

---

## 3. Lambda Operations on Paths

### 3.1 Operations Summary

| Operation | Directory Path | File Path |
|-----------|----------------|-----------|
| `len(path)` | Number of children | Length of content |
| `for x in path` | Iterate children (paths) | Iterate content items |
| `path[i]` | Get i-th child path | Get i-th content item |
| `path.name` | Last segment name | Last segment name |
| `path.size` | Dir total size (or -1) | File size in bytes |
| `path.modified` | Dir mtime | File mtime |
| `path.parent` | Parent path | Parent path |
| `path.scheme` | "file", "http", etc. | "file", "http", etc. |
| `path.is_dir` | true | false |
| `path.is_file` | false | true |
| `exists(path)` | true/false | true/false |

### 3.2 fn_len() for PATH

Update `fn_len()` to handle paths properly:

```c
case LMD_TYPE_PATH: {
    Path* path_val = item.path;
    if (!path_val) { size = 0; break; }
    
    // Resolve content if not cached
    if (path_val->result == 0) {
        Item resolved = resolve_path_for_iteration(path_val);
        if (resolved.item == ItemError.item) {
            return INT64_ERROR;
        }
        // result is now cached in path_val->result
    }
    
    // Get length from cached result
    Item cached = {.item = path_val->result};
    size = fn_len(cached);
    break;
}
```

### 3.3 item_at() for PATH

Update `item_at()` to handle paths:

```c
case LMD_TYPE_PATH: {
    Path* path_val = data.path;
    if (!path_val) return ItemNull;
    
    // Resolve content if not cached
    if (path_val->result == 0) {
        Item resolved = resolve_path_for_iteration(path_val);
        if (resolved.item == ItemError.item) {
            return ItemError;
        }
    }
    
    // Delegate to cached result
    Item cached = {.item = path_val->result};
    return item_at(cached, index);
}
```

### 3.4 Path Properties (item_attr for PATH)

Add path property access:

```c
// In item_attr() or new path_get_property()
case LMD_TYPE_PATH: {
    Path* path = data.path;
    if (!path) return ItemNull;
    
    // Intrinsic path properties (no I/O needed)
    if (strcmp(key, "name") == 0) {
        if (!path->name) return ItemNull;
        String* name = heap_strcpy(path->name, strlen(path->name));
        return (Item){.item = s2it(name)};
    }
    if (strcmp(key, "parent") == 0) {
        if (!path->parent || path_is_root(path)) return ItemNull;
        return (Item){.path = path->parent};
    }
    if (strcmp(key, "scheme") == 0) {
        const char* scheme = path_get_scheme_name(path);
        String* s = heap_strcpy(scheme, strlen(scheme));
        return (Item){.item = s2it(s)};
    }
    
    // Metadata properties (require stat, but not content loading)
    if (strcmp(key, "size") == 0 || strcmp(key, "modified") == 0 ||
        strcmp(key, "is_dir") == 0 || strcmp(key, "is_file") == 0 ||
        strcmp(key, "is_link") == 0 || strcmp(key, "mode") == 0) {
        
        // Ensure metadata is loaded
        if (!(path->flags & PATH_FLAG_META_LOADED)) {
            load_path_metadata(path);
        }
        
        PathMeta* meta = path->meta;
        if (!meta) return ItemNull;
        
        if (strcmp(key, "size") == 0) {
            return int64_item(meta->size);
        }
        if (strcmp(key, "modified") == 0) {
            return datetime_item(meta->modified);
        }
        if (strcmp(key, "is_dir") == 0) {
            return bool_item(meta->flags & 1);
        }
        if (strcmp(key, "is_file") == 0) {
            return bool_item(!(meta->flags & 1));
        }
        if (strcmp(key, "is_link") == 0) {
            return bool_item(meta->flags & 2);
        }
        if (strcmp(key, "mode") == 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%o", meta->mode);
            String* s = heap_strcpy(buf, strlen(buf));
            return (Item){.item = s2it(s)};
        }
    }
    
    // For other keys, resolve content and delegate
    if (path->result == 0) {
        resolve_path_for_iteration(path);
    }
    if (path->result != 0) {
        return item_attr((Item){.item = path->result}, key);
    }
    
    return ItemNull;
}
```

### 3.5 load_path_metadata()

Helper to stat a path and populate metadata:

```c
static void load_path_metadata(Path* path) {
    if (!path || (path->flags & PATH_FLAG_META_LOADED)) return;
    
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path, path_buf);
    
    struct stat st;
    if (stat(path_buf->str, &st) == 0) {
        Pool* pool = eval_context_get_pool(context);
        PathMeta* meta = (PathMeta*)pool_calloc(pool, sizeof(PathMeta));
        if (meta) {
            meta->size = st.st_size;
            meta->modified = *datetime_from_unix(pool, (int64_t)st.st_mtime);
            meta->flags = S_ISDIR(st.st_mode) ? 1 : 0;
            meta->mode = st.st_mode & 0777;
            path->meta = meta;
        }
    }
    
    path->flags |= PATH_FLAG_META_LOADED;
    strbuf_free(path_buf);
}
```

---

## 4. Wildcard Expansion

### 4.1 Wildcard Detection

Paths with wildcards (`*`, `**`) need special handling:

```c
bool path_has_wildcards(Path* path);  // Already exists in path.c

// Check if the leaf segment is a wildcard
bool path_ends_with_wildcard(Path* path) {
    if (!path) return false;
    LPathSegmentType seg_type = PATH_GET_SEG_TYPE(path);
    return seg_type == LPATH_SEG_WILDCARD || seg_type == LPATH_SEG_WILDCARD_REC;
}
```

### 4.2 resolve_path_for_iteration() with Wildcards

```c
static Item resolve_path_for_iteration(Path* path) {
    if (!path) return ItemNull;
    if (path->result != 0) return {.item = path->result};
    
    // Handle wildcards specially
    if (path_ends_with_wildcard(path)) {
        // Get parent path (directory to list)
        Path* parent = path->parent;
        if (!parent) return ItemError;
        
        StrBuf* path_buf = strbuf_new();
        path_to_os_path(parent, path_buf);
        
        bool recursive = PATH_GET_SEG_TYPE(path) == LPATH_SEG_WILDCARD_REC;
        Item result = expand_wildcard(parent, path_buf->str, recursive);
        
        strbuf_free(path_buf);
        path->result = result.item;
        return result;
    }
    
    // Normal path resolution (existing logic)
    // ...
}
```

### 4.3 expand_wildcard()

```c
static Item expand_wildcard(Path* base_path, const char* dir_path, bool recursive) {
    List* matches = list();
    expand_wildcard_recursive(base_path, dir_path, recursive, matches, 0, 16);
    return (Item){.list = matches};
}

static void expand_wildcard_recursive(Path* base, const char* dir_path, 
                                       bool recursive, List* matches,
                                       int depth, int max_depth) {
    if (depth > max_depth) return;
    
    DIR* dir = opendir(dir_path);
    if (!dir) return;
    
    Pool* pool = eval_context_get_pool(context);
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        
        // Create child path
        Path* child = path_extend(pool, base, entry->d_name);
        if (!child) continue;
        
        // Load metadata
        PathMeta* meta = (PathMeta*)pool_calloc(pool, sizeof(PathMeta));
        if (meta) {
            meta->size = st.st_size;
            meta->modified = *datetime_from_unix(pool, (int64_t)st.st_mtime);
            meta->flags = S_ISDIR(st.st_mode) ? 1 : 0;
            meta->mode = st.st_mode & 0777;
            child->meta = meta;
            child->flags |= PATH_FLAG_META_LOADED;
        }
        
        // Add to matches
        list_push(matches, (Item){.path = child});
        
        // Recurse into subdirectories for **
        if (recursive && S_ISDIR(st.st_mode)) {
            expand_wildcard_recursive(child, full_path, true, matches, depth + 1, max_depth);
        }
    }
    
    closedir(dir);
}
```

---

## 5. Usage Examples

### 5.1 Directory Iteration

```lambda
// List files in /etc (non-recursive)
let etc = file.etc.*
for f in etc {
    print(f.name + " - " + f.size + " bytes")
}

// Recursive listing
let all_src = file.src.**
for f in all_src where f.is_file {
    print(f.name)
}
```

### 5.2 JSON File Iteration

```lambda
// file.data.'config.json' is just a path
let config = file.data.'config.json'

// Iteration triggers lazy load + parse
for key, value at config {
    print(key + ": " + value)
}

// Or access directly (also triggers load)
let port = config.server.port
```

### 5.3 Text File Iteration

```lambda
// Iterate over characters
let hosts = file.etc.hosts
for ch in hosts {
    // ch is each character
}

// To iterate lines, use split
for line in split(hosts, "\n") {
    print(line)
}
```

### 5.4 Metadata Access (No Content Loading)

```lambda
let f = file.data.'large_file.bin'

// These don't load file content:
f.name       // "large_file.bin"
f.size       // 1234567890
f.modified   // 2024-01-15T10:30:00Z
f.is_file    // true
f.is_dir     // false

// This loads content:
len(f)       // triggers load for length
```

---

## 6. Implementation Checklist

### 6.1 Core Changes

- [ ] Add `PathMeta*` field to `Path` struct in `lambda.h`
- [ ] Add `PATH_FLAG_META_LOADED` flag constant
- [ ] Implement `resolve_path_for_iteration()` in `lambda-data-runtime.cpp`
- [ ] Implement `resolve_directory_children()` (returns `[]` for empty dir, `null` for non-existent)
- [ ] Implement `resolve_file_content()` (can delegate to existing `fn_input1`, returns `null` if not found)
- [ ] Implement `load_path_metadata()`
- [ ] Implement `expand_wildcard()` and `expand_wildcard_recursive()`
- [ ] Implement `fn_exists()` system function

### 6.2 Operation Updates

- [ ] Update `fn_len()` for PATH to use `resolve_path_for_iteration()`
- [ ] Update `item_at()` for PATH to use `resolve_path_for_iteration()`
- [ ] Add PATH case to `item_attr()` for property access
- [ ] Ensure for-loop transpilation works with PATH (uses `fn_len` + `item_at`)

### 6.3 Path Property Functions

- [ ] `path.name` - segment name
- [ ] `path.parent` - parent path
- [ ] `path.scheme` - scheme name
- [ ] `path.size` - file/dir size (from metadata)
- [ ] `path.modified` - modification time (from metadata)
- [ ] `path.is_dir` - directory check (from metadata)
- [ ] `path.is_file` - file check (from metadata)
- [ ] `path.is_link` - symlink check (from metadata)
- [ ] `path.mode` - Unix permissions (from metadata)

### 6.4 Tests

- [ ] Test directory iteration: `for f in file.test.*`
- [ ] Test recursive wildcard: `for f in file.test.**`
- [ ] Test JSON file iteration: `for k, v at json_path`
- [ ] Test text file as string
- [ ] Test metadata access without content load
- [ ] Test `len()` on directory path
- [ ] Test `len()` on file path
- [ ] Test indexing: `dir_path[0]`
- [ ] Test empty directory returns `[]`
- [ ] Test non-existent path returns `null`
- [ ] Test access-denied path returns `error`
- [ ] Test `exists()` on existing file/dir
- [ ] Test `exists()` on non-existent path
- [ ] Test for-loop over non-existent path (zero iterations)
- [ ] Test for-loop over access-denied path (error propagation)

---

## 7. Files to Modify

| File | Changes |
|------|---------|
| [lambda.h](../lambda/lambda.h) | Add `PathMeta` struct, `meta` field to `Path`, `PATH_FLAG_META_LOADED` |
| [lambda-data-runtime.cpp](../lambda/lambda-data-runtime.cpp) | Implement `resolve_path_for_iteration()`, update `item_at()` |
| [lambda-eval.cpp](../lambda/lambda-eval.cpp) | Update `fn_len()` for PATH |
| [path.c](../lambda/path.c) | Add `expand_wildcard()`, `load_path_metadata()` |
| [input/input.cpp](../lambda/input/input.cpp) | Possibly refactor to share logic with path resolution |

---

## 8. System Function: exists()

### 8.1 Specification

The `exists(path)` function checks if a file or directory exists at the given path:

```lambda
exists(file.etc.hosts)              // true
exists(file.nonexistent)            // false
exists(file.home.user.documents)    // true (directory)
exists(http.'api.example.com')      // true if reachable (HTTP HEAD)
```

### 8.2 Implementation

```c
// In lambda-eval.cpp

/**
 * Check if a path exists (file or directory).
 * For file:// paths: uses stat()
 * For http(s):// paths: uses HTTP HEAD request
 * Returns: bool Item (true/false)
 */
Item fn_exists(Item path_item) {
    TypeId type_id = get_type_id(path_item);
    
    // Handle string URLs as well as Path objects
    if (type_id == LMD_TYPE_STRING) {
        String* url = path_item.get_string();
        // Use URL parsing to determine scheme and check existence
        Url* parsed = url_parse(url->chars);
        if (!parsed) return bool_item(false);
        
        bool exists = false;
        if (parsed->scheme == URL_SCHEME_FILE) {
            struct stat st;
            exists = (stat(url_get_pathname(parsed), &st) == 0);
        } else if (parsed->scheme == URL_SCHEME_HTTP || 
                   parsed->scheme == URL_SCHEME_HTTPS) {
            // HTTP HEAD request to check existence
            exists = http_head_exists(url->chars);
        }
        url_destroy(parsed);
        return bool_item(exists);
    }
    
    if (type_id != LMD_TYPE_PATH) {
        return bool_item(false);  // Non-path types don't "exist"
    }
    
    Path* path = path_item.path;
    if (!path) return bool_item(false);
    
    // Convert path to OS path
    StrBuf* path_buf = strbuf_new();
    path_to_os_path(path, path_buf);
    
    const char* scheme = path_get_scheme_name(path);
    bool exists = false;
    
    if (strcmp(scheme, "file") == 0 || 
        strcmp(scheme, ".") == 0 || 
        strcmp(scheme, "..") == 0) {
        // Local file system - use stat
        struct stat st;
        exists = (stat(path_buf->str, &st) == 0);
    } else if (strcmp(scheme, "http") == 0 || strcmp(scheme, "https") == 0) {
        // HTTP(S) - use HEAD request
        exists = http_head_exists(path_buf->str);
    } else if (strcmp(scheme, "sys") == 0) {
        // System paths always "exist" if valid
        exists = true;
    }
    
    strbuf_free(path_buf);
    return bool_item(exists);
}
```

### 8.3 Registration

Add to system functions table:

```c
// In mir.c function table
{"fn_exists", (fn_ptr) fn_exists},

// In lambda.h declarations
Item fn_exists(Item path);
```

### 8.4 Usage Patterns

```lambda
// Guard file access
let config_path = file.data.'config.json'
let config = if exists(config_path) { config_path } else { {} }

// Check before iteration
let src = file.src.*
for f in src where exists(f) {
    // f is guaranteed to exist
}

// Conditional loading
let data = exists(file.cache.data) 
    ? file.cache.data 
    : fetch_from_remote()
```

---

## 9. Open Questions

1. **String vs Lines**: Should `for x in text_file` iterate characters or lines?
   - **Current**: Characters (string behavior)
   - **Alternative**: Lines (more useful for text processing)
   - **Recommendation**: Keep as characters for consistency; use `split(file, "\n")` for lines

2. **Caching Scope**: How long should `path->result` be cached?
   - **Current**: For lifetime of Path object
   - **Alternative**: Add TTL or invalidation mechanism
   - **Recommendation**: Keep simple caching for now

3. **Error Handling**: What happens if file doesn't exist during iteration?
   - **Decision**: 
     - Non-existent path → `null` (follows optional chaining)
     - Access error (permission denied, I/O error) → `error`
   - For-loop over `null` produces zero iterations
   - For-loop over `error` propagates the error
   - Use `exists(path)` to explicitly check before access

4. **Memory Management**: Who owns the resolved content?
   - **Recommendation**: Use heap allocation with ref counting (existing pattern)
