# Converting `generate_premake.py` to Lambda Script

## Analysis: Feasibility and Language Improvements

This document analyzes the feasibility of converting the Python-based Premake5 generator (`utils/generate_premake.py`) to Lambda Script, identifies gaps in Lambda's current feature set, and proposes improvements.

---

## Overview of `generate_premake.py`

The script is a ~3000 line Python program that:

1. **Reads** `build_lambda_config.json` configuration
2. **Detects** the current platform (macOS, Linux, Windows)
3. **Generates** a `premake5.lua` build file with:
   - Workspace configuration
   - Library projects (static/dynamic)
   - Executable targets
   - Test projects
   - Platform-specific compiler flags, libraries, and paths
4. **Writes** the output Lua file

### Key Python Features Used

| Feature | Usage in Script |
|---------|-----------------|
| Classes | `PremakeGenerator` with ~25 methods |
| JSON parsing | `json.load()` for config |
| Platform detection | `platform.system()` |
| String formatting | f-strings, `str.format()` |
| List operations | `extend()`, `append()`, comprehensions |
| Dict operations | `.get()`, iteration, nested access |
| File I/O | `open()`, `write()` |
| Path manipulation | `os.path.basename()` |
| Environment | `os.environ`, `sys.argv` |

---

## Lambda's Current Capabilities

### ✅ Fully Supported

| Feature | Lambda Equivalent |
|---------|-------------------|
| JSON parsing | `input("file.json", 'json)` |
| Maps/dicts | `{key: value}`, `map.field`, `map["key"]` |
| Arrays/lists | `[1, 2, 3]`, for-expressions |
| String manipulation | `split`, `str_join`, `replace`, `contains`, `starts_with`, `ends_with`, `trim` |
| Functions | `fn` (pure) and `pn` (procedural) |
| File output | `output(data, "file.lua")` |
| Mutable state | `var` declarations in `pn` functions |
| Loops | `while`, `for` with `break`/`continue` |
| Null-safe access | `config.platforms.linux` (auto null-propagation) |

### ✅ Available via `sys://` Input

Lambda has a **system information input** (`input("sys://system/info")`) that provides:

```lambda
let sysinfo = input("sys://system/info")

// Available fields:
sysinfo.platform.value      // "darwin", "linux", "windows"
sysinfo.os.name             // "Darwin", "Linux", "Windows"
sysinfo.os.version          // Kernel version string
sysinfo.os.machine          // "x86_64", "arm64", etc.
sysinfo.hostname.value      // Machine hostname
sysinfo.uptime.seconds      // System uptime
```

This addresses the critical **platform detection** requirement!

### ⚠️ Partially Supported (Workarounds Needed)

| Feature | Status | Workaround |
|---------|--------|------------|
| CLI arguments | No `argv` access | Could use config file or environment |
| String interpolation | No f-strings | Use functional statements or `+` concatenation |
| Classes/OOP | Not supported | Pass state maps through functions |

### ✅ Available via Directory Input

Lambda has **directory listing support** via `input()` on directory paths:

```lambda
let dir = input("./src")          // Non-recursive listing
let dir = input("./src", {recursive: true, max_depth: 3})

// Returns <dir> element with children:
// dir.name          // "src"
// dir.path          // "./src" (full path)
// dir.size          // Directory size
// dir.modified      // DateTime of last modification
// dir.mode          // Permission string (e.g., "755")

// Children are <file> or <dir> elements:
for entry in dir {
    entry.name       // Filename
    entry.size       // File size (int64)
    entry.modified   // DateTime
    entry.mode       // Permissions
    entry.is_link    // true if symbolic link
}
```

This addresses **directory listing** and partially addresses **file existence** (try to input, check for null/error).

### ❌ Remaining Missing Features

| Feature | Python | Lambda | Impact |
|---------|--------|--------|--------|
| Environment variables | `os.environ["VAR"]` | — | Medium |
| File existence check | `os.path.exists()` | Workaround via `input()` | Low |
| Path manipulation | `os.path.basename()` | — | Low |
| Symlink creation | `os.symlink()` | — | Low |

---

## Conversion Strategy

### 1. State Management (Replacing Classes)

Python uses a class with instance variables:

```python
class PremakeGenerator:
    def __init__(self):
        self.config = json.load(...)
        self.premake_content = []
        self.use_linux_config = False
        # ...
```

