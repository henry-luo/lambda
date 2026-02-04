# Lambda File System Support Proposal

## Overview

This document proposes enhanced file system support for Lambda, introducing a procedural API for file and directory manipulation. While Lambda already supports reading files and directories as input, this proposal extends capabilities to include creating, writing, renaming, and deleting files and directories.

---

## 1. Survey of File System Support in Other Languages

### 1.1 Node.js (fs module)

Node.js provides both synchronous and asynchronous APIs through the `fs` module. Key characteristics:
- Promise-based API (`fs/promises`) and callback-based API
- Synchronous variants with `*Sync` suffix
- Comprehensive path manipulation via `path` module
- Stream-based reading/writing for large files

### 1.2 Python (os, pathlib, shutil)

Python offers multiple modules for file operations:
- `os` - Low-level OS interface
- `pathlib` - Object-oriented path handling (modern, preferred)
- `shutil` - High-level file operations (copy, move, archive)
- Context managers for safe file handling (`with open(...)`)

### 1.3 Go (os, io, filepath)

Go provides a minimal, explicit API:
- `os` - Core file operations
- `io` and `io/ioutil` - Reading/writing utilities
- `filepath` - Cross-platform path manipulation
- Explicit error handling (no exceptions)

### 1.4 Rust (std::fs, std::path)

Rust emphasizes safety and explicitness:
- `std::fs` - File system operations
- `std::path` - Path and PathBuf types
- Result-based error handling
- Ownership semantics for file handles

---

## 2. Comparison Table

### 2.1 File Operations

| Operation | Node.js (fs) | Python | Go (os) | Rust (std::fs) |
|-----------|--------------|--------|---------|----------------|
| Read file | `readFile()` | `open().read()` | `ReadFile()` | `read_to_string()` |
| Write file | `writeFile()` | `open().write()` | `WriteFile()` | `write()` |
| Append to file | `appendFile()` | `open('a').write()` | `OpenFile()` + flags | `OpenOptions::append()` |
| Create file | `writeFile()` | `open('x')` | `Create()` | `File::create()` |
| Delete file | `unlink()` | `os.remove()` | `Remove()` | `remove_file()` |
| Copy file | `copyFile()` | `shutil.copy()` | manual | `copy()` |
| Rename/Move | `rename()` | `os.rename()` | `Rename()` | `rename()` |
| File exists | `existsSync()` | `os.path.exists()` | `Stat()` err check | `Path::exists()` |
| File stat | `stat()` | `os.stat()` | `Stat()` | `metadata()` |
| Truncate | `truncate()` | `truncate()` | `Truncate()` | `set_len()` |
| Permissions | `chmod()` | `os.chmod()` | `Chmod()` | `set_permissions()` |

### 2.2 Directory Operations

| Operation | Node.js (fs) | Python | Go (os) | Rust (std::fs) |
|-----------|--------------|--------|---------|----------------|
| Create dir | `mkdir()` | `os.mkdir()` | `Mkdir()` | `create_dir()` |
| Create dirs (recursive) | `mkdir({recursive})` | `os.makedirs()` | `MkdirAll()` | `create_dir_all()` |
| Remove dir | `rmdir()` | `os.rmdir()` | `Remove()` | `remove_dir()` |
| Remove dir (recursive) | `rm({recursive})` | `shutil.rmtree()` | `RemoveAll()` | `remove_dir_all()` |
| List dir | `readdir()` | `os.listdir()` | `ReadDir()` | `read_dir()` |
| Walk dir | custom/glob | `os.walk()` | `WalkDir()` | `walkdir` crate |
| Current dir | `process.cwd()` | `os.getcwd()` | `Getwd()` | `current_dir()` |
| Change dir | `process.chdir()` | `os.chdir()` | `Chdir()` | `set_current_dir()` |
| Temp dir | `os.tmpdir()` | `tempfile` module | `TempDir()` | `temp_dir()` |

### 2.3 Path Operations

