# Lambda Shell / I/O Module

This document describes the `io` module functions available in Lambda Script for file system operations and network I/O.

## Overview

The `io` module provides unified I/O operations for both local files and remote URLs. Most functions are **procedural** (have side effects), while `exists()` is a **global pure function** that can be used in functional contexts.

## Global Functions

### exists(path) → bool

Check if a file or directory exists. This is a **global function** (not under the `io` module).

**Type:** Pure function (can be used in functional expressions)

**Parameters:**
- `path` - File path (string, symbol, or Path type)

**Returns:** `true` if the path exists, `false` otherwise

**Example:**
```lambda
// In functional context
let config_exists = exists("/etc/config.json")

// In procedural context with conditional
if exists("./output") {
    print("Output directory exists")
}
```

---

## IO Module Functions

### io.fetch(target, options?) → string/binary

Fetch content from a URL or local file.

**Type:** Procedure

**Parameters:**
- `target` - URL or file path (string)
- `options` *(optional)* - Options map (reserved for future use)

**Returns:** Content as string or binary data

**Example:**
```lambda
// Fetch from HTTP URL
let data = io.fetch("https://httpbin.org/json")

// Fetch local file
let content = io.fetch("./data.txt")
```

---

### io.mkdir(path)

Create a directory, including parent directories if needed (like `mkdir -p`).

**Type:** Procedure

**Parameters:**
- `path` - Directory path to create (string)

**Example:**
```lambda
io.mkdir("./output/reports/2024")
```

---

### io.touch(path)

Create an empty file or update its modification time if it exists.

**Type:** Procedure

**Parameters:**
- `path` - File path (string)

**Example:**
```lambda
io.touch("./output/marker.txt")
```

---

### io.copy(source, destination)

Copy a file or directory to a new location. **Supports remote URLs as source**.

**Type:** Procedure

**Parameters:**
- `source` - Source path or URL (string). Can be:
  - Local file path: `"./data.json"`
  - HTTP/HTTPS URL: `"https://example.com/data.json"`
- `destination` - Destination path (string, must be local)

**Example:**
```lambda
// Copy local file
io.copy("./config.json", "./config.backup.json")
io.copy("./src", "./src_backup")  // copies directory recursively

// Copy from remote URL
io.copy("https://example.com/data.json", "./downloaded.json")
```

---

### io.move(source, destination)

Move a file or directory to a new location. Works across filesystems.

**Type:** Procedure

**Parameters:**
- `source` - Source path (string)
- `destination` - Destination path (string)

**Example:**
```lambda
io.move("./temp/output.txt", "./final/output.txt")
```

---

### io.rename(old_path, new_path)

Rename a file or directory. Must be on the same filesystem.

**Type:** Procedure

**Parameters:**
- `old_path` - Current path (string)
- `new_path` - New path (string)

**Example:**
```lambda
io.rename("./report.txt", "./report_final.txt")
```

---

### io.delete(path)

Delete a file or directory. Directories are deleted recursively.

**Type:** Procedure

**Parameters:**
- `path` - Path to delete (string)

**Example:**
```lambda
io.delete("./temp/cache.txt")
io.delete("./build")  // deletes directory and all contents
```

---

### io.symlink(target, link_path)

Create a symbolic link.

**Type:** Procedure

**Parameters:**
- `target` - Target path the symlink points to (string)
- `link_path` - Path for the new symlink (string)

**Example:**
```lambda
io.symlink("./config/production.json", "./config.json")
```

---

### io.chmod(path, mode)

Change file permissions.

**Type:** Procedure

**Parameters:**
- `path` - File path (string)
- `mode` - Permission mode as octal number (e.g., 644, 755)

**Example:**
```lambda
io.chmod("./script.sh", 755)
io.chmod("./config.json", 644)
```

---

## Complete Example

```lambda
// Procedural script with io module
pn main() {
    // Create output directory
    io.mkdir("./output")
    
    // Check if source exists before copying
    if exists("./data/input.csv") {
        io.copy("./data/input.csv", "./output/input.csv")
        print("Copied input file")
    }
    
    // Download from URL
    io.copy("https://example.com/config.json", "./output/remote_config.json")
    
    // Create marker file
    io.touch("./output/.processed")
    
    // Set permissions
    io.chmod("./output/.processed", 644)
    
    // Cleanup old temp files
    if exists("./temp") {
        io.delete("./temp")
        print("Cleaned up temp directory")
    }
    
    print("Done!")
}
```

## Implementation Notes

- All `io` module functions are **procedures** (`pn_io_*` prefix internally)
- `exists()` is a **global pure function** (`fn_exists` internally) and can be used in:
  - Functional expressions
  - `if` conditions in functional code
  - `let` bindings
- `io.copy()` supports remote URLs (http/https) as the source - data is fetched and saved locally
- Path arguments accept string paths; relative paths are resolved from the current working directory
- Error handling: Functions return `null` on failure and log errors

---

# Output Functions and Operators

Lambda provides multiple ways to write data to files: the `output()` function and pipe-to-file operators.

## output(data, target, options?)

Write data to a file.

**Type:** Procedure

