# Lambda System Information Implementation Proposal

> **Version**: 1.0
> **Status**: Proposal
> **Date**: February 2026

## Overview

This document outlines the implementation plan for `sys.*` path support in Lambda, providing cross-platform access to system information through Lambda's lazy path resolution mechanism.

### Design Goals

1. **Native Path Integration**: Use `sys.*` as a first-class Lambda path scheme (like `/`, `.`, `http`)
2. **Lazy Resolution**: System info is only fetched when accessed, following Lambda's path semantics
3. **Refactor Existing Code**: Reuse `input_sysinfo.cpp` platform-specific code
4. **Phase Out `sys://` URL Scheme**: All access through `sys.*` path only

---

## 1. Architecture

### 1.1 Key Insight: Path vs URL

The current `input_sysinfo.cpp` uses URL-based access (`sys://system/info`). This proposal shifts to **path-based access**:

| Old (URL) | New (Path) | Notes |
|-----------|------------|-------|
| `sys://system/info` | `sys` | Root returns full system info |
| `sys://hardware/cpu` | `sys.cpu` | CPU information |
| `sys://perf/memory` | `sys.memory` | Memory statistics |
| `sys://process/list` | `sys.proc.*` | Process list |

### 1.2 Cascading Lazy Resolution

The key mechanism is **cascading lazy resolution**: each level returns a Map containing either **actual values** (lightweight) or **unresolved Paths** (heavyweight). Resolution only happens when a value is actually accessed.

#### Resolution Cascade Example

```
sys                          // Path (unresolved)
  │
  ▼ [access sys]
Map {                        // Resolved to Map with Path values
  os:     sys.os,            // Path (unresolved)
  cpu:    sys.cpu,           // Path (unresolved)
  memory: sys.memory,        // Path (unresolved)
  proc:   sys.proc,          // Path (unresolved)
  home:   sys.home,          // Path (unresolved)
  temp:   sys.temp,          // Path (unresolved)
  lambda: sys.lambda,        // Path (unresolved)
  time:   sys.time           // Path (unresolved)
}
  │
  ▼ [access sys.os]
Map {                        // sys.os resolved to Map
  name:     "Darwin",        // String (immediate value - lightweight)
  version:  "23.2.0",        // String (immediate value)
  kernel:   "Darwin Kernel Version 23.2.0", // String
  platform: "darwin",        // String
  hostname: "macbook.local"  // String
}
  │
  ▼ [access sys.proc]
Map {                        // sys.proc resolved to Map with mixed values
  self:    sys.proc.self,    // Path (unresolved - heavy)
  uptime:  sys.proc.uptime,  // Path (unresolved - dynamic)
  1234:    sys.proc.1234     // Path (unresolved - process by PID)
}
  │
  ▼ [access sys.proc.self]
Map {                        // sys.proc.self resolved
  pid:  12345,               // Int (immediate)
  ppid: 1,                   // Int (immediate)
  uid:  501,                 // Int (immediate)
  gid:  20,                  // Int (immediate)
  cwd:  sys.proc.self.cwd,   // Path (unresolved - returns file Path)
  env:  sys.proc.self.env,   // Path (unresolved - heavy: all env vars)
  argv: ["lambda", "run"]    // List (immediate - small)
}
  │
  ▼ [access sys.proc.self.env]
Map {                        // Environment variables resolved
  PATH:  "/usr/bin:/bin",    // String (immediate)
  HOME:  "/Users/alice",     // String (immediate)
  USER:  "alice",            // String (immediate)
  ...                        // All env vars loaded
}
```

#### Resolution Rules

| Value Type | When to Use | Example |
|------------|-------------|---------|
| **Immediate value** | Lightweight, static | `sys.os.name` → `"Darwin"` |
| **Unresolved Path** | Heavyweight or dynamic | `sys.proc.self` → Path |
| **File Path** | Returns actual file path | `sys.home` → `/Users/alice` as Path |

#### Visual Flow

```
sys.proc.self.env.PATH
 │    │    │    │   │
 │    │    │    │   └─ Map lookup: env["PATH"] → "..."
 │    │    │    │
 │    │    │    └─ Resolve sys.proc.self.env → Map of env vars
 │    │    │
 │    │    └─ Resolve sys.proc.self → Map { env: Path, ... }
 │    │
 │    └─ Resolve sys.proc → Map { self: Path, ... }
 │
 └─ Resolve sys → Map { proc: Path, ... }
```