| Operation | Node.js (path) | Python (pathlib) | Go (filepath) | Rust (std::path) |
|-----------|----------------|------------------|---------------|------------------|
| Join paths | `join()` | `Path / "sub"` | `Join()` | `Path::join()` |
| Absolute path | `resolve()` | `resolve()` | `Abs()` | `canonicalize()` |
| Dirname | `dirname()` | `parent` | `Dir()` | `parent()` |
| Basename | `basename()` | `name` | `Base()` | `file_name()` |
| Extension | `extname()` | `suffix` | `Ext()` | `extension()` |
| Normalize | `normalize()` | `resolve()` | `Clean()` | `canonicalize()` |
| Is absolute | `isAbsolute()` | `is_absolute()` | `IsAbs()` | `is_absolute()` |
| Split | `parse()` | `parts` | `Split()` | `components()` |

---

## 3. Proposed Lambda File System API

### 3.1 Design Principles

1. **Procedural-only**: File system operations are side effects and must be called within procedural functions only.
2. **Explicit errors**: Return error values rather than throwing exceptions.
3. **Consistent naming**: Use clear, descriptive names following Lambda conventions.
4. **Path-first**: Paths as first argument for consistency.
5. **Sensible defaults**: Common operations should be simple; advanced options available.

### 3.2 Module Import

```lambda
import fs;  // Built-in system module (no quotes, no '.' prefix)

proc main() {
    fs.write_file("output.txt", "Hello, World!");
}
```

### 3.3 File Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file` | `(path: string) -> string \| error` | Read entire file as UTF-8 string |
| `read_bytes` | `(path: string) -> binary \| error` | Read entire file as binary data |
| `write_file` | `(path: string, content: string) -> null \| error` | Write string to file (creates/overwrites) |
| `write_bytes` | `(path: string, content: binary) -> null \| error` | Write binary data to file |
| `append_file` | `(path: string, content: string) -> null \| error` | Append string to file |
| `append_bytes` | `(path: string, content: binary) -> null \| error` | Append binary data to file |
| `copy_file` | `(src: string, dst: string) -> null \| error` | Copy file from src to dst |
| `move_file` | `(src: string, dst: string) -> null \| error` | Move/rename file |
| `remove_file` | `(path: string) -> null \| error` | Delete file |
| `truncate` | `(path: string, size: int) -> null \| error` | Truncate file to specified size |

### 3.4 Directory Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `create_dir` | `(path: string) -> null \| error` | Create single directory |
| `create_dir_all` | `(path: string) -> null \| error` | Create directory and all parents |
| `remove_dir` | `(path: string) -> null \| error` | Remove empty directory |
| `remove_dir_all` | `(path: string) -> null \| error` | Remove directory and all contents |
| `read_dir` | `(path: string) -> list \| error` | List directory entries |
| `copy_dir` | `(src: string, dst: string) -> null \| error` | Recursively copy directory |

### 3.5 Path Query Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `exists` | `(path: string) -> bool` | Check if path exists |
| `is_file` | `(path: string) -> bool` | Check if path is a file |
| `is_dir` | `(path: string) -> bool` | Check if path is a directory |
| `is_symlink` | `(path: string) -> bool` | Check if path is a symbolic link |
| `file_size` | `(path: string) -> int \| error` | Get file size in bytes |
| `modified_time` | `(path: string) -> datetime \| error` | Get last modification time |
| `created_time` | `(path: string) -> datetime \| error` | Get creation time (if available) |

### 3.6 Path Manipulation (Pure Functions)

These are pure functions and can be called in any context:

| Function | Signature | Description |
|----------|-----------|-------------|
| `join` | `(...parts: string) -> string` | Join path components |
| `dirname` | `(path: string) -> string` | Get parent directory |
| `basename` | `(path: string) -> string` | Get file/directory name |
| `extname` | `(path: string) -> string` | Get file extension (with dot) |
| `stem` | `(path: string) -> string` | Get filename without extension |
| `normalize` | `(path: string) -> string` | Normalize path (resolve `.` and `..`) |
| `absolute` | `(path: string) -> string` | Convert to absolute path |
| `relative` | `(path: string, base: string) -> string` | Get relative path from base |
| `is_absolute` | `(path: string) -> bool` | Check if path is absolute |
| `split` | `(path: string) -> list` | Split path into components |
| `with_extension` | `(path: string, ext: string) -> string` | Replace file extension |

