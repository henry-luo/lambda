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

---

# Output Functions and Operators

Lambda provides multiple ways to write data to files: the `output()` function and pipe-to-file operators.

## output(data, target, format?, mode?)

Write data to a file.

**Type:** Procedure

**Parameters:**
- `data` - Data to write (any type)
- `target` - File path (string, symbol, or Path)
- `format` *(optional)* - Format symbol: `'json`, `'yaml`, `'xml`, `'html`, `'markdown`, `'text`, `'toml`, `'ini`, `'mark`. If omitted, auto-detected from file extension.
- `mode` *(optional)* - Write mode: `"write"` (truncate, default) or `"append"`

**Format Auto-Detection** (when `format` is omitted or `null`):
- `.json` → JSON format
- `.yaml`, `.yml` → YAML format
- `.xml` → XML format
- `.html`, `.htm` → HTML format
- `.md` → Markdown format
- `.txt` → Text format
- `.toml` → TOML format
- `.ini` → INI format
- `.ls`, `.mark`, `.mk` → Mark format (Lambda's native format)
- Unknown/no extension → Based on data type:
  - String → text
  - Binary → binary
  - Other → Mark format

**Examples:**
```lambda
let data = {name: "Lambda", version: 1}

// Auto-detect format from extension
output(data, "/tmp/config.json")      // writes as JSON
output(data, "/tmp/config.yaml")      // writes as YAML
output(data, "/tmp/config.mk")        // writes as Mark
output("Hello", "/tmp/greeting.txt")  // writes as text

// Explicit format (overrides extension)
output(data, "/tmp/data.txt", 'json)  // force JSON format
output(data, "/tmp/data", 'yaml)      // write YAML to extensionless file

// Append mode
let entry1 = {event: "start", time: t'2026-02-06'}
let entry2 = {event: "end", time: t'2026-02-06'}
output(entry1, "/tmp/log.mk", null, "write")   // create/overwrite file
output(entry2, "/tmp/log.mk", null, "append")  // append to file
```

---

## Pipe-to-File Operators

Lambda provides concise pipe operators for writing data to files. These are syntactic sugar for the `output()` function.

### |> (Pipe Write)

Write data to a file, truncating if it exists.

**Syntax:** `data |> target`

**Equivalent to:** `output(data, target, null, "write")`

**Example:**
```lambda
{name: "Lambda", version: 1} |> "/tmp/config.mk"
"Hello, world!" |> "/tmp/greeting.txt"
42 |> "/tmp/answer.mk"
```

---

### |>> (Pipe Append)

Append data to a file, creating it if it doesn't exist.

**Syntax:** `data |>> target`

**Equivalent to:** `output(data, target, null, "append")`

**Example:**
```lambda
// Build a log file incrementally
{event: "start"} |> "/tmp/events.mk"      // create file
{event: "process"} |>> "/tmp/events.mk"   // append
{event: "end"} |>> "/tmp/events.mk"       // append
```

---

## Output Behavior by Data Type

| Data Type | Output Behavior |
|-----------|-----------------|
| String | Written as raw text (no formatting) |
| Binary | Written as raw bytes |
| Map, List, Array | Formatted according to detected/specified format |
| Scalars (int, bool, etc.) | Formatted according to detected/specified format |

**Note:** String and Binary data bypass format conversion and are written directly, regardless of the format parameter.

---

## Directory Creation

When the target path contains directory components, parent directories are automatically created if they don't exist:

```lambda
// Creates /tmp/output/reports/ if needed
{report: "Q1"} |> "/tmp/output/reports/q1.json"
```

---

## Complete Output Example

```lambda
pn main() {
    let users = [
        {name: "Alice", role: "admin"},
        {name: "Bob", role: "user"}
    ]
    
    // Write as different formats
    output(users, "./users.json")           // JSON
    output(users, "./users.yaml")           // YAML
    output(users, "./users.mk")             // Mark
    
    // Append log entries
    "=== Session Start ===" |> "./session.log"
    "Processing users..." |>> "./session.log"
    "Done!" |>> "./session.log"
    
    // Force format regardless of extension
    output(users, "./data.txt", 'json')      // JSON in .txt file
    
    print("Files written successfully")
}
```

---

## Related

- `input()` function for reading file contents
- `fs` module for file system operations (copy, move, delete, etc.)
- Path expressions: `/path.to.file`, `.relative.path`, `..parent.path`
