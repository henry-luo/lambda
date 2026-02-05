# Lambda Path Mapping Proposal

## Overview

This document proposes a unified path notation system that maps file system paths and URLs to Lambda-style dot-separated paths. This enables seamless integration of external resources into Lambda's namespace system.

---

## 1. Core Mapping Rules

### 1.1 Basic Principle

| Source | Lambda Path |
|--------|-------------|
| Path separator `/` | Dot `.` |
| Scheme separator `://` | Single `.` |
| Relative path prefix | Leading `.` |

### 1.2 Built-in Schemes

Lambda supports **4 built-in schemes** out of the box:

| Scheme | Purpose | Example |
|--------|---------|--------|
| `file` | Local file system | `file.etc.hosts` |
| `http` | HTTP URLs | `http.'example.com'.api` |
| `https` | HTTPS URLs | `https.'api.github.com'.users` |
| `sys` | System/runtime paths | `sys.temp`, `sys.home` |

Other schemes (e.g., `ftp`, `s3`, `ssh`) must be imported first:

```lambda
import ftp;    // Import ftp scheme support
import s3;     // Import S3 scheme support

let remote = ftp.'ftp.example.com'.pub.'file.zip';
let bucket = s3.'my-bucket'.data.'archive.tar.gz';
```

### 1.3 Absolute URL Mapping

```
file:///etc/hosts          → file.etc.hosts
file:///home/user/data.json → file.home.user.'data.json'
http://example.com/api/v1   → http.'example.com'.api.v1
https://cdn.site.io/assets  → https.'cdn.site.io'.assets
```

### 1.4 Relative Path Mapping

Relative paths start with a leading dot, distinguishing them from absolute paths:

```
./src/main.ls              → .src.'main.ls'
../lib/utils.ls            → ..lib.'utils.ls'
data/input.json            → .data.'input.json'
```

### 1.5 Parent Directory Navigation

The `..` notation refers to the parent directory:

```lambda
let parent = ..;                    // Parent of current directory
let sibling = ..other;              // ../other
let file = ..lib.'utils.ls';        // ../lib/utils.ls
```

**Important**: `...` is reserved in Lambda for **variable arguments** (variadic functions). To navigate to grandparent or beyond, use parentheses:

```lambda
// Grandparent navigation
let grandparent = (..);..           // ../../ (parent of parent)
let great = ((..)..)..              // ../../../ (three levels up)

// With path segments
let file = (..)..shared.'config.json';   // ../../shared/config.json
```

**Syntax breakdown:**

| Expression | Meaning | Equivalent |
|------------|---------|------------|
| `..` | Parent directory | `../` |
| `(..)..` | Grandparent | `../../` |
| `((..)..)..` | Great-grandparent | `../../../` |
| `..sibling` | Sibling directory | `../sibling` |
| `(..)..shared` | Uncle directory | `../../shared` |

### 1.6 Windows Path Mapping

```
C:\Users\name\file.txt     → file.C.'Users'.name.'file.txt'
D:\data\                   → file.D.data
```

---

## 2. Handling Special Characters

### 2.1 The Dot Problem

File extensions and domain names contain dots, which conflict with the path separator. Solution: **quote segments containing dots**.

| Original | Lambda Path |
|----------|-------------|
| `file.txt` | `'file.txt'` |
| `example.com` | `'example.com'` |
| `config.dev.json` | `'config.dev.json'` |

**Rule**: Any path segment containing `.` must be quoted with single quotes.

### 2.2 Quoting Rules

Segments must be quoted when they contain:
- Dots (`.`)
- Spaces or whitespace
- Special characters: `@`, `#`, `$`, `%`, `&`, `?`, `=`, etc.
- Hyphens at start (to avoid ambiguity with operators)
- Numeric-only names (to avoid ambiguity with indices)