### 3.7 Directory Traversal

| Function | Signature | Description |
|----------|-----------|-------------|
| `walk` | `(path: string) -> iterator` | Iterate through directory tree |
| `glob` | `(pattern: string) -> list` | Find files matching glob pattern |

### 3.8 Environment & Temp

| Function | Signature | Description |
|----------|-----------|-------------|
| `cwd` | `() -> string` | Get current working directory |
| `home_dir` | `() -> string \| null` | Get user's home directory |
| `temp_dir` | `() -> string` | Get system temp directory |
| `temp_file` | `(prefix: string?) -> string` | Create and return path to temp file |

---

## 4. Detailed API Specifications

### 4.1 `fs.read_file`

Read entire file contents as a UTF-8 string.

```lambda
proc example() {
    let content = fs.read_file("data.txt");
    if (is_error(content)) {
        print("Error: " + error_message(content));
        return;
    }
    print(content);
}
```

**Behavior:**
- Returns file contents as string on success
- Returns error if file doesn't exist, permission denied, or encoding error

### 4.2 `fs.write_file`

Write string content to a file, creating it if it doesn't exist.

```lambda
proc example() {
    let result = fs.write_file("output.txt", "Hello, Lambda!");
    if (is_error(result)) {
        print("Failed to write: " + error_message(result));
    }
}
```

**Behavior:**
- Creates file if it doesn't exist
- Overwrites existing content
- Creates parent directories if needed (optional: `create_parents: true`)
- Returns null on success, error on failure

### 4.3 `fs.read_dir`

List contents of a directory.

```lambda
proc list_files() {
    let entries = fs.read_dir("./src");
    if (is_error(entries)) {
        return;
    }

    for (entry in entries) {
        print(entry.name + " - " + (entry.is_dir ? "dir" : "file"));
    }
}
```

**Return Structure:**
```
[
    { name: "file.txt", is_file: true, is_dir: false, size: 1024 },
    { name: "subdir", is_file: false, is_dir: true, size: 0 },
    ...
]
```

### 4.4 `fs.walk`

Recursively traverse a directory tree.

```lambda
proc find_lambda_files() {
    for (entry in fs.walk("./src")) {
        if (fs.extname(entry.path) == ".ls") {
            print(entry.path);
        }
    }
}
```

**Entry Structure:**
```
{
    path: "src/utils/helper.ls",  // Full path
    name: "helper.ls",            // File name
    is_file: true,
    is_dir: false,
    depth: 2                      // Depth from start path
}
```

### 4.5 `fs.glob`

Find files matching a glob pattern.

```lambda
proc find_tests() {
    let test_files = fs.glob("test/**/*_test.ls");
    for (file in test_files) {
        print(file);
    }
}
```

**Supported Patterns:**
- `*` - Match any characters except path separator
- `**` - Match any characters including path separator (recursive)
- `?` - Match single character
- `[abc]` - Match character class
- `{a,b,c}` - Match alternatives

---

## 5. Error Handling

### 5.1 Error Types

All file system operations that can fail return an error type with these fields:

```
{
    code: "ENOENT",           // Error code (POSIX-like)
    message: "File not found: /path/to/file",
    path: "/path/to/file"     // Affected path
}
```

### 5.2 Common Error Codes

| Code | Description |
|------|-------------|
| `ENOENT` | No such file or directory |
| `EEXIST` | File already exists |
| `EACCES` | Permission denied |
| `ENOTDIR` | Not a directory |
| `EISDIR` | Is a directory (when file expected) |
| `ENOTEMPTY` | Directory not empty |
| `ENOSPC` | No space left on device |
| `EROFS` | Read-only file system |

### 5.3 Error Checking Patterns

```lambda
proc safe_read(path: string) -> string | null {
    let content = fs.read_file(path);

    // Pattern 1: Using is_error
    if (is_error(content)) {
        log.error("Read failed: " + error_message(content));
        return null;
    }

    return content;
}

proc safe_write(path: string, data: string) -> bool {
    // Pattern 2: Match on result
    match fs.write_file(path, data) {
        error(e) => {
            log.error("Write failed: " + e.message);
            return false;
        },
        _ => return true
    }
}
```