**Lambda approach**: Use a state map passed through functions:

```lambda
type GeneratorState = {
    config: any,
    content: [string],
    platform: string,
    use_linux: bool,
    use_macos: bool,
    use_windows: bool,
    external_libraries: {string: any}
}

fn create_state(config_path: string) -> GeneratorState => {
    let config = input(config_path, 'json)
    let sysinfo = input("sys://system/info")
    let platform = sysinfo.platform.value
    
    {
        config: config,
        content: [],
        platform: platform,
        use_linux: platform == "linux",
        use_macos: platform == "darwin",
        use_windows: platform == "windows",
        external_libraries: {}
    }
}
```

### 2. Building Output Content

Python mutates a list:
```python
self.premake_content.extend([
    f'project "{name}"',
    f'    kind "{kind}"',
])
```

**Lambda approach**: Functional concatenation or procedural mutation:

```lambda
// Functional style
fn add_project(state: GeneratorState, name: string, kind: string) -> GeneratorState => {
    let new_lines = [
        "project \"" + name + "\"",
        "    kind \"" + kind + "\""
    ]
    {...state, content: state.content ++ new_lines}
}

// Procedural style
pn add_project(state: GeneratorState, name: string, kind: string) {
    var content = state.content
    content = content ++ [
        "project \"" + name + "\"",
        "    kind \"" + kind + "\""
    ]
    {...state, content: content}
}
```

### 3. Platform-Specific Logic

Python:
```python
if self.use_linux_config:
    linux_config = platforms_config.get('linux', {})
```

**Lambda equivalent** (with null-safe access):
```lambda
fn get_platform_config(state: GeneratorState) => {
    let platforms = state.config.platforms
    if (state.use_linux) platforms.linux or {}
    else if (state.use_macos) platforms.macos or {}
    else if (state.use_windows) platforms.windows or {}
    else {}
}
```

### 4. String Building for Lua Output

Python f-strings:
```python
f'    language "{language}"'
f'    targetdir "{target_dir}"'
```

**Lambda helper function**:
```lambda
fn lua_line(indent: int, key: string, value: string) => 
    fill(indent, " ").str_join("") + key + " \"" + value + "\""

// Usage
lua_line(4, "language", "C++")  // '    language "C++"'
```

Or with a template approach:
```lambda
fn lua_setting(name: string, value: string) =>
    "    " + name + " \"" + value + "\""
```

---

## Proposed Lambda Script Structure