```
my file.txt                   → .'my file.txt'
data-2024.csv                 → .'data-2024.csv'
user@host                     → .'user@host'
100                           → .'100'        // Numeric, must quote
v2                            → .v2           // Alphanumeric, no quote needed
http://localhost:8080/api     → http.'localhost.:8080'.api
http://api.com/?q=test&n=10   → http.'api.com'.'?q=test&n=10'
http://docs.com/page#section  → http.'docs.com'.page.'#section'
ftp://user:pass@host.com/file → ftp.'user:pass@host.com'.file
```

### 2.3 Escape Sequences in Quotes

Within quoted segments:
- `\'` - Literal single quote
- `\\` - Literal backslash

```
file'name.txt              → .'file\'name.txt'
path\to\file               → .'path\\to\\file'
```

---

## 3. Glob and Wildcard Patterns

### 3.1 Supported Wildcards

Lambda currently supports two wildcard patterns:

| Pattern | Meaning | Example |
|---------|---------|--------|
| `*` | Match any single path segment | `.src.*` |
| `**` | Match zero or more path segments (recursive) | `.src.**` |

### 3.2 Single-Level Wildcard (`*`)

Match any single path segment:

```
.src.*                     → All entries directly in src/
file.etc.*                 → All files directly in /etc/
http.'api.com'.users.*     → All direct children of /users/
```

### 3.3 Multi-Level Wildcard (`**`)

Match any number of path segments (recursive):

```
.src.**                    → All files anywhere under src/
file.home.**               → All files under /home/
.**.config                 → All 'config' files in project
```

### 3.4 Future: Extended Patterns (Deferred)

The following patterns inside quoted segments are **deferred to future versions**:

```
// NOT YET SUPPORTED - Future work
.src.'*.{ls,json}'         → Brace expansion (alternatives)
.src.'[abc]*.ls'           → Character class patterns
.data.'file[0-9].txt'      → Range patterns
.src.'!test'.**            → Negation patterns
```

For now, use explicit iteration or filtering:

```lambda
// Current approach: filter manually
let all_files = .src.**;
let ls_files = all_files |> filter(f => f.extension == ".ls");
```

---

## 4. Path Operations

### 4.1 Path Literals and Lazy Evaluation

Lambda path literals can be written directly. **Path evaluation is always lazy** - paths are references/handles until their content is actually accessed:

```lambda
let config = file.etc.'nginx.conf';       // Just a path reference, file not read yet
let api = https.'api.github.com'.repos;   // Just a URL reference, not fetched yet
let local = .src.main;                    // Relative path reference

// File is read only when content is accessed:
for line in config {                      // File read happens here
    print(line);
}

// JSON is parsed only when iterated/accessed:
let json_data = .data.'config.json';      // Just a reference
for key in json_data {                    // File loaded & parsed here
    print(key + ": " + json_data[key]);
}
```

**Benefits of lazy evaluation:**
- No wasted I/O for unused paths
- Paths can be composed and passed around without triggering reads
- Error handling deferred to point of use
- Efficient conditional loading

### 4.2 Path Concatenation

Paths are concatenated using the `++` operator:

```lambda
let base = file.home.user;
let full = base ++ documents ++ 'report.pdf';  // Concatenation via ++

// Concatenate with string
let dir = .src;
let file = dir ++ 'main.ls';                   // .src.'main.ls'

// Dynamic segment via bracket notation
let name = "config.json";
let path = .src[name];                         // Bracket notation for dynamic segments

// Multiple segments
let output = .build ++ 'dist' ++ 'bundle.js';  // .build.dist.'bundle.js'
```

**Note:** The `.` separator is for static path literals only. Use `++` for runtime concatenation.

### 4.3 Path Decomposition

```lambda
let p = file.home.user.'data.json';

p.scheme      → "file"
p.segments    → ["home", "user", "data.json"]
p.parent      → file.home.user
p.name        → "data.json"
p.stem        → "data"
p.extension   → ".json"
p.is_absolute → true
p.is_relative → false
```

### 4.4 Path Conversion

```lambda
let p = .src.'main.ls';

to_string(p)          → "./src/main.ls"
to_uri(p)             → "file:///current/dir/src/main.ls"
to_lambda_path("./x") → .x
```