---

## 6. Implementation Considerations

### 6.1 Procedural Enforcement

File system functions that perform side effects (write, delete, create) must only be callable from within procedural functions (`proc`). Attempting to call them from pure functions should result in a compile-time error.

```lambda
// This should fail at compile time
let bad = fs.write_file("x.txt", "data");  // Error: fs.write_file requires procedural context

proc good() {
    fs.write_file("x.txt", "data");  // OK
}
```

### 6.2 Path Handling

- All paths are treated as UTF-8 strings
- Relative paths are resolved relative to current working directory
- Platform-specific path separators are handled automatically
- Path normalization removes redundant separators and resolves `.` and `..`

### 6.3 Encoding

- Text operations (`read_file`, `write_file`, etc.) assume UTF-8
- Binary operations (`read_bytes`, `write_bytes`) work with raw bytes
- Invalid UTF-8 in text mode results in an error

### 6.4 Atomicity

- `write_file` should write atomically (write to temp, then rename)
- `move_file` is atomic on same filesystem
- `copy_file` is not atomic

---

## 7. Usage Examples

### 7.1 Copy Files with Pattern

```lambda
import fs;

proc backup_lambda_files(src_dir: string, backup_dir: string) {
    fs.create_dir_all(backup_dir);

    let files = fs.glob(fs.join(src_dir, "**/*.ls"));
    for (file in files) {
        let rel = fs.relative(file, src_dir);
        let dst = fs.join(backup_dir, rel);
        fs.create_dir_all(fs.dirname(dst));
        fs.copy_file(file, dst);
        print("Copied: " + file + " -> " + dst);
    }
}
```

### 7.2 Clean Build Directory

```lambda
import fs;

proc clean_build() {
    let build_dir = "./build";

    if (fs.exists(build_dir)) {
        let result = fs.remove_dir_all(build_dir);
        if (is_error(result)) {
            print("Failed to clean: " + error_message(result));
            return;
        }
        print("Cleaned build directory");
    }

    fs.create_dir_all(build_dir);
    print("Created fresh build directory");
}
```

### 7.3 Process All JSON Files

```lambda
import fs;

proc process_json_files(dir: string) {
    for (entry in fs.walk(dir)) {
        if (!entry.is_file) continue;
        if (fs.extname(entry.path) != ".json") continue;

        let content = fs.read_file(entry.path);
        if (is_error(content)) {
            print("Skip: " + entry.path);
            continue;
        }

        let data = parse_json(content);
        // Process data...

        let output = fs.with_extension(entry.path, ".processed.json");
        fs.write_file(output, format_json(data));
    }
}
```

### 7.4 Safe File Update

```lambda
import fs;

proc safe_update(path: string, transform: (string) -> string) -> bool {
    // Read existing content
    let content = fs.read_file(path);
    if (is_error(content)) {
        return false;
    }

    // Transform
    let new_content = transform(content);

    // Write to temp file first
    let temp = path + ".tmp";
    let result = fs.write_file(temp, new_content);
    if (is_error(result)) {
        return false;
    }

    // Atomic rename
    result = fs.move_file(temp, path);
    if (is_error(result)) {
        fs.remove_file(temp);  // Clean up temp
        return false;
    }

    return true;
}
```

---

## 8. Summary

The proposed `fs` module provides:

- **19 file/directory operation functions** for reading, writing, and managing files
- **12 path manipulation functions** for working with file paths
- **4 traversal/search functions** for finding and iterating files
- **4 environment functions** for directory context

The API follows best practices from Node.js, Python, Go, and Rust:
- Clear, descriptive naming (influenced by Rust and Python)
- Error-as-value pattern (influenced by Go and Rust)
- Comprehensive path utilities (influenced by Node.js and Python)
- Recursive variants with `_all` suffix (influenced by Rust)

All mutating operations are restricted to procedural context, maintaining Lambda's functional purity where appropriate.
