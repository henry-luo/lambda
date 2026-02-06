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
| File output | `output(data, "file.lua")` or pipe `data |> "file.lua"` |
| Mutable state | `var` declarations in `pn` functions |
| Loops | `while`, `for` with `break`/`continue` |
| Null-safe access | `config.platforms.linux` (auto null-propagation) |
| File existence | `exists("path")` returns bool |

### ✅ System Information via `sys.*` Paths

Lambda provides **native system information access** through `sys.*` path literals with lazy evaluation and caching:

```lambda
// Platform detection
sys.os.platform              // "darwin", "linux", "windows"
sys.os.name                  // "Darwin", "Linux", "Windows"
sys.os.version               // Kernel version string
sys.os.machine               // "x86_64", "arm64", etc.

// CPU information
sys.cpu.cores                // Number of CPU cores
sys.cpu.threads              // Number of CPU threads

// Memory information
sys.memory.total             // Total memory in bytes
sys.memory.available         // Available memory in bytes

// Process information
sys.proc.self.pid            // Current process ID
sys.proc.self.cwd            // Current working directory
sys.proc.self.args           // Command line arguments as array
sys.proc.self.env            // Environment variables as map
sys.proc.self.env.HOME       // Specific environment variable
sys.proc.self.env.PATH       // Another environment variable

// Time and paths
sys.time.now                 // Current timestamp
sys.home                     // User home directory
sys.temp                     // System temp directory

// Lambda runtime info
sys.lambda.version           // Lambda version string
```

This addresses **all critical system access requirements**:
- ✅ Platform detection via `sys.os.platform`
- ✅ Environment variables via `sys.proc.self.env.*`
- ✅ Command-line arguments via `sys.proc.self.args`
- ✅ Current directory via `sys.proc.self.cwd`

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

### ⚠️ Design Differences (Not Limitations)

| Feature | Status | Lambda Approach |
|---------|--------|-----------------|
| String interpolation | No f-strings | Use functional statements or `+` concatenation |
| Classes/OOP | Not supported | Pass state maps through functions (functional style) |

### ✅ Previously Missing, Now Available

| Feature | Python | Lambda | Status |
|---------|--------|--------|--------|
| Environment variables | `os.environ["VAR"]` | `sys.proc.self.env.VAR` | ✅ Available |
| CLI arguments | `sys.argv` | `sys.proc.self.args` | ✅ Available |
| File existence | `os.path.exists()` | `exists(path)` | ✅ Available |
| Current directory | `os.getcwd()` | `sys.proc.self.cwd` | ✅ Available |

### ⚠️ Minor Missing Features

| Feature | Python | Lambda | Impact |
|---------|--------|--------|--------|
| Path manipulation | `os.path.basename()` | Use string functions | Low |
| Symlink creation | `os.symlink()` | `io.symlink()` in procedural | Low |

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
    let platform = sys.os.platform  // Direct path access
    
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
    let platform = sys.os.platform  // "darwin", "linux", or "windows"
    
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
    // Get config path from CLI args or use default
    let args = sys.proc.self.args
    let config_path = if (len(args) > 1) args[1] else "build_lambda_config.json"
    
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
    content |> output_file
    
    print("Generated: " + output_file)
}
```

---

## Recently Implemented Features

The following features were previously identified as blockers and have now been implemented:

### ✅ Environment Variables (Now Available)

```lambda
// Access via sys.proc.self.env
sys.proc.self.env.PATH         // Get PATH environment variable
sys.proc.self.env.HOME         // Get HOME directory
sys.proc.self.env.USER         // Get current user

// Access full environment map
let all_env = sys.proc.self.env
for k, v in all_env { ... }    // Iterate all variables
```

### ✅ Command-Line Arguments (Now Available)

```lambda
// Access via sys.proc.self.args
let args = sys.proc.self.args  // Returns array of strings
args[0]                        // Program name (e.g., "./lambda.exe")
args[1]                        // First argument
len(args)                      // Number of arguments

// Example usage
pn main() {
    let args = sys.proc.self.args
    if (len(args) < 2) {
        print("Usage: lambda run script.ls <config_file>")
        return
    }
    let config_file = args[1]
    // ...
}
```

### ✅ Platform Detection (Now Available)

```lambda
// Direct path access - no input() needed
sys.os.platform                // "darwin", "linux", "windows"
sys.os.name                    // "Darwin", "Linux", "Windows"  
sys.os.machine                 // "arm64", "x86_64", etc.