---

## 5. Integration with Data Access

### 5.1 File Access via Lazy Evaluation

Lambda paths are lazy references. Content is loaded when accessed:

```lambda
// Path is just a reference
let hosts = file.etc.hosts;              // No file read yet
let config = .data.'config.json';        // No file read yet

// Content loaded on access
let lines = hosts | split("\n");         // File read happens here
let value = config.key;                  // JSON parsed here

// Writing (in procedural context) uses fs module
import fs;
proc save() {
    fs.write_file(.output.'result.json', data);
}
```

### 5.2 URL Fetching

Simple GET requests work via lazy evaluation:

```lambda
// Simple HTTP GET (lazy)
let users = https.'api.github.com'.users;
let octocat = users.octocat;             // Fetched when accessed
```

For complex HTTP requests (POST, headers, authentication), use `input()`:

```lambda
// Complex URL fetching via input()
let data = input('https://api.example.com/data', {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: { key: "value" }
});

// With authentication
let private_data = input('https://api.example.com/private', {
    headers: { "Authorization": "Bearer " + token }
});
```

### 5.3 Directory Listing

```lambda
// List directory contents (lazy)
let files = .src.*;            // List of paths in src/
let all = .src.**;             // Recursive listing

// Iteration triggers actual directory read
for f in files {
    print(f.name);
}
```

---

## 6. The `sys` Scheme: System Information

The `sys` scheme provides access to system information, environment, and runtime paths. Refer to Lambda_Sysinfo.md.

---

## 7. Potential Issues and Mitigations

### 7.1 Ambiguity Issues

| Issue | Example | Mitigation |
|-------|---------|------------|
| Dot in name vs separator | `file.txt` vs `file/txt` | Quote segments with dots: `'file.txt'` |
| Numeric segments | `.data.1.value` | Require quotes for pure numbers: `.data.'1'.value` |
| Scheme vs path | `http.local` (scheme or path?) | Schemes are reserved keywords; `http` as path segment must be quoted: `.'http'.local` |
| Empty segments | `//double/slash` | Collapse or preserve as `''` empty string segment |

### 7.2 Reserved Scheme Names

The following are reserved as **built-in URL schemes** and cannot be used as unquoted path segments:
- `file`, `http`, `https`, `sys` (built-in)

Other schemes become reserved **after import**:
- After `import ftp;` → `ftp` becomes reserved
- After `import s3;` → `s3` becomes reserved

```lambda
// These are URL schemes:
file.etc.hosts
http.'example.com'

// To use as path segments, quote them:
.'file'.document          // ./file/document
.'http'.requests.log      // ./http/requests.log
.'sys'.config             // ./sys/config
```

### 7.3 Case Sensitivity

| Platform | File System | Recommendation |
|----------|-------------|----------------|
| Linux | Case-sensitive | Preserve case |
| macOS | Case-insensitive (usually) | Preserve case, compare case-insensitive |
| Windows | Case-insensitive | Preserve case, compare case-insensitive |
| URLs | Case-sensitive (path), case-insensitive (domain) | Preserve as-is |

**Rule**: Lambda paths preserve case but can be configured for case-insensitive matching.

### 7.4 Unicode and Encoding

- Lambda paths support full Unicode
- URL-encoded characters (`%20`, `%2F`) are decoded in Lambda paths
- File paths use UTF-8 encoding

```lambda
file.home.'文档'.'报告.pdf'     // Unicode path
http.'example.com'.'path%20name' → http.'example.com'.'path name'  // Decoded
```

### 7.5 Security Concerns

| Risk | Description | Mitigation |
|------|-------------|------------|
| Path traversal | `..` segments escaping sandbox | Normalize and validate resolved path |
| Symlink attacks | Following malicious symlinks | Option to disable symlink following |
| URL injection | Malicious URLs constructed dynamically | Validate URL components |
| Credential exposure | Credentials in URL paths | Warn/block credentials in paths |

### 7.6 Performance Considerations