### 1.3 Data Model

System info is exposed as a **virtual Map** with lazy-loaded properties. Values are either immediate (lightweight) or Paths (deferred):

```lambda
// Level 0: sys resolves to Map of Paths
sys                    // → Map { os: Path, cpu: Path, memory: Path, ... }

// Level 1: category resolves to Map (mixed values and Paths)
sys.os                 // → Map { name: "Darwin", version: "23.2.0", ... }
sys.cpu                // → Map { model: "Apple M2", cores: 8, ... }
sys.memory             // → Map { total: 17179869184, free: 8589934592, ... }
sys.proc               // → Map { self: Path, uptime: Path, ... }

// Level 2+: deeper access
sys.os.name            // → "Darwin" (immediate from resolved Map)
sys.proc.self          // → Map { pid: 123, env: Path, ... }
sys.proc.self.pid      // → 123 (immediate)
sys.proc.self.env      // → Map { PATH: "...", HOME: "...", ... }
sys.proc.self.env.PATH // → "/usr/bin:/bin:..."

// Special: returns actual file Path (not string)
sys.home               // → Path /Users/alice (can use with ++)
sys.temp               // → Path /tmp
```

---

## 2. Implementation Strategy

### 2.1 Phase 1: Core Infrastructure

**Goal**: Establish `sys` path resolution mechanism.

**Files to Modify**:

| File | Changes |
|------|---------|
| `lambda/path.c` | Add `resolve_sys_path()` function |
| `lambda/lambda.h` | Add sys-specific structures |
| `lambda/input/input_sysinfo.cpp` | Refactor to `sysinfo.cpp`, export reusable functions |

**New File**: `lambda/sysinfo.cpp` (refactored from `input_sysinfo.cpp`)

```c
// sysinfo.cpp - System information provider for sys.* paths

// Reusable functions from existing input_sysinfo.cpp:
// - get_system_uptime()      → double (seconds)
// - get_os_version()         → const char*
// - get_hostname()           → const char*
// - get_cpu_info()           → NEW: returns Map
// - get_memory_info()        → NEW: returns Map
// - get_process_info(pid)    → NEW: returns Map
// - get_env_vars()           → NEW: returns Map

// Main resolution entry point
Item resolve_sys_path(Path* path);
```

**Path Resolution with Lazy Loading** (in `sysinfo.cpp`):

```c
// Helper: create a sys sub-path
static Item sys_subpath(const char* segment) {
    Path* sys_root = path_get_root(PATH_SCHEME_SYS);
    Path* subpath = path_append(sys_root, segment);
    return item_path(subpath);  // Returns unresolved Path
}

// Level 0: sys → Map of unresolved Paths
Item sysinfo_resolve_root(void) {
    Map* root = map_new(pool);
    
    // All values are Paths - not resolved until accessed
    map_set(root, "os",     sys_subpath("os"));
    map_set(root, "cpu",    sys_subpath("cpu"));
    map_set(root, "memory", sys_subpath("memory"));
    map_set(root, "proc",   sys_subpath("proc"));
    map_set(root, "home",   sys_subpath("home"));
    map_set(root, "temp",   sys_subpath("temp"));
    map_set(root, "lambda", sys_subpath("lambda"));
    map_set(root, "time",   sys_subpath("time"));
    map_set(root, "locale", sys_subpath("locale"));
    
    return item_map(root);
}

// Main resolution dispatcher - called when a sys.* path is accessed
Item resolve_sys_path(Path* path) {
    // Collect path segments: sys.cpu.model → ["cpu", "model"]
    const char* segments[8];
    int depth = collect_sys_segments(path, segments, 8);
    
    if (depth == 0) {
        // sys alone → return Map of unresolved sub-paths
        return sysinfo_resolve_root();
    }
    
    // Dispatch based on first segment
    const char* category = segments[0];
    
    if (strcmp(category, "os") == 0) {
        return sysinfo_resolve_os(segments + 1, depth - 1);
    }
    if (strcmp(category, "cpu") == 0) {
        return sysinfo_resolve_cpu(segments + 1, depth - 1);
    }
    if (strcmp(category, "memory") == 0) {
        return sysinfo_resolve_memory(segments + 1, depth - 1);
    }
    if (strcmp(category, "proc") == 0) {
        return sysinfo_resolve_proc(segments + 1, depth - 1);
    }
    if (strcmp(category, "home") == 0) {
        return sysinfo_resolve_home();  // Returns file Path
    }
    if (strcmp(category, "temp") == 0) {
        return sysinfo_resolve_temp();  // Returns file Path
    }
    // ... other categories
    
    return ItemNull;  // Unknown sys path
}
```