```lambda
// generate_premake.ls

// =============================================================================
// Types
// =============================================================================

type GeneratorState = {
    config: any,
    content: [string],
    platform: string,
    use_linux: bool,
    use_macos: bool,  
    use_windows: bool,
    external_libraries: {string: any}
}

type LibraryInfo = {
    name: string,
    include: string?,
    link: string?,
    sources: [string]?
}

// =============================================================================
// Initialization
// =============================================================================

fn create_state(config_path: string) -> GeneratorState => {
    let config = input(config_path, 'json)
    let sysinfo = input("sys://system/info")
    let platform = sysinfo.platform.value
    
    let state = {
        config: config,
        content: [],
        platform: platform,
        use_linux: platform == "linux",
        use_macos: platform == "darwin",
        use_windows: platform == "windows",
        external_libraries: {}
    }
    
    // Parse external libraries
    parse_external_libraries(state)
}

fn parse_external_libraries(state: GeneratorState) -> GeneratorState => {
    let libs = state.config.libraries or []
    let parsed = (for lib in libs {
        if (lib.name != null) {
            name: lib.name,
            include: lib.include,
            link: lib.link
        } else null
    })
    let lib_map = {} // Build map from array
    {...state, external_libraries: lib_map}
}

// =============================================================================
// Lua Code Generation Helpers
// =============================================================================

fn indent(n: int) => fill(n, " ").str_join("")

fn lua_string(value: string) => "\"" + value + "\""

fn lua_setting(name: string, value: string) =>
    indent(4) + name + " " + lua_string(value)

fn lua_block_start(name: string, value: string) =>
    indent(4) + name + " { " + lua_string(value)

fn lua_list(name: string, items: [string]) => {
    let header = indent(4) + name + " {"
    let body = (for item in items indent(8) + lua_string(item) + ",")
    let footer = indent(4) + "}"
    [header] ++ body ++ [footer]
}

// =============================================================================
// Workspace Generation
// =============================================================================

fn generate_workspace(state: GeneratorState) -> GeneratorState => {
    let config = state.config
    let workspace_name = config.workspace_name or "Lambda"
    let toolset = if (state.use_macos or state.use_linux) "clang" else "gcc"
    
    let lines = [
        "-- Generated by generate_premake.ls",
        "-- Platform: " + state.platform,
        "",
        "workspace " + lua_string(workspace_name),
        lua_setting("configurations", "{ \"Debug\", \"Release\" }"),
        lua_setting("platforms", "{ \"native\" }"),
        lua_setting("location", "build/premake"),
        lua_setting("toolset", toolset),
        "",
        indent(4) + "cppdialect \"C++17\"",
        indent(4) + "cdialect \"C99\"",
        indent(4) + "warnings \"Extra\"",
        ""
    ]
    
    {...state, content: state.content ++ lines}
}

// =============================================================================
// Library Project Generation
// =============================================================================

fn generate_library_project(state: GeneratorState, lib: any) -> GeneratorState => {
    let name = lib.name
    let kind = if (lib.link == "dynamic") "SharedLib" else "StaticLib"
    let language = lib.language or "C"
    let sources = lib.source_files or lib.sources or []
    
    let header_lines = [
        "",
        "project " + lua_string(name),
        lua_setting("kind", kind),
        lua_setting("language", language),
        lua_setting("targetdir", "build/lib"),
        lua_setting("objdir", "build/obj/%{prj.name}"),
        ""
    ]
    
    let source_lines = if (len(sources) > 0) {
        lua_list("files", sources)
    } else []
    
    {...state, content: state.content ++ header_lines ++ source_lines}
}

fn generate_library_projects(state: GeneratorState) -> GeneratorState => {
    let targets = state.config.targets or []
    let lib_targets = (for t in targets 
        if (t.name != null and (t.link == "static" or t.link == "dynamic"))
            t 
        else null)
    
    // Fold over libraries, accumulating state
    var current_state = state
    for lib in lib_targets {
        current_state = generate_library_project(current_state, lib)
    }
    current_state
}

// =============================================================================
// Test Project Generation
// =============================================================================

fn generate_test_project(state: GeneratorState, test: any) -> GeneratorState => {
    let name = test.name or test.test
    let source = test.source or test.test
    let language = if (source.ends_with(".cpp")) "C++" else "C"
    
    let lines = [
        "",
        "project " + lua_string(name),
        lua_setting("kind", "ConsoleApp"),
        lua_setting("language", language),
        lua_setting("targetdir", "test"),
        lua_setting("objdir", "build/obj/%{prj.name}"),
        lua_setting("targetextension", ".exe"),
        ""
    ] ++ lua_list("files", [source])
    
    {...state, content: state.content ++ lines}
}

fn generate_test_projects(state: GeneratorState) -> GeneratorState => {
    let test_config = state.config.test or {}
    let suites = test_config.test_suites or []
    
    var current_state = state
    for suite in suites {
        let tests = suite.tests or []
        for test in tests {
            current_state = generate_test_project(current_state, test)
        }
    }
    current_state
}

// =============================================================================
// Main Program Generation
// =============================================================================

fn generate_main_program(state: GeneratorState) -> GeneratorState => {
    let config = state.config
    let output_name = config.output or "lambda.exe"
    let project_name = output_name.replace(".exe", "")
    let source_files = config.source_files or []
    
    let lines = [
        "",
        "project " + lua_string(project_name),
        lua_setting("kind", "ConsoleApp"),
        lua_setting("language", "C++"),
        lua_setting("targetdir", "."),
        lua_setting("objdir", "build/obj/%{prj.name}"),
        lua_setting("targetname", project_name),
        lua_setting("targetextension", ".exe"),
        ""
    ] ++ lua_list("files", source_files)
    
    {...state, content: state.content ++ lines}
}

// =============================================================================
// Entry Point
// =============================================================================

pn main() {
    let config_path = "build_lambda_config.json"
    
    // Initialize state with platform detection
    var state = create_state(config_path)
    
    // Generate all sections
    state = generate_workspace(state)
    state = generate_library_projects(state)
    state = generate_main_program(state)
    state = generate_test_projects(state)
    
    // Determine output filename based on platform
    let output_file = if (state.use_macos) "premake5.mac.lua"
                      else if (state.use_linux) "premake5.linux.lua"
                      else "premake5.windows.lua"
    
    // Write output
    let content = state.content.str_join("\n")
    output(content, output_file)
    
    print("Generated: " + output_file)
}
```

