# Lambda System Information Reference

> **Version**: Draft 0.1
> **Status**: Proposal
> **Last Updated**: February 2026

## Overview

The `sys` scheme provides access to system information, environment, and runtime paths in Lambda. Unlike `file` and `http` schemes which access external resources, `sys` provides introspection into the Lambda runtime environment and the underlying operating system.

### Design Principles

1. **Unified namespace**: All system info under `sys.*` (unlike other languages that scatter across modules)
2. **Linux `/proc` inspiration**: Process info modeled after `/proc` filesystem
3. **Lazy evaluation**: Values fetched on demand, not upfront
4. **Cross-platform**: Consistent API across macOS, Linux, and Windows

### Quick Reference

| Path | Description |
|------|-------------|
| `sys.os.*` | Operating system identity |
| `sys.proc.*` | Process information (like `/proc`) |
| `sys.cpu.*` | CPU information |
| `sys.memory.*` | Memory statistics |
| `sys.time.*` | Time and timezone |
| `sys.locale.*` | Locale settings |
| `sys.lambda.*` | Lambda runtime info |
| `sys.home` | User's home directory |
| `sys.temp` | System temp directory |

---

## Table of Contents

1. [System Information: `sys`](#1-system-information-sys)
2. [Process Information: `sys.proc`](#2-process-information-sysproc)
3. [Resource Information](#3-resource-information)
4. [Lambda Runtime Information](#4-lambda-runtime-information)
5. [Time and Locale](#5-time-and-locale)
6. [Caching and Refresh](#6-caching-and-refresh)
7. [Platform Differences](#7-platform-differences)
8. [Comparison with Other Languages](#8-comparison-with-other-languages)
9. [Comparison with OS APIs](#9-comparison-with-os-apis)
10. [Future Extensions (KIV)](#10-future-extensions-kiv)

---

## 1. System Information: `sys`

The root `sys` path returns comprehensive system information:

```lambda
let info = sys;

// Access OS information
info.os.name          // "Darwin", "Linux", "Windows"
info.os.version       // "23.2.0" (kernel version)
info.os.kernel        // Full kernel release string
info.os.platform      // "darwin", "linux", "windows"
info.os.hostname      // Machine hostname

// Timestamp when info was captured
info.timestamp        // Unix timestamp
```

**Example usage:**

```lambda
print("Running on: " + sys.os.name + " (" + sys.os.platform + ")");
print("Hostname: " + sys.os.hostname);
print("Uptime: " + sys.proc.uptime.days + " days, " + sys.proc.uptime.hours + " hours");
```

---

## 2. Process Information: `sys.proc`

Process-related information is grouped under `sys.proc`, modeled after Linux's `/proc` filesystem:

| Path | Description | Example |
|------|-------------|---------|
| `sys.proc.self.pid` | Current process ID | `12345` |
| `sys.proc.self.ppid` | Parent process ID | `12340` |
| `sys.proc.self.uid` | User ID | `501` |
| `sys.proc.self.gid` | Group ID | `20` |
| `sys.proc.self.argv` | Command-line arguments | `["lambda", "run", "script.ls"]` |
| `sys.proc.self.cwd` | Current working directory | `/projects/myapp` |
| `sys.proc.self.env` | Environment variables (map) | `{ PATH: "...", HOME: "..." }` |
| `sys.proc.self.env.VAR` | Specific environment variable | `sys.proc.self.env.PATH` |
| `sys.proc.uptime` | System uptime information | `{ seconds, days, hours, minutes }` |
| `sys.proc.[pid]` | Process info by PID | `sys.proc.1234` |
| `sys.process` | Alias for `sys.proc.self` | (convenience shortcut) |
| `sys.home` | User's home directory | `/Users/alice` |
| `sys.temp` | System temp directory | `/tmp` or `/var/folders/...` |

### 2.1 Process Identification

Current process info is accessed via `sys.proc.self` (like `/proc/self` in Linux):

```lambda
print("Process ID: " + sys.proc.self.pid);
print("Parent PID: " + sys.proc.self.ppid);
print("User ID: " + sys.proc.self.uid);
print("Group ID: " + sys.proc.self.gid);
print("Arguments: " + sys.proc.self.argv);
```

### 2.2 System Uptime

```lambda
let uptime = sys.proc.uptime;
print("Uptime: " + uptime.days + " days, " + uptime.hours + " hours");
print("Total seconds: " + uptime.seconds);
```

### 2.3 Process Listing by PID

Access information about any process by PID (like `/proc/[pid]` in Linux):

```lambda
// Get info for a specific process
let proc = sys.proc.1234;
print("Process 1234: " + proc.name);
print("State: " + proc.state);       // "running", "sleeping", etc.
print("Memory: " + proc.memory);     // Memory usage in bytes
print("Command: " + proc.cmdline);   // Full command line
print("Open files: " + proc.fd);     // List of file descriptors

// List all processes (returns list of PIDs)
let pids = sys.proc.*;               // All process IDs
for pid in pids {
    let p = sys.proc[pid];
    print(pid + ": " + p.name);
}
```

**Process properties available via `sys.proc.[pid]`:**

| Property | Description | Example |
|----------|-------------|--------|
| `name` | Process name | `"lambda"` |
| `state` | Process state | `"running"`, `"sleeping"`, `"zombie"` |
| `memory` | Memory usage (bytes) | `12345678` |
| `cmdline` | Full command line | `"/usr/bin/lambda run script.ls"` |
| `fd` | Open file descriptors | `[0, 1, 2, 5, 7]` |
| `ppid` | Parent process ID | `1234` |
| `uid` | User ID | `501` |
| `gid` | Group ID | `20` |

> **Alias**: `sys.process` is an alias for `sys.proc.self` for convenience:
> ```lambda
> sys.process.pid      // Same as sys.proc.self.pid
> sys.process.env.PATH // Same as sys.proc.self.env.PATH
> ```

### 2.4 Current Working Directory

```lambda
// Current working directory as a file path
let cwd = sys.proc.self.cwd;      // file.projects.myapp
let src = cwd ++ src;             // file.projects.myapp.src

// Use in path operations
let config = sys.proc.self.cwd ++ 'config.json';
```

### 2.5 Environment Variables

```lambda
// Access all environment variables as a map
let env = sys.proc.self.env;
for name in env {
    print(name + "=" + env[name]);
}

// Access specific variable
let path = sys.proc.self.env.PATH;   // "/usr/bin:/bin:..."
let home = sys.proc.self.env.HOME;   // "/Users/alice"
let user = sys.proc.self.env.USER;   // "alice"

// Check if variable exists
if sys.proc.self.env.DEBUG {
    enable_debug_mode();
}

// With fallback
let port = sys.proc.self.env.PORT ?? "8080";
```

### 2.6 Directory Paths

```lambda
// Home directory - returns a file path
let home = sys.home;              // file.Users.alice (as path)
let config = home ++ '.config';   // file.Users.alice.'.config'

// Temp directory
let temp = sys.temp;              // file.tmp or platform-specific
let tmp_file = temp ++ 'cache.json';
```

---

## 3. Resource Information

| Path | Description | Example |
|------|-------------|---------|
| `sys.memory.total` | Total system memory (bytes) | `17179869184` |
| `sys.memory.free` | Free memory (bytes) | `4294967296` |
| `sys.memory.used` | Used memory (bytes) | `12884901888` |
| `sys.cpu.architecture` | CPU architecture | `"arm64"`, `"x86_64"` |
| `sys.cpu.count` | Number of CPU cores | `8` |
| `sys.cpu.model` | CPU model name | `"Apple M1 Pro"` |

```lambda
let mem = sys.memory;
let used_pct = (mem.used / mem.total) * 100;
print("Memory usage: " + used_pct + "%");
print("CPU: " + sys.cpu.model + " (" + sys.cpu.architecture + ")");
print("CPU cores: " + sys.cpu.count);
```

---

## 4. Lambda Runtime Information

| Path | Description | Example |
|------|-------------|---------|
| `sys.lambda.version` | Lambda version | `"0.9.0"` |
| `sys.lambda.build` | Build identifier | `"2026.02.04"` |
| `sys.lambda.features` | Enabled features | `["jit", "mir", "radiant"]` |

```lambda
print("Lambda version: " + sys.lambda.version);
if "jit" in sys.lambda.features {
    print("JIT compilation enabled");
}
```

---

## 5. Time and Locale

| Path | Description | Example |
|------|-------------|---------|
| `sys.time.now` | Current datetime | `2026-02-04T15:30:00Z` |
| `sys.time.zone` | Timezone name | `"America/Los_Angeles"` |
| `sys.time.offset` | UTC offset (seconds) | `-28800` |
| `sys.locale.lang` | Language code | `"en"` |
| `sys.locale.country` | Country code | `"US"` |

```lambda
let now = sys.time.now;
print("Current time: " + now);
print("Timezone: " + sys.time.zone);
```

---

## 6. Caching and Refresh

System information may be cached for performance. The current implementation uses a 5-second TTL (time-to-live) for cached results.

```lambda
// First access fetches fresh data
let info1 = sys;

// Within 5 seconds, returns cached data
let info2 = sys;  // Same as info1

// Force refresh (proposed)
let fresh = sys!;  // ! suffix forces refresh
```

---

## 7. Platform Differences

| Feature | macOS | Linux | Windows |
|---------|-------|-------|---------|
| `sys.home` | `/Users/name` | `/home/name` | `C:\Users\name` |
| `sys.temp` | `/var/folders/...` | `/tmp` | `C:\Users\name\AppData\Local\Temp` |
| Uptime | ✅ via sysctl | ✅ via /proc | ✅ via GetTickCount |
| Memory info | ✅ via mach | ✅ via /proc/meminfo | ✅ via GlobalMemoryStatus |

---

## 8. Comparison with Other Languages

| Feature | Lambda | Node.js | Python | Go | Rust |
|---------|--------|---------|--------|----|------|
| Platform | `sys.os.platform` | `os.platform()` | `sys.platform` | `runtime.GOOS` | `std::env::consts::OS` |
| Hostname | `sys.os.hostname` | `os.hostname()` | `socket.gethostname()` | `os.Hostname()` | `gethostname` crate |
| PID | `sys.proc.self.pid` | `process.pid` | `os.getpid()` | `os.Getpid()` | `std::process::id()` |
| Env vars | `sys.proc.self.env` | `process.env` | `os.environ` | `os.Environ()` | `std::env::vars()` |
| CWD | `sys.proc.self.cwd` | `process.cwd()` | `os.getcwd()` | `os.Getwd()` | `std::env::current_dir()` |
| CPU arch | `sys.cpu.architecture` | `os.arch()` | `platform.machine()` | `runtime.GOARCH` | `std::env::consts::ARCH` |
| Memory | `sys.memory.*` | `os.freemem()` | `psutil.virtual_memory()` | `runtime.MemStats` | `sysinfo` crate |
| Home dir | `sys.home` | `os.homedir()` | `Path.home()` | `os.UserHomeDir()` | `dirs::home_dir()` |
| Uptime | `sys.proc.uptime` | `os.uptime()` | `psutil.boot_time()` | N/A | `sysinfo` crate |
| Process list | `sys.proc.[pid]` | N/A (child_process) | `psutil.Process(pid)` | N/A | `sysinfo` crate |

**Key insight**: Most languages scatter system info across `os`, `sys`, `process`, `runtime` modules. Lambda unifies everything under `sys.*`.

---

## 9. Comparison with OS APIs

| Lambda Path | Linux `/proc` | macOS API | Windows API |
|-------------|---------------|-----------|-------------|
| `sys.proc.self.pid` | `/proc/self/stat` | `getpid()` | `GetCurrentProcessId()` |
| `sys.proc.[pid]` | `/proc/[pid]/*` | No direct equivalent | `OpenProcess()` + queries |
| `sys.proc.[pid].cmdline` | `/proc/[pid]/cmdline` | `proc_pidpath()` | `QueryFullProcessImageName()` |
| `sys.proc.[pid].fd` | `/proc/[pid]/fd/` | `proc_pidinfo()` | `NtQuerySystemInformation()` |
| `sys.proc.uptime` | `/proc/uptime` | `sysctl(KERN_BOOTTIME)` | `GetTickCount64()` |
| `sys.os.hostname` | `/proc/sys/kernel/hostname` | `gethostname()` | `GetComputerName()` |
| `sys.cpu.architecture` | `/proc/cpuinfo` | `uname().machine` | `SYSTEM_INFO.wProcessorArchitecture` |
| `sys.memory.*` | `/proc/meminfo` | `mach_host_statistics()` | `GlobalMemoryStatusEx()` |
| `sys.proc.self.env` | `/proc/self/environ` | `environ` global | `GetEnvironmentStrings()` |

---

## 10. Future Extensions (KIV)

The following paths are reserved for future implementation:

| Path | Description | Inspired by |
|------|-------------|-------------|
| `sys.proc.loadavg` | System load averages | `/proc/loadavg` |
| `sys.proc.mounts` | Mounted filesystems | `/proc/mounts` |
| `sys.proc.net.*` | Network statistics | `/proc/net/*` |
| `sys.fs.*` | Filesystem information | `df`, `statvfs()` |
| `sys.disk.*` | Disk I/O statistics | `/proc/diskstats` |
| `sys.net.interfaces` | Network interfaces | `getifaddrs()` |
| `sys.net.connections` | Active connections | `netstat` |

> **KIV** = Keep In View. These will be defined in future versions based on use cases.

---

## See Also

- [Lambda Path Mapping](Lambda_Path.md) - URL and file path mapping to Lambda paths
- [Lambda File System](Lambda_File%20(idea).md) - File system operations proposal
- [Lambda Reference](../doc/Lambda_Reference.md) - Core language reference