### 2.2 Phase 2: OS Information (`sys.os.*`)

**Reuse from `input_sysinfo.cpp`**:
- `uname()` call (Unix) / `GetSystemInfo()` (Windows)
- `get_os_version()` function
- `gethostname()` call

**Implementation** (lightweight - all immediate values):

```c
// sysinfo.cpp

// sys.os → all values are immediate (lightweight static data)
Item sysinfo_resolve_os(const char** segments, int depth) {
    if (depth == 0) {
        // Return full os Map with immediate values
        Map* os = map_new(pool);
        map_set(os, "name", sysinfo_os_name());
        map_set(os, "version", sysinfo_os_version());
        map_set(os, "kernel", sysinfo_os_kernel());
        map_set(os, "platform", sysinfo_os_platform());
        map_set(os, "hostname", sysinfo_os_hostname());
        return item_map(os);
    }
    
    // Access specific property
    const char* prop = segments[0];
    if (strcmp(prop, "name") == 0) return sysinfo_os_name();
    if (strcmp(prop, "version") == 0) return sysinfo_os_version();
    // ...
    
    return ItemNull;
}
```

### 2.3 Phase 3: CPU & Memory (`sys.cpu.*`, `sys.memory.*`)

**Platform APIs**:

| Info | macOS | Linux | Windows |
|------|-------|-------|---------|
| CPU model | `sysctl hw.model` | `/proc/cpuinfo` | `Win32_Processor` |
| CPU cores | `sysctl hw.ncpu` | `/proc/cpuinfo` | `SYSTEM_INFO` |
| CPU arch | `uname().machine` | `uname().machine` | `SYSTEM_INFO` |
| Memory total | `sysctl hw.memsize` | `/proc/meminfo` | `GlobalMemoryStatusEx` |
| Memory free | `vm_statistics64` | `/proc/meminfo` | `GlobalMemoryStatusEx` |

**Implementation Pattern**:

```c
Item sysinfo_get_cpu(const char** segments, int depth) {
    // Cache with 1-hour TTL (static info)
    static Item cached_cpu = ItemNull;
    static time_t cached_at = 0;
    
    time_t now = time(NULL);
    if (cached_cpu.item != 0 && (now - cached_at) < 3600) {
        if (depth == 0) return cached_cpu;
        return map_get_path((Map*)cached_cpu.item, segments, depth);
    }
    
    // Build CPU info
    Map* cpu = map_new(pool);
    
#ifdef __APPLE__
    char model[256];
    size_t size = sizeof(model);
    sysctlbyname("machdep.cpu.brand_string", model, &size, NULL, 0);
    map_set_str(cpu, "model", model);
    
    int ncpu;
    size = sizeof(ncpu);
    sysctlbyname("hw.ncpu", &ncpu, &size, NULL, 0);
    map_set_int(cpu, "cores", ncpu);
#elif defined(__linux__)
    // Parse /proc/cpuinfo
#elif defined(_WIN32)
    // Use GetSystemInfo / WMI
#endif
    
    cached_cpu = item_map(cpu);
    cached_at = now;
    
    if (depth == 0) return cached_cpu;
    return map_get_path((Map*)cached_cpu.item, segments, depth);
}
```

### 2.4 Phase 4: Process Information (`sys.proc.*`)

**Existing Code Reuse**:
- `getpid()`, `getppid()`, `getuid()`, `getgid()` (from `input_sysinfo.cpp`)
- Environment variable access

**New Features**:
- `sys.proc.self` - current process
- `sys.proc.[pid]` - process by PID
- `sys.proc.uptime` - system uptime

**Implementation with Mixed Immediate/Deferred Values**:

```c
// Helper: create sys.proc.* sub-path
static Item sys_proc_subpath(const char* segment) {
    Path* proc_path = path_append(path_get_root(PATH_SCHEME_SYS), "proc");
    Path* subpath = path_append(proc_path, segment);
    return item_path(subpath);
}

// sys.proc → Map with unresolved Paths (deferred loading)
Item sysinfo_resolve_proc(const char** segments, int depth) {
    if (depth == 0) {
        // Return proc Map with sub-paths (not resolved yet)
        Map* proc = map_new(pool);
        map_set(proc, "self",   sys_proc_subpath("self"));    // Path (deferred)
        map_set(proc, "uptime", sys_proc_subpath("uptime"));  // Path (deferred)
        // Note: individual PIDs accessed via sys.proc.1234
        return item_map(proc);
    }
    
    const char* sub = segments[0];
    
    if (strcmp(sub, "self") == 0) {
        return sysinfo_resolve_proc_self(segments + 1, depth - 1);
    }
    if (strcmp(sub, "uptime") == 0) {
        return sysinfo_resolve_uptime();
    }
    
    // Check if it's a PID number (sys.proc.1234)
    char* endptr;
    long pid = strtol(sub, &endptr, 10);
    if (*endptr == '\0' && pid > 0) {
        return sysinfo_resolve_proc_by_pid(pid, segments + 1, depth - 1);
    }
    
    return ItemNull;
}

// sys.proc.self → Map with MIXED immediate values and Paths
Item sysinfo_resolve_proc_self(const char** segments, int depth) {
    if (depth == 0) {
        Map* self = map_new(pool);
        
        // Immediate values (lightweight - always loaded)
        map_set_int(self, "pid", getpid());
        map_set_int(self, "ppid", getppid());
#ifndef _WIN32
        map_set_int(self, "uid", getuid());
        map_set_int(self, "gid", getgid());
#endif
        map_set(self, "argv", sysinfo_get_argv());  // Small list - immediate
        
        // Deferred values (heavyweight - Paths for lazy loading)
        Path* self_path = path_append(
            path_append(path_get_root(PATH_SCHEME_SYS), "proc"), "self");
        map_set(self, "env", item_path(path_append(self_path, "env")));  // Path
        map_set(self, "cwd", item_path(path_append(self_path, "cwd")));  // Path
        
        return item_map(self);
    }
    
    const char* prop = segments[0];
    
    // Direct property access - resolve immediately
    if (strcmp(prop, "pid") == 0) return item_int(getpid());
    if (strcmp(prop, "ppid") == 0) return item_int(getppid());
    if (strcmp(prop, "env") == 0) {
        return sysinfo_resolve_env(segments + 1, depth - 1);
    }
    if (strcmp(prop, "cwd") == 0) {
        return sysinfo_resolve_cwd();  // Returns file Path
    }
    // ...
    
    return ItemNull;
}

// sys.proc.self.env → Map of all environment variables (heavyweight)
Item sysinfo_resolve_env(const char** segments, int depth) {
    if (depth == 0) {
        // Load all environment variables into Map
        Map* env = map_new(pool);
        extern char** environ;
        for (char** e = environ; *e; e++) {
            char* eq = strchr(*e, '=');
            if (eq) {
                size_t name_len = eq - *e;
                char* name = pool_strndup(pool, *e, name_len);
                char* value = eq + 1;
                map_set_str(env, name, value);
            }
        }
        return item_map(env);
    }
    
    // sys.proc.self.env.PATH → direct lookup
    const char* var_name = segments[0];
    const char* value = getenv(var_name);
    return value ? item_string(value) : ItemNull;
}
```
```

### 2.5 Phase 5: Directory Paths (`sys.home`, `sys.temp`)

These return **actual Path types** that can be used for file operations:

```c
Item sysinfo_get_home(void) {
    // Get home directory path string
    const char* home = NULL;
    
#ifdef _WIN32
    home = getenv("USERPROFILE");
#else
    home = getenv("HOME");
#endif
    
    if (!home) return ItemNull;
    
    // Build Path from home directory string
    // e.g., "/Users/alice" → Path with scheme FILE
    return item_path(path_from_os_string(home));
}