- **Path parsing overhead**: Cache parsed path structures
- **Glob expansion**: Lazy evaluation for large directories
- **Network URLs**: Async fetching with caching
- **Path normalization**: Memoize normalized paths

---

## 8. Comparison with Other Path Systems

### 8.1 Shell Script Path Support

Shell scripts (Bash, Zsh, etc.) have extensive built-in path and glob support.

#### 7.1.1 Shell Path Features

| Feature | Shell Syntax | Lambda Equivalent | Status |
|---------|--------------|-------------------|--------|
| Absolute path | `/etc/hosts` | `file.etc.hosts` | ✅ |
| Relative path | `./src/file` | `.src.file` | ✅ |
| Parent dir | `../lib` | `..lib` | ✅ |
| Grandparent | `../../shared` | `(..)..shared` | ✅ |
| Home expansion | `~/documents` | `sys.home ++ documents` | ✅ |
| Variable expansion | `$HOME/file` | `[HOME] ++ file` | ✅ |
| Glob single | `*.txt` | `.*` (filter manually) | ✅ |
| Glob recursive | `**/*.js` | `.**` (filter manually) | ✅ |
| Brace expansion | `{a,b,c}.txt` | Deferred | ⏳ |
| Character class | `[0-9].log` | Deferred | ⏳ |
| Negation | `!(*.txt)` | Deferred | ⏳ |

#### 7.1.2 Shell Path Operations

```bash
# Shell
dirname /path/to/file.txt    # → /path/to
basename /path/to/file.txt   # → file.txt
realpath ./relative          # → /absolute/path
readlink -f symlink          # → resolved path

# Parameter expansion
file="/path/to/name.tar.gz"
${file%.*}                   # → /path/to/name.tar (remove shortest suffix)
${file%%.*}                  # → /path/to/name (remove longest suffix)
${file#*/}                   # → path/to/name.tar.gz (remove shortest prefix)
${file##*/}                  # → name.tar.gz (remove longest prefix)
```

#### 8.1.3 Shell vs Lambda: Pros and Cons

| Aspect | Shell | Lambda | Analysis |
|--------|-------|--------|----------|
| **Readability** | `/path/to/file` familiar | `path.to.file` consistent with code | Shell more familiar for sysadmins; Lambda integrates with code |
| **Whitespace** | Requires quoting: `"my file"` | Auto-quoted: `.'my file'` | Lambda safer by default |
| **Glob power** | Very powerful, mature | Comparable | Shell has decades of refinement |
| **Variable expansion** | `$var` inline | `[var]` bracket syntax | Lambda more explicit |
| **Composability** | String concatenation | First-class path objects | Lambda can validate and manipulate |
| **Type safety** | None (all strings) | Path types | Lambda prevents path/string confusion |
| **Cross-platform** | Unix-centric | Unified across OS | Lambda abstracts platform differences |
| **Error handling** | Silent failures common | Explicit errors | Lambda more robust |

**Lessons from Shell:**
1. ✅ Adopt glob patterns (`*`, `**`) - done
2. ✅ Support home directory expansion via `sys.home`
3. ⏳ Brace expansion for alternatives - deferred
4. ⏳ Character classes - deferred
5. ✅ Avoid implicit variable expansion (use `[var]` syntax)
6. ✅ Require explicit handling of missing files (lazy evaluation)

---

### 8.2 XPath in XQuery/XSLT

XPath is a query language for navigating XML/HTML documents, deeply integrated into XQuery and XSLT.

#### 8.2.1 XPath Features

| Feature    | XPath Syntax                | Lambda Path Analogy                 |
| ---------- | --------------------------- | ----------------------------------- |
| Root       | `/`                         | `file.` or `.`                      |
| Child      | `/child`                    | `.child`                            |
| Descendant | `//descendant`              | `.**descendant` or `.**.descendant` |
| Parent     | `..`                        | `..`                                |
| Current    | `.`                         | `.` (context-dependent)             |
| Attribute  | `@attr`                     | `.@attr` or `.attr`                 |
| Predicate  | `[condition]`               | `[condition]`                       |
| Wildcard   | `*`                         | `.*`                                |
| Position   | `[1]`, `[last()]`           | `.'1'` or `[0]`                     |
| Axis       | `ancestor::`, `following::` | Not directly supported              |