**Parameters:**
- `data` - Data to write (any type)
- `target` - File path (string, symbol, or Path)
- `options` *(optional)* - Options map with:
  - `format` - Format string: `"json"`, `"yaml"`, `"xml"`, `"html"`, `"markdown"`, `"text"`, `"toml"`, `"ini"`, `"mark"`. If omitted, auto-detected from file extension.
  - `mode` - Write mode string: `"write"` (truncate, default) or `"append"`
  - `atomic` - Boolean: if `true`, write to temp file first then rename (default: `false`)

**Returns:** Number of bytes written on success, error on failure.

**Format Auto-Detection** (when `format` is omitted):
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
let bytes = output(data, "/tmp/config.json")  // writes as JSON, returns bytes
output(data, "/tmp/config.yaml")              // writes as YAML
output(data, "/tmp/config.mk")                // writes as Mark
output("Hello", "/tmp/greeting.txt")          // writes as text

// Explicit format (overrides extension)
output(data, "/tmp/data.txt", {format: "json"})  // force JSON format
output(data, "/tmp/data", {format: "yaml"})      // write YAML to extensionless file

// Append mode
let entry1 = {event: "start", time: t'2026-02-06'}
let entry2 = {event: "end", time: t'2026-02-06'}
output(entry1, "/tmp/log.mk", {})                  // create/overwrite file
output(entry2, "/tmp/log.mk", {mode: "append"})    // append to file

// Atomic writes (safe for concurrent access)
output(data, "/tmp/config.json", {atomic: true})   // writes atomically

// Combining options
output(data, "/tmp/data.out", {format: "json", atomic: true})
```

---

## Pipe-to-File Operators

Lambda provides concise pipe operators for writing data to files. These are syntactic sugar for the `output()` function.

### |> (Pipe Write)

Write data to a file, truncating if it exists.

**Syntax:** `data |> target`

**Returns:** Number of bytes written

**Example:**
```lambda
let bytes = {name: "Lambda", version: 1} |> "/tmp/config.mk"
"Hello, world!" |> "/tmp/greeting.txt"
42 |> "/tmp/answer.mk"
```

---

### |>> (Pipe Append)

Append data to a file, creating it if it doesn't exist.

**Syntax:** `data |>> target`

**Returns:** Number of bytes written

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
    let bytes = output(users, "./users.json")  // JSON, returns bytes written
    output(users, "./users.yaml")              // YAML
    output(users, "./users.mk")                // Mark
    
    // Append log entries
    "=== Session Start ===" |> "./session.log"
    "Processing users..." |>> "./session.log"
    "Done!" |>> "./session.log"
    
    // Force format regardless of extension
    output(users, "./data.txt", {format: "json"})   // JSON in .txt file
    
    // Atomic write for critical data
    output(users, "./config.json", {atomic: true})   // safe atomic write
    
    print("Files written successfully")
}
```

---

## I/O Target System

Lambda's `input()` and `output()` functions accept **targets** that identify where to read from or write to. The Target system provides unified handling for different location types.

### Accepted Target Types

| Type   | Description                   | Example                                           |
| ------ | ----------------------------- | ------------------------------------------------- |
| String | URL or relative path          | `"./data.json"`, `"https://api.example.com/data"` |
| Symbol | URL or relative path (quoted) | `'./data.json'`, `'https://example.com'`          |
| Path   | Lambda Path literal           | `/path.to.data`, `.relative.path`                 |

### URL Schemes

| Scheme | Description | Example |
|--------|-------------|---------|
| `file://` | Local file system | `"file:///home/user/data.json"` |
| `http://` | HTTP URL (read-only) | `"http://example.com/api/data"` |
| `https://` | HTTPS URL (read-only) | `"https://api.github.com/repos"` |
| `sys://` | System information | `"sys://env"`, `"sys://platform"` |
| *(none)* | Relative file path | `"./data.json"`, `"../config.yaml"` |

### Path Resolution

Relative paths (without a scheme) are resolved against the current working directory:

```lambda
// If cwd is /home/user/project/
input("./data/input.json")      // reads /home/user/project/data/input.json
input("../shared/config.yaml")  // reads /home/user/shared/config.yaml
```

### Lambda Path Type

Lambda provides a native `Path` type for cross-platform path handling:

```lambda
// Path literals use dots instead of slashes
let config = input(/etc.config.json)
let data = input(.data.input.csv)      // relative path
let parent = input(..parent.config.mk) // parent directory
```

**Path Type Features:**
- Cross-platform (no OS-specific separators)
- Type-safe (compile-time checked)
- Supports wildcards: `*` (single level), `**` (recursive)

### Remote URLs

HTTP/HTTPS targets are **read-only** and fetch content from the web:

```lambda
// Fetch JSON from REST API
let data = input("https://api.github.com/repos/owner/repo")

// Error: cannot write to remote URL
output(data, "https://example.com")  // ERROR
```

### Target Behavior Summary

| Target Type | input() | output() | Notes |
|-------------|---------|----------|-------|
| Relative path | ✅ | ✅ | Resolved against cwd |
| Absolute path | ✅ | ✅ | Used directly |
| file:// URL | ✅ | ✅ | Extracted pathname |
| http(s):// URL | ✅ | ❌ | Read-only |
| sys:// URL | ✅ | ❌ | System info (read-only) |
| Lambda Path | ✅ | ✅ | Cross-platform |

---

## Related

- `input()` function for reading file contents
- `io` module for file system operations (copy, move, delete, etc.)
- Path expressions: `/path.to.file`, `.relative.path`, `..parent.path`