Item sysinfo_get_temp(void) {
#ifdef __APPLE__
    // macOS: Use confstr or TMPDIR
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
#elif defined(_WIN32)
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
#else
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
#endif
    
    return item_path(path_from_os_string(tmp));
}
```

---

## 3. Lazy Loading Decision Rules

### 3.1 When to Use Immediate Values vs Paths

The decision of whether to store an immediate value or an unresolved Path in the Map depends on:

| Criterion | Immediate Value | Unresolved Path |
|-----------|-----------------|-----------------|
| **Data size** | Small (< 1KB) | Large (env vars, process lists) |
| **Fetch cost** | Cheap syscall | Expensive I/O or iteration |
| **Volatility** | Static or slow-changing | Dynamic, changes frequently |
| **Access pattern** | Usually accessed together | Often accessed individually |
| **Dependencies** | None | May need other data first |

### 3.2 Category-by-Category Breakdown

```
sys                     → Map of Paths (all deferred)
├── os                  → Map of immediates (all lightweight)
│   ├── name            → "Darwin" (immediate)
│   ├── version         → "23.2.0" (immediate)
│   ├── kernel          → "..." (immediate)
│   ├── platform        → "darwin" (immediate)
│   └── hostname        → "..." (immediate)
│
├── cpu                 → Map (mixed)
│   ├── model           → "Apple M2" (immediate - static)
│   ├── cores           → 8 (immediate - static)
│   ├── architecture    → "arm64" (immediate - static)
│   └── usage           → Path sys.cpu.usage (deferred - dynamic)
│
├── memory              → Map (all immediate - single syscall)
│   ├── total           → 17179869184 (immediate)
│   ├── free            → 8589934592 (immediate)
│   └── used            → 8589934592 (immediate)
│
├── proc                → Map of Paths (all deferred)
│   ├── self            → Path sys.proc.self
│   │   ├── pid         → 12345 (immediate)
│   │   ├── ppid        → 1 (immediate)
│   │   ├── argv        → ["lambda"] (immediate - small)
│   │   ├── env         → Path sys.proc.self.env (deferred - heavy)
│   │   └── cwd         → Path sys.proc.self.cwd (deferred)
│   └── uptime          → Path sys.proc.uptime
│
├── home                → Path /Users/alice (file Path - special)
├── temp                → Path /tmp (file Path - special)
│
├── time                → Map (all immediate - single syscall)
│   ├── now             → datetime (immediate)
│   ├── zone            → "America/Los_Angeles" (immediate)
│   └── offset          → -28800 (immediate)
│
└── lambda              → Map (all immediate - runtime constants)
    ├── version         → "0.9.0" (immediate)
    ├── build           → "2026.02.04" (immediate)
    └── features        → ["jit", "mir"] (immediate)
```

### 3.3 Path Resolution Trigger Points

A Path is resolved when:

1. **Value access**: `sys.proc.self.env` - accessing the `env` key triggers resolution
2. **Iteration**: `for (k in sys.proc.self.env)` - iterating over the Map
3. **Conversion**: `to_string(sys.proc.self.env)` - converting to string
4. **Comparison**: `sys.proc.self.env == other` - equality check

A Path is **NOT** resolved when:

1. **Assignment**: `let e = sys.proc.self.env` - just copies the Path reference
2. **Passing**: `f(sys.proc.self.env)` - passes the Path as argument
3. **Storage**: `map.env = sys.proc.self.env` - stores the Path in another Map

---

## 4. Caching Strategy

### 4.1 TTL-Based Caching

Different data has different volatility:

| Category | TTL | Rationale |
|----------|-----|-----------|
| `sys.os.*` | 1 hour | OS info doesn't change |
| `sys.cpu.model/cores` | 1 hour | Static hardware info |
| `sys.cpu.usage` | 1 second | Dynamic metric |
| `sys.memory.*` | 1 second | Dynamic metric |
| `sys.proc.self.*` | 5 seconds | Semi-static |
| `sys.proc.uptime` | 1 second | Dynamic |
| `sys.home`, `sys.temp` | 1 hour | Rarely changes |

### 4.2 Cache Structure

```c
typedef struct SysinfoCache {
    Item os_info;
    Item cpu_info;
    Item memory_info;
    Item proc_self;
    Item home_path;
    Item temp_path;
    time_t os_time;
    time_t cpu_time;
    time_t memory_time;
    time_t proc_time;
    time_t path_time;
} SysinfoCache;