#### 8.2.2 XPath Expression Examples

```xpath
// XPath                           // Lambda Path Equivalent
/bookstore/book                    → .bookstore.book
//book                             → .**.book
/bookstore/book[1]                 → .bookstore.book.'0'
/bookstore/book[@lang='en']        → .bookstore.book[lang == 'en']
/bookstore/book[price>35]          → .bookstore.book[price > 35]
//title[@lang]                     → .**.title[has(@lang)]
/bookstore/book/title | //price    → Union not directly supported
ancestor::chapter                  → No direct equivalent (requires function)
following-sibling::*               → No direct equivalent
```

#### 8.2.3 XPath Axes (Navigation Directions)

XPath has 13 axes for document navigation:

| Axis | Description | Lambda Support |
|------|-------------|----------------|
| `child::` | Direct children | ✅ `.child` |
| `descendant::` | All descendants | ✅ `.**.child` |
| `parent::` | Parent node | ✅ `..` |
| `ancestor::` | All ancestors | ❌ Needs function |
| `following-sibling::` | Following siblings | ❌ Needs function |
| `preceding-sibling::` | Preceding siblings | ❌ Needs function |
| `following::` | All following nodes | ❌ Needs function |
| `preceding::` | All preceding nodes | ❌ Needs function |
| `attribute::` | Attributes | ⚠️ Via `@` prefix |
| `namespace::` | Namespace nodes | ❌ N/A for files |
| `self::` | Current node | ✅ `.` |
| `descendant-or-self::` | Self + descendants | ✅ `.**` |
| `ancestor-or-self::` | Self + ancestors | ❌ Needs function |

#### 8.2.4 XPath Predicates vs Lambda Filters

```xpath
// XPath predicates
/books/book[author='Smith']          // Filter by child value
/books/book[@year > 2000]            // Filter by attribute
/books/book[position() < 5]          // First 4 books
/books/book[contains(title, 'XML')]  // String function

// Lambda equivalent (proposed)
.books.book[author == 'Smith']
.books.book[@year > 2000]
.books.book[: < 5]                   // Slice notation
.books.book[contains(title, 'XML')]
```

#### 8.2.5 XPath vs Lambda: Pros and Cons

| Aspect | XPath | Lambda Path | Analysis |
|--------|-------|-------------|----------|
| **Purpose** | XML/document navigation | File/URL/data navigation | Different domains |
| **Separator** | `/` | `.` | Lambda consistent with member access |
| **Predicates** | `[expr]` very powerful | `[expr]` similar | XPath more mature |
| **Axes** | 13 navigation axes | Limited (child, descendant, parent) | XPath more expressive for trees |
| **Functions** | Extensive built-in | Needs library | XPath has 100+ functions |
| **Type system** | Node sequences | Path objects | Lambda more typed |
| **Wildcards** | `*` for elements | `*`, `**` for segments | Similar |
| **Union** | `\|` operator | Not built-in | XPath more expressive |
| **Namespaces** | Full support | N/A | XPath designed for XML |

**Lessons from XPath:**
1. ✅ Adopt predicate syntax `[condition]` for filtering
2. ✅ Support descendant axis (`**`)
3. ⚠️ Consider adding `ancestor` function for tree navigation
4. ⚠️ Consider union operator for combining paths
5. ❌ Axes like `following-sibling` are XML-specific, not needed for files
6. ✅ Built-in functions for string matching, position, etc.

---

### 8.3 Other Languages with Built-in Path Support

#### 8.3.1 PowerShell

PowerShell has sophisticated path support with providers:

```powershell
# PowerShell paths
Get-ChildItem -Path C:\Users\*\Documents  # Glob
Get-Item Env:\PATH                         # Environment provider
Get-Item HKLM:\SOFTWARE                    # Registry provider
Get-Item Cert:\LocalMachine\My             # Certificate store

# Path cmdlets
Split-Path "C:\folder\file.txt" -Leaf      # → file.txt
Split-Path "C:\folder\file.txt" -Parent    # → C:\folder
Join-Path "C:\folder" "sub" "file.txt"     # → C:\folder\sub\file.txt
Resolve-Path ".\relative"                   # → absolute path
Test-Path "C:\file.txt"                     # → $true/$false
```

**Key Features:**
- Provider abstraction (filesystem, registry, environment, certificates)
- Object pipeline (paths as objects, not strings)
- Consistent cmdlet naming

**Lessons:**
1. ✅ Provider/scheme abstraction (Lambda has `file.`, `http.`, etc.)
2. ✅ Paths as objects, not just strings
3. ⚠️ Consider additional "providers" (environment, config stores)

#### 8.3.2 Nix Expression Language

Nix has path literals as first-class values:

```nix
# Nix path literals
/etc/hosts                    # Absolute path (no quotes!)
./relative/path               # Relative path
~/user/path                   # Home-relative path
<nixpkgs>                     # Search path lookup

# Path operations
baseNameOf /foo/bar/baz       # → "baz"
dirOf /foo/bar/baz            # → /foo/bar
/foo + "/bar"                 # → /foo/bar (concatenation)
builtins.pathExists /etc/hosts # → true/false
```

**Key Features:**
- Paths are literals (no string quoting)
- Immutable/pure path handling
- Built-in for reproducible builds

**Lessons:**
1. ✅ Path literals without quotes (Lambda uses `file.etc.hosts`)
2. ✅ Path concatenation via operators
3. ✅ Pure/functional path operations

#### 8.3.3 Make/Makefile

Make has built-in path functions:

```makefile
# Make path functions
$(dir src/foo/bar.c)          # → src/foo/
$(notdir src/foo/bar.c)       # → bar.c
$(suffix src/foo/bar.c)       # → .c
$(basename src/foo/bar.c)     # → src/foo/bar
$(addsuffix .o,foo bar)       # → foo.o bar.o
$(addprefix src/,foo bar)     # → src/foo src/bar
$(wildcard src/*.c)           # → list of matching files
$(realpath ./relative)        # → absolute path
```

**Lessons:**
1. ✅ Batch operations on path lists (`addsuffix`, `addprefix`)
2. ✅ Wildcard as function
3. ⚠️ Consider batch path transformations

#### 8.3.4 Glob (Ruby, Python, Node.js)

```ruby
# Ruby
Dir.glob("**/*.rb")                    # Recursive glob
Dir.glob("src/{lib,test}/**/*.rb")     # Brace expansion
File.fnmatch("*.txt", "readme.txt")    # Pattern matching
```

```python
# Python pathlib + glob
from pathlib import Path
list(Path(".").glob("**/*.py"))        # Recursive
list(Path(".").glob("[0-9]*.log"))     # Character class
Path("src") / "main.py"                # Path composition
```

```javascript
// Node.js glob
const glob = require("glob");
glob.sync("**/*.js");                   // Recursive
glob.sync("src/{a,b}/**/*.ts");         // Brace expansion
```

**Common Features:**
- `**` for recursive descent
- `{}` for alternatives
- `[]` for character classes
- `?` for single character

---

### 8.4 Comprehensive Comparison Matrix