---

## Recommended Lambda Improvements

### Priority 1: Environment Variables (High Value)

```lambda
// Proposed syntax
env("PATH")                    // Get environment variable
env("HOME", "/default")        // With default value
```

**Implementation**: Add `SYSFUNC_ENV` in `build_ast.cpp`, implement in `lambda-proc.cpp` using `getenv()`.

### Priority 2: Command-Line Arguments (High Value)

```lambda
// Proposed syntax  
args()                         // Returns [string] of CLI args
args(0)                        // Get specific argument
```

**Implementation**: Pass `argc`/`argv` to runtime, expose via `SYSFUNC_ARGS`.

### Priority 3: Functional Statement String Composition (Already Available)

Lambda's **functional statement syntax** is designed for dynamic string composition. A function body with semicolon-separated expressions automatically concatenates string results:

```lambda
// Using functional statements for string composition
fn lua_project(name: string, kind: string) => {
    "project \""; name; "\"\n";
    "    kind \""; kind; "\""
}

// Usage
lua_project("mylib", "StaticLib")
// Returns: 'project "mylib"\n    kind "StaticLib"'

// More complex example with conditionals
fn generate_filter(platform: string, settings: [string]) => {
    "filter \""; platform; "\"\n";
    for s in settings {
        "    "; s; "\n"
    }
}
```

This is **more powerful than template strings** because it integrates seamlessly with Lambda's control flow (conditionals, loops, etc.) and maintains the functional paradigm.

### Priority 4: Path Manipulation Functions (Medium Value)

```lambda
path_basename("dir/file.txt")  // "file.txt"
path_dirname("dir/file.txt")   // "dir"
path_ext("file.txt")           // ".txt"
path_join("dir", "file")       // "dir/file"
```

Note: Directory listing is already available via `input("./dir")`.

### Priority 5: Record Update Syntax (Nice to Have)

```lambda
// Current
{...state, content: state.content ++ new_lines}

// Proposed shorthand
state with {content: state.content ++ new_lines}
```

---

## Feasibility Assessment

| Aspect | Current Status | With Improvements |
|--------|----------------|-------------------|
| Config parsing | ✅ Excellent | ✅ Excellent |
| Platform detection | ✅ Via `sys://` | ✅ Excellent |
| Directory listing | ✅ Via `input()` | ✅ Excellent |
| String generation | ✅ Functional statements | ✅ Excellent |
| State management | ⚠️ Manual | ⚠️ Acceptable |
| File output | ✅ Available | ✅ Available |
| CLI args | ❌ Blocked | ✅ Feasible |
| Env vars | ❌ Blocked | ✅ Feasible |

### Verdict

**Conversion is feasible today** for a simplified version that:
- Reads config from JSON file
- Auto-detects platform via `sys://system/info`
- Generates Lua output to a file
- Uses functional statements for clean string composition

**Full parity with Python** requires:
1. `env()` function for environment variables
2. `args()` function for CLI argument parsing

---

## Implementation Roadmap

### Phase 1: Minimal Viable Conversion (Today)
- Convert core generation logic
- Use `sys://system/info` for platform detection
- Hardcode config path (no CLI args)
- Output to platform-specific filenames

### Phase 2: Add System Functions
1. Implement `env(name)` and `env(name, default)`
2. Implement `args()` and `args(n)`
3. Update documentation

### Phase 3: Full Feature Parity
1. Add `file_exists()` function
2. Add path manipulation functions

---

## Conclusion

Converting `generate_premake.py` to Lambda Script is **largely feasible** with Lambda's current features, especially with the `sys://system/info` input providing platform detection. The main challenges are:

1. **No classes** → Use state maps (acceptable workaround)
2. **String composition** → Functional statements provide elegant solution
3. **No CLI args** → Needs language extension
4. **No env vars** → Needs language extension

The conversion would demonstrate Lambda's viability as a **general-purpose scripting language** for build tooling, not just data processing. Adding `env()` and `args()` functions would unlock a significant new use case category.