// Thread-local cache
static __thread SysinfoCache* g_sysinfo_cache = NULL;
```

---

## 5. Integration Points

### 5.1 Path Resolution Hook

In `path.c`, add sys scheme handling:

```c
// In path_resolve_for_access() or similar
Item path_resolve(Path* path) {
    PathScheme scheme = path_get_scheme(path);
    
    switch (scheme) {
        case PATH_SCHEME_FILE:
        case PATH_SCHEME_REL:
        case PATH_SCHEME_PARENT:
            return resolve_file_path(path);
            
        case PATH_SCHEME_HTTP:
        case PATH_SCHEME_HTTPS:
            return resolve_http_path(path);
            
        case PATH_SCHEME_SYS:
            return resolve_sys_path(path);  // NEW
            
        default:
            return ItemNull;
    }
}
```

### 4.2 Property Access

When accessing `sys.cpu.model`, the path `sys.cpu` resolves to a Map, then `.model` accesses the map property. This works naturally with Lambda's existing member access semantics.

### 4.3 Deprecate `sys://` URL Scheme

1. Remove `input_from_sysinfo()` function
2. Remove `is_sys_url()` function  
3. Remove URL parsing code for sys scheme
4. Keep platform-specific data collection functions in `sysinfo.cpp`

---

## 5. Code Refactoring Plan

### 5.1 From `input_sysinfo.cpp`

**Keep (refactor to `sysinfo.cpp`)**:
- `get_system_uptime()` - works well, keep as-is
- `get_os_version()` - refactor to return Item
- Platform detection macros and includes
- `SysInfoManager` struct (simplify, remove URL-related fields)

**Remove**:
- `parse_sys_url()` - URL scheme deprecated
- `input_from_sysinfo()` - replaced by path resolution
- `is_sys_url()` - no longer needed
- `create_system_info_element()` - replace with Map-based structure

**Add**:
- `resolve_sys_path(Path*)` - main entry point
- `sysinfo_get_os/cpu/memory/proc()` - category handlers
- `sysinfo_get_env_map()` - environment as Map
- `sysinfo_get_home/temp()` - directory paths

### 5.2 New File Structure

```
lambda/
├── sysinfo.cpp          # NEW: System info provider (refactored)
├── sysinfo.h            # NEW: Header for sysinfo functions
├── path.c               # MODIFY: Add resolve_sys_path() call
├── lambda.h             # MODIFY: Add sysinfo declarations
└── input/
    └── input_sysinfo.cpp  # DEPRECATED: Remove after migration
```

---

## 6. Testing Plan

### 6.1 Unit Tests

```lambda
// test/lambda/sysinfo.ls

// Basic access
assert(sys.os.name != null)
assert(sys.os.platform in ["darwin", "linux", "windows"])

// CPU info
assert(sys.cpu.cores > 0)
assert(len(sys.cpu.model) > 0)

// Memory info
assert(sys.memory.total > 0)
assert(sys.memory.free >= 0)
assert(sys.memory.used > 0)

// Process info
assert(sys.proc.self.pid > 0)
assert(sys.proc.self.env.PATH != null)

// Directory paths
assert(exists(sys.home))
assert(exists(sys.temp))

// Path composition
let config = sys.home ++ '.config'
assert(type(config) == 'path')
```

### 6.2 Cross-Platform Tests

- Test on macOS, Linux, Windows
- Verify data accuracy against `uname`, `free`, `ps` commands
- Test caching behavior (repeated access within TTL)

---

## 7. Implementation Phases

| Phase | Scope | Duration | Deliverables |
|-------|-------|----------|--------------|
| 1 | Core infrastructure | 1 week | `resolve_sys_path()`, basic `sys.os.*` |
| 2 | CPU & Memory | 3 days | `sys.cpu.*`, `sys.memory.*` |
| 3 | Process info | 3 days | `sys.proc.*`, env vars |
| 4 | Directory paths | 2 days | `sys.home`, `sys.temp` |
| 5 | Cleanup | 2 days | Remove `sys://`, tests, docs |

---

## 8. Open Questions

1. **Refresh mechanism**: Should there be `sys!` syntax to force refresh? Or function `refresh(sys.cpu)`?
2. **Error handling**: Return `null` or `error` for permission denied on process info?
3. **Extended info**: When to implement `sys.net.*`, `sys.disk.*`, `sys.gpu.*`?
4. **Process listing**: Should `sys.proc.*` list all processes (security concern)?

---

## See Also

- [Lambda_Sysinfo.md](Lambda_Sysinfo.md) - API design and reference
- [Lambda_Path_Impl.md](Lambda_Path_Impl.md) - Path resolution implementation
- [input_sysinfo.cpp](../lambda/input/input_sysinfo.cpp) - Existing implementation to refactor