| Feature | Shell | XPath | PowerShell | Nix | Lambda |
|---------|-------|-------|------------|-----|--------|
| Path literal | ❌ (strings) | ❌ (strings) | ❌ (strings) | ✅ | ✅ |
| Separator | `/` | `/` | `\` or `/` | `/` | `.` |
| Glob `*` | ✅ | ✅ | ✅ | ❌ | ✅ |
| Recursive `**` | ✅ (bash 4+) | `//` | ❌ | ❌ | ✅ |
| Predicates | ❌ | ✅ `[expr]` | `-Filter` | ❌ | ✅ `[expr]` |
| Brace expansion | ✅ `{a,b}` | ❌ | ❌ | ❌ | ✅ `'{a,b}'` |
| Character class | ✅ `[a-z]` | ❌ | ✅ | ❌ | ✅ `'[a-z]'` |
| Home dir | ✅ `~` | ❌ | ❌ | ✅ `~` | ✅ `~` |
| Variable expansion | ✅ `$VAR` | ✅ `$var` | ✅ `$env:VAR` | ✅ | ✅ `[var]` |
| Provider/scheme | ❌ | ❌ | ✅ | ❌ | ✅ |
| Path composition | String concat | String concat | `Join-Path` | `+` | `.` concat |
| Cross-platform | ❌ | ✅ | ⚠️ | ✅ | ✅ |
| Type safety | ❌ | ❌ | ⚠️ | ✅ | ✅ |
| Tree axes | ❌ | ✅ (13 axes) | ❌ | ❌ | ⚠️ (limited) |

---

### 8.5 Summary: Lessons Learned

#### From Shell Scripts
| Adopt | Avoid |
|-------|-------|
| ✅ Glob patterns (`*`, `**`, `?`) | ❌ Implicit failures |
| ✅ Brace expansion `{a,b}` | ❌ Unquoted whitespace issues |
| ✅ Character classes `[a-z]` | ❌ Complex parameter expansion syntax |
| ✅ Home expansion `~` | ❌ Word splitting pitfalls |
| ✅ Recursive `**` | |

#### From XPath
| Adopt | Avoid |
|-------|-------|
| ✅ Predicate filters `[condition]` | ❌ 13 axis complexity |
| ✅ Descendant operator `//` → `**` | ❌ XML-specific features |
| ✅ Position indexing | ❌ Namespace complexity |
| ⚠️ Consider union operator | |
| ⚠️ Consider ancestor navigation | |

#### From PowerShell
| Adopt | Avoid |
|-------|-------|
| ✅ Provider/scheme abstraction | ❌ Verbose cmdlet names |
| ✅ Path objects (not strings) | ❌ Windows-centric defaults |
| ✅ Consistent API naming | |

#### From Nix
| Adopt | Avoid |
|-------|-------|
| ✅ Path literals (no quotes for simple paths) | ❌ Search path magic `<...>` |
| ✅ Pure/immutable path operations | |
| ✅ Composition via operators | |

---

### 8.6 Proposed Enhancements Based on Comparison

Based on this analysis, consider adding to Lambda Path in future versions:

1. **From Shell**: Extended glob patterns (DEFERRED)
   ```lambda
   // Future - currently use manual filtering
   .src.'*.{js,ts}'           // Brace alternatives
   .data.'[0-9][0-9].log'     // Character classes
   ```

2. **From XPath**: Ancestor navigation function
   ```lambda
   let file = .src.utils.'helper.ls';
   file.ancestors()           // → [.src.utils, .src, .]
   file.ancestors(2)          // → .src
   ```

3. **From XPath**: Union operator
   ```lambda
   .src.** | .lib.**          // Union of two patterns
   ```

4. **From PowerShell**: Additional providers/schemes (via import)
   ```lambda
   import env;    // Environment variables
   import config; // App configuration

   env.PATH                   // Environment variable
   config.app.setting         // App configuration
   ```

5. **From Nix**: Path arithmetic (ADOPTED as `++`)
   ```lambda
   let base = .src;
   let full = base ++ utils ++ 'helper.ls';  // Concatenation via ++
   ```

---

## 9. Grammar Specification

### 9.1 EBNF Grammar

```ebnf
lambda_path     = absolute_path | relative_path ;

absolute_path   = scheme "." path_segments ;
relative_path   = "." path_segments ;

scheme          = "file" | "http" | "https" | "sys" ;  // Built-in only

path_segments   = segment ( "." segment )* ;

segment         = identifier
                | quoted_segment
                | wildcard
                | bracket_expr ;

identifier      = letter ( letter | digit | "_" )* ;
quoted_segment  = "'" ( char | escape )* "'" ;
wildcard        = "*" | "**" ;
bracket_expr    = "[" expression "]" ;

escape          = "\\" | "\'" ;
char            = any character except "'" and "\" ;

// Path concatenation (runtime)
path_concat     = path "++" ( path | identifier | quoted_segment ) ;
```