// Example platform-specific logic
let compiler = if (sys.os.platform == "windows") "msvc" else "clang"
```

### ✅ Functional Statement String Composition (Already Available)

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

---

## Remaining Nice-to-Have Improvements

### Priority 1: Path Manipulation Functions (Low Value)

```lambda
path_basename("dir/file.txt")  // "file.txt"
path_dirname("dir/file.txt")   // "dir"
path_ext("file.txt")           // ".txt"
path_join("dir", "file")       // "dir/file"
```

**Workaround**: Use string functions:
```lambda
fn basename(path: string) => {
    let parts = path.split("/")
    parts[len(parts) - 1]
}
```

Note: Directory listing is already available via `input("./dir")`.

### Priority 2: Record Update Syntax (Nice to Have)

```lambda
// Current
{...state, content: state.content ++ new_lines}

// Proposed shorthand
state with {content: state.content ++ new_lines}
```

---

## Feasibility Assessment

| Aspect                | Status      | Notes                         |
| --------------------- | ----------- | ----------------------------- |
| Config parsing        | ✅ Excellent | `input("file.json", 'json)`   |
| Platform detection    | ✅ Excellent | `sys.os.platform` native path |
| Environment variables | ✅ Excellent | `sys.proc.self.env.*`         |
| CLI arguments         | ✅ Excellent | `sys.proc.self.args` array    |
| Current directory     | ✅ Excellent | `sys.proc.self.cwd`           |
| Directory listing     | ✅ Excellent | `input("./dir")`              |
| String generation     | ✅ Excellent | Functional statements         |
| State management      | ✅ Good      | State maps through functions  |
| File output           | ✅ Excellent | pipe or `output()`            |
| File existence        | ✅ Excellent | `exists(path)`                |

### Verdict: **FULL CONVERSION IS NOW FEASIBLE**

All previously blocking features have been implemented:

1. ✅ **CLI arguments** → `sys.proc.self.args` returns array
2. ✅ **Environment variables** → `sys.proc.self.env.*` access
3. ✅ **Platform detection** → `sys.os.platform` native path
4. ✅ **File existence** → `exists()` function
5. ✅ **Current directory** → `sys.proc.self.cwd`

The conversion can proceed with **full feature parity** to the Python version.

---

## Implementation Roadmap

### Phase 1: Core Conversion (Ready Now)
- ✅ Platform detection via `sys.os.platform`
- ✅ CLI argument parsing via `sys.proc.self.args`
- ✅ Environment access via `sys.proc.self.env.*`
- ✅ JSON config loading via `input()`
- ✅ File output via `|>` pipe operator
- Convert core generation logic using state maps
- Implement Lua code generation helpers
- Test on all three platforms

### Phase 2: Full Feature Parity
- Convert all ~25 generator methods
- Handle all library types (static, dynamic, header-only)
- Platform-specific compiler flags and libraries
- Test suite generation
- Comprehensive testing against Python output

### Phase 3: Enhancements (Optional)
- Add path manipulation helper functions
- Performance optimization
- Error handling improvements
- Documentation generation

---

## Conclusion

Converting `generate_premake.py` to Lambda Script is now **fully feasible** with all blocking features implemented:

| Feature            | Python                   | Lambda                  | Status             |
| ------------------ | ------------------------ | ----------------------- | ------------------ |
| Platform detection | `platform.system()`      | `sys.os.platform`       | ✅ Native           |
| Environment vars   | `os.environ["VAR"]`      | `sys.proc.self.env.VAR` | ✅ Native           |
| CLI arguments      | `sys.argv`               | `sys.proc.self.args`    | ✅ Native           |
| JSON parsing       | `json.load()`            | `input(file, 'json)`    | ✅ Native           |
| File output        | `open().write()`         | `data > file`           | ✅ Native           |
| Classes            | `class PremakeGenerator` | State maps + functions  | ✅ Functional style |

### Key Advantages of Lambda Version

1. **Cleaner Syntax**: Functional composition and pipe operators
2. **Type Safety**: Optional type annotations catch errors early
3. **Self-Hosting**: Lambda can generate its own build files
4. **Single File**: No external dependencies (Python interpreter)
5. **Cross-Platform**: Same code runs on all platforms

### Next Steps

1. Create `utils/generate_premake.ls` with initial implementation
2. Test output against current Python-generated `premake5.lua`
3. Iterate until outputs match
4. Replace Python script in build workflow

The conversion demonstrates Lambda's viability as a **general-purpose scripting language** for build tooling and system automation, not just data processing.

---

## Implementation Notes: Workarounds & Known Issues

During the initial implementation of `utils/generate_premake.ls`, several transpiler bugs and limitations were discovered. These workarounds are documented here for future reference.

### Transpiler Bugs / Limitations

| Issue                  | Description                                                                                                                  | Workaround                                                                     |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| `input()` null in `pn` | `input()` returns null when called inside a `pn` function with JIT compilation                                               | Load config at top-level with `let config = input(...)` before `pn main()`     |
| `var = var ++ fn()`    | Concatenating a function call result directly causes parse errors                                                            | Use intermediate variable: `h = fn(); lua = lua ++ h`                          |
| `replace(s, old, "")`  | Replacing with empty string returns null                                                                                     | Use `slice(s, 0, len(s) - n)` to remove suffix                                 |
| Type coercion warnings | Multiple `if (x == null) "default" else x` expressions cause "incompatible types" warnings that can break `main()` detection | Minimize null checks; use direct field access when config is known to be valid |
| `var` inside blocks    | Cannot declare `var` inside `while` or `if` blocks                                                                           | Declare all `var` at top of `pn` function                                      |
| `if` blocks in `fn`    | Block-style `if (cond) { ... }` doesn't work in pure functions                                                               | Use expression-style: `if (cond) value else other`                             |

### Working Pattern: Hybrid Functional + Procedural

The successful pattern combines functional and procedural code:

```lambda
// 1. Top-level functional code for input
let config = input("config.json", "json")
let platform = sys.os.platform

// 2. Pure functions for string generation
fn gen_header(name: string) {
    "workspace " ++ lua_string(name) ++ "\n" ++
    "    configurations { \"Debug\", \"Release\" }\n"
}

// 3. Procedural main for file output
pn main() {
    var lua = "x"
    var h1 = "x"
    
    h1 = gen_header("Lambda")
    lua = h1
    
    lua |> "output.lua"
    return 0
}
```

### Code Style Requirements

1. **All `var` declarations at function top** - no inline declarations
2. **Intermediate variables for fn results** - `h = fn(); result = result ++ h`
3. **Expression-style if-else in fn** - `if (cond) a else b` not `if (cond) { a }`
4. **Use `slice()` not `replace()` for suffix removal**
5. **Minimize null checks** to avoid type coercion issues

---

## TODO: Remaining Implementation Tasks

The current `utils/generate_premake.ls` generates workspace headers and configurations. The following tasks remain for full feature parity with `generate_premake.py`:

### High Priority

- [ ] **Iterate `source_dirs`** - Generate file lists for each source directory
- [ ] **Generate `lambda-lib`** - Static library project from `lib/` sources
- [ ] **Generate `lambda-runtime`** - Static library from `lambda/` sources  
- [ ] **Generate main executable** - `lambda` project linking all libraries
- [ ] **Handle `includes`** - Generate `includedirs` from config

### Medium Priority

- [ ] **Platform-specific libraries** - Parse `platforms.macos/linux/windows` overrides
- [ ] **Library linking** - Generate `links` section with correct library order
- [ ] **Build options** - Generate `buildoptions` from config flags
- [ ] **Linker options** - Generate `linkoptions` for LTO, dead stripping, etc.

### Lower Priority

- [ ] **Test projects** - Generate test executables from `test_suites`
- [ ] **Dev libraries** - Handle `dev_libraries` for test builds
- [ ] **Library dependencies** - Track and order library dependencies correctly
- [ ] **Platform filters** - Generate `filter "system:macos"` blocks

### Enhancements

- [ ] **CLI argument parsing** - Support `--platform linux` override
- [ ] **Output path option** - Allow custom output filename
- [ ] **Validation** - Check config structure before generation
- [ ] **Diff mode** - Compare against existing premake5.lua

### Testing

- [ ] **Output comparison** - Diff against Python-generated premake5.lua
- [ ] **Build verification** - Ensure generated Lua produces working builds
- [ ] **Cross-platform** - Test on Linux and Windows
