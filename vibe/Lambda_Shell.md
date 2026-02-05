# Lambda Shell / File System Module

This document describes the `fs` module functions available in Lambda Script for file system operations.

## Overview

The `fs` module provides file system operations that are commonly needed for scripting tasks. Most functions are **procedural** (have side effects), while `fs.exists()` is a **pure function** that can be used in functional contexts.

## Function Reference

### fs.exists(path) → bool

Check if a file or directory exists.

**Type:** Pure function (can be used in functional expressions)

**Parameters:**
- `path` - File path (string or Path type)

**Returns:** `true` if the path exists, `false` otherwise

**Example:**
```lambda
// In functional context
let config_exists = fs.exists("/etc/config.json")

// In procedural context with conditional
if fs.exists("./output") {
    print("Output directory exists")
}
```

---

### fs.mkdir(path)

Create a directory, including parent directories if needed (like `mkdir -p`).

**Type:** Procedure

**Parameters:**
- `path` - Directory path to create (string)

**Example:**
```lambda
fs.mkdir("./output/reports/2024")
```

---

### fs.touch(path)

Create an empty file or update its modification time if it exists.

**Type:** Procedure

**Parameters:**
- `path` - File path (string)

**Example:**
```lambda
fs.touch("./output/marker.txt")
```

---

### fs.copy(source, destination)

Copy a file or directory to a new location.

**Type:** Procedure

**Parameters:**
- `source` - Source path (string)
- `destination` - Destination path (string)

**Example:**
```lambda
fs.copy("./config.json", "./config.backup.json")
fs.copy("./src", "./src_backup")  // copies directory recursively
```

---

### fs.move(source, destination)

Move a file or directory to a new location. Works across filesystems.

**Type:** Procedure

**Parameters:**
- `source` - Source path (string)
- `destination` - Destination path (string)

**Example:**
```lambda
fs.move("./temp/output.txt", "./final/output.txt")
```

---

### fs.rename(old_path, new_path)

Rename a file or directory. Must be on the same filesystem.

**Type:** Procedure

**Parameters:**
- `old_path` - Current path (string)
- `new_path` - New path (string)

**Example:**
```lambda
fs.rename("./report.txt", "./report_final.txt")
```

---

### fs.delete(path)

Delete a file or directory. Directories are deleted recursively.

**Type:** Procedure

**Parameters:**
- `path` - Path to delete (string)

**Example:**
```lambda
fs.delete("./temp/cache.txt")
fs.delete("./build")  // deletes directory and all contents
```

---

### fs.symlink(target, link_path)

Create a symbolic link.

**Type:** Procedure

**Parameters:**
- `target` - Target path the symlink points to (string)
- `link_path` - Path for the new symlink (string)

**Example:**
```lambda
fs.symlink("./config/production.json", "./config.json")
```

---

### fs.chmod(path, mode)

Change file permissions.

**Type:** Procedure

**Parameters:**
- `path` - File path (string)
- `mode` - Permission mode as octal number (e.g., 644, 755)

**Example:**
```lambda
fs.chmod("./script.sh", 755)
fs.chmod("./config.json", 644)
```

---

## Complete Example

```lambda
// Procedural script with fs module
pn main() {
    // Create output directory
    fs.mkdir("./output")
    
    // Check if source exists before copying
    if fs.exists("./data/input.csv") {
        fs.copy("./data/input.csv", "./output/input.csv")
        print("Copied input file")
    }
    
    // Create marker file
    fs.touch("./output/.processed")
    
    // Set permissions
    fs.chmod("./output/.processed", 644)
    
    // Cleanup old temp files
    if fs.exists("./temp") {
        fs.delete("./temp")
        print("Cleaned up temp directory")
    }
    
    print("Done!")
}
```

## Implementation Notes

- All `fs` functions except `fs.exists()` are **procedures** (`pn_*` prefix internally)
- `fs.exists()` is a **pure function** (`fn_fs_exists` internally) and can be used in:
  - Functional expressions
  - `if` conditions in functional code
  - `let` bindings
- Path arguments accept string paths; relative paths are resolved from the current working directory
- Error handling: Functions return `null` on failure and log errors

## Related

- Path expressions: `/path.to.file`, `.relative.path`, `..parent.path`
- `input()` function for reading file contents
- `output()` procedure for writing data to files