### 9.2 Examples Parsed

```
file.etc.hosts
  → Scheme: file
  → Segments: ["etc", "hosts"]

.src.'main.ls'
  → Relative: true
  → Segments: ["src", "main.ls"]

https.'api.github.com'.repos.*.issues
  → Scheme: https
  → Segments: ["api.github.com", "repos", *, "issues"]
  → Wildcards: [position 3]
```

---

## 10. Usage Examples

### 10.1 Configuration Loading

```lambda
// Load config with fallback (lazy evaluation)
let config = .config.'local.json'
          ?? .config.'default.json'
          ?? {};

// Environment-specific config
let env = get_env("LAMBDA_ENV") ?? "dev";
let config_path = .config ++ [env + ".json"];
let config = config_path;  // Loaded when accessed
```

### 10.2 Asset Pipeline

```lambda
// Find all source files (lazy)
let sources = .src.**;

// Process each - filter manually since patterns in quotes not yet supported
for src in sources {
    if src.extension != ".ls" { continue; }
    let out = .build ++ [src.relative(.src).with_extension(".mir")];
    compile(src, out);
}
```

### 10.3 API Integration

```lambda
let github = https.'api.github.com';

proc get_user(username: string) {
    // Simple GET via lazy evaluation
    return github ++ users ++ [username];
}

proc create_issue(owner: string, repo: string, title: string, body: string) {
    // Complex POST via input()
    let url = "https://api.github.com/repos/" + owner + "/" + repo + "/issues";
    return input(url, {
        method: "POST",
        headers: { "Authorization": "Bearer " + token },
        body: { title: title, body: body }
    });
}
```

### 10.4 Multi-Source Data

```lambda
// Merge data from multiple sources (all lazy)
let local_data = .data.'users.json';
let remote_data = https.'api.example.com'.users;
let cached = file.tmp.'users.cache.json';

// First available source wins (loaded only when accessed)
let users = cached or remote_data or local_data;
```

---

## 11. Summary

### 11.1 Key Design Decisions

| Aspect            | Decision                           | Rationale                            |
| ----------------- | ---------------------------------- | ------------------------------------ |
| Built-in schemes  | `file`, `http`, `https`, `sys`     | Cover 99% of use cases               |
| Other schemes     | Require `import`                   | Extensible without bloat             |
| Separator         | `.` (dot)                          | Consistent with Lambda member access |
| Concatenation     | `++` operator                      | Explicit runtime concatenation       |
| Quoting           | Single quotes for special segments | Clear, unambiguous                   |
| Relative marker   | Leading `.`                        | Mirrors `./` in file paths           |
| Wildcards         | `*` and `**` only                  | Simple, covers most needs            |
| Extended patterns | Deferred to future                 | Avoid complexity                     |
| Evaluation        | Lazy (on access)                   | Efficient, no wasted I/O             |
| Dynamic segments  | `[expr]` brackets                  | Consistent with array access         |
| Complex URLs      | Via `input()`                      | Separation of concerns               |

### 11.2 Benefits

1. **Unified**: Single notation for files, URLs, and namespaces
2. **Familiar**: Dot notation is intuitive for programmers
3. **Lazy**: Paths are references until content is needed
4. **Efficient**: No wasted I/O for unused paths
5. **Extensible**: Import additional schemes as needed
6. **Safe**: Quoting rules prevent ambiguity
7. **Composable**: Paths are first-class values with `++` concatenation

### 11.3 Open Questions

1. How to handle very long paths (URL length limits)?
2. Should there be a path literal syntax (like regex `/pattern/`)?
3. How to represent relative URLs (same scheme, different host)?
4. Should symlinks be resolved automatically or exposed?
5. When to add extended patterns (`{a,b}`, `[0-9]`, etc.)?
