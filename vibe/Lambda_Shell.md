# Lambda Shell Scripting Proposal

> **Version**: Draft 0.1
> **Status**: Proposal
> **Last Updated**: February 2026

## Overview

This proposal defines file system operations and shell-like capabilities for Lambda, building on existing features (paths, pipes, patterns, `sys.*` scheme).

---

## 1. Gap Analysis: Unix Shell vs Lambda

### File Operations

| Unix Command | Description | Lambda Status |
|--------------|-------------|---------------|
| `cat`, `less` | Read file | ❌ Need `read()` |
| `echo > file` | Write file | ❌ Need `write()` |
| `>>` | Append to file | ❌ Need `append()` |
| `cp` | Copy | ❌ Need `copy()` |
| `mv` | Move/rename | ❌ Need `move()` |
| `rm` | Delete | ❌ Need `delete()` |
| `mkdir -p` | Create directory | ❌ Need `mkdir()` |
| `rmdir` | Remove directory | ❌ Need `rmdir()` |
| `touch` | Create/update mtime | ❌ Need `touch()` |
| `ln -s` | Create symlink | ❌ Need `symlink()` |
| `chmod` | Change permissions | ❌ Need `chmod()` |
| `stat` | File metadata | ⚠️ Path properties proposed |

### Text Processing

| Unix Command | Description | Lambda Status |
|--------------|-------------|---------------|
| `grep` | Search pattern | ✅ `where ~ ~ /pattern/` |
| `sed s/a/b/` | Replace text | ❌ Need `replace()` |
| `awk` | Field processing | ✅ Pipes + map |
| `cut` | Extract columns | ⚠️ Need `split()` improvements |
| `sort` | Sort lines | ✅ `sort()` exists |
| `uniq` | Remove duplicates | ✅ `unique()` exists |
| `wc -l/-w/-c` | Count lines/words/chars | ❌ Need count functions |
| `head`/`tail` | First/last N lines | ✅ `take()`, `drop()` |
| `tr` | Translate characters | ❌ Need `translate()` |
| `diff` | Compare files | ❌ Need `diff()` |

### Utilities

| Unix Command | Description | Lambda Status |
|--------------|-------------|---------------|
| `date +FMT` | Format datetime | ⚠️ Have `datetime`, need format options |
| `uuidgen` | Generate UUID | ❌ Need `uuid()` |
| `md5sum`, `sha256sum` | Checksums | ❌ Need `checksum()` |
| `base64` | Encode/decode | ❌ Need `base64_encode/decode()` |
| `gzip`, `tar` | Compression/archive | ❌ Need archive functions |
| `basename`, `dirname` | Path components | ⚠️ Need `path.stem`, `path.parent` |

### Process & Environment

| Unix Feature | Description | Lambda Status |
|--------------|-------------|---------------|
| `$VAR` | Read env var | ✅ `sys.proc.self.env.VAR` |
| `export VAR=` | Set env var | ❌ Need `set_env()` (procedural) |
| `cd` | Change directory | ❌ Need `chdir()` (procedural) |
| `pwd` | Current directory | ✅ `sys.proc.self.cwd` |
| `sleep` | Delay | ❌ Need `sleep()` (procedural) |
| `cmd &` | Background process | ❌ Need `spawn()` (procedural) |
| `wait` | Wait for process | ❌ Need `wait()` (procedural) |
| `kill` | Send signal | ❌ Need `kill()` (procedural) |
| `trap` | Signal handler | ❌ Future consideration |

### Network

| Unix Command | Description | Lambda Status |
|--------------|-------------|---------------|
| `curl`, `wget` | HTTP request | ✅ `fetch()` exists |
| `ping` | Network test | ❌ Not planned |
| `ssh`, `scp` | Remote operations | ❌ Not planned |

---

## 2. Path Metadata Properties

Lazy-loaded metadata via `stat()`:

| Property | Type | Description |
|----------|------|-------------|
| `path.name` | string | Filename (last segment) |
| `path.is_file` | bool | Is regular file |
| `path.is_dir` | bool | Is directory |
| `path.is_link` | bool | Is symbolic link |
| `path.size` | int | Size in bytes |
| `path.modified` | datetime | Modification time |
| `path.parent` | path | Parent directory |
| `path.stem` | string | Name without extension |
| `path.ext` | string | File extension |
| `path.mode` | string | Permission mode (e.g., "755") |

```lambda
// Filter by metadata
/downloads.* where ~.is_file and ~.size > 1_000_000

// Sort by modified time
/logs.* | sort(~, by: .modified, desc: true)
```

---

## 3. Proposed Functions

### File I/O

| Function | Returns | Description |
|----------|---------|-------------|
| `read(path)` | string | Read entire file as string |
| `read_lines(path)` | list | Read file as list of lines |
| `read_bytes(path)` | binary | Read file as binary data |
| `write(path, content)` | null | Write string to file (overwrite) |
| `write_lines(path, lines)` | null | Write list as lines |
| `append(path, content)` | null | Append to file |

```lambda
let config = input(read(/config.json), 'json');
write(/out.json, format(data, 'json'));
```

### File Operations

| Function | Description |
|----------|-------------|
| `copy(src, dst)` | Copy file or directory |
| `move(src, dst)` | Move/rename file or directory |
| `delete(path)` | Delete file or directory |
| `mkdir(path)` | Create directory (recursive) |
| `rmdir(path)` | Remove empty directory |
| `touch(path)` | Create file or update mtime |
| `symlink(target, link)` | Create symbolic link |
| `chmod(path, mode)` | Change permissions (e.g., "755") |

### Text Processing

| Function | Description |
|----------|-------------|
| `replace(str, pattern, repl)` | Replace first match |
| `replace_all(str, pattern, repl)` | Replace all matches |
| `translate(str, from, to)` | Character translation (like `tr`) |
| `lines(str)` | Split string into lines |
| `unlines(list)` | Join lines with newline |
| `words(str)` | Split string into words |
| `line_count(str)` | Count lines |
| `word_count(str)` | Count words |
| `char_count(str)` | Count characters |

```lambda
// sed-like replacement
replace_all(text, /foo/, "bar")

// tr-like translation
translate(text, "abc", "xyz")  // a→x, b→y, c→z
```

### Utilities

| Function | Returns | Description |
|----------|---------|-------------|
| `uuid()` | string | Generate UUID v4 |
| `checksum(path, algo)` | string | File hash: 'md5', 'sha256', 'sha512' |
| `base64_encode(data)` | string | Encode binary/string to base64 |
| `base64_decode(str)` | binary | Decode base64 to binary |
| `diff(path1, path2)` | list | Compare files, return differences |
| `glob(pattern)` | list | Expand glob pattern to paths |

```lambda
let hash = checksum(/file.bin, 'sha256');
let id = uuid();
```

### Archive Operations

| Function | Description |
|----------|-------------|
| `tar(paths, output)` | Create tar archive |
| `untar(archive, dest)` | Extract tar archive |
| `gzip(path)` | Compress file with gzip |
| `gunzip(path)` | Decompress gzip file |
| `zip(paths, output)` | Create zip archive |
| `unzip(archive, dest)` | Extract zip archive |

### Procedural Functions (for `pn` blocks)

| Function | Description |
|----------|-------------|
| `sleep(seconds)` | Pause execution |
| `chdir(path)` | Change working directory |
| `set_env(name, value)` | Set environment variable |
| `spawn(cmd, args)` | Start background process, returns pid |
| `wait(pid)` | Wait for process to complete |
| `kill(pid, signal?)` | Send signal to process (default: TERM) |

```lambda
pn deploy() {
    chdir(/project);
    let pid = spawn("npm", ["run", "build"]);
    wait(pid);
    copy(/project.dist, /var.www.app);
}
```

---

## 4. Practical Examples

### Find Large Files

```lambda
/home.user.**.* where ~.is_file and ~.size > 100_000_000
    | sort(~, by: .size, desc: true)
```

### Clean Old Logs

```lambda
let cutoff = now() - days(30);
for f in /var.log.**.log where ~.modified < cutoff {
    delete(f)
}
```

### Batch Rename

```lambda
for f in /photos.*.jpeg {
    move(f, f.parent ++ (f.stem + ".jpg"))
}
```

### grep-like Search

```lambda
for f in /project.**.js where read(~) ~ /TODO/ {
    print("=== " + f + " ===");
    for i, line in read_lines(f) where line ~ /TODO/ {
        print(i + ": " + line)
    }
}
```

### sed-like Replace

```lambda
for f in /src.**.js {
    let content = read(f);
    let updated = replace_all(content, /console\.log\(.*?\);/, "");
    write(f, updated);
}
```

### Backup with Checksum

```lambda
pn backup(src, dest) {
    copy(src, dest);
    let h1 = checksum(src, 'sha256');
    let h2 = checksum(dest, 'sha256');
    if h1 != h2 { error("Backup verification failed") }
}
```

### Build Script (Procedural)

```lambda
pn build() {
    chdir(/project);
    
    // Clean
    for f in /project.dist.* { delete(f) }
    
    // Compile
    let pid = spawn("tsc", ["--outDir", "dist"]);
    let result = wait(pid);
    if result != 0 { error("Compilation failed") }
    
    // Archive
    tar(/project.dist.*, /project.'dist.tar.gz');
    print("Build complete: " + checksum(/project.'dist.tar.gz', 'sha256'));
}
```

---

## 5. Shell Advantages Over Lambda

Areas where Unix shell genuinely excels over Lambda's design:

### Process Composition (Streaming)

```bash
# Shell: True streaming - memory efficient for huge files
tail -f /var/log/syslog | grep error | head -100
```

```lambda
// Lambda: Must load entire list at each stage
read_lines(/var.log.syslog) where ~ ~ /error/i | take(100)
```

Shell pipes are byte streams between processes. Data flows incrementally - you can `tail -f` a growing file indefinitely. Lambda's list-based pipes materialize the full list before passing to next stage.

### Process Orchestration

```bash
# Shell: Native job control
./server &          # Background
jobs                # List jobs
fg %1               # Bring to foreground
./a & ./b & wait    # Parallel processes, wait for all
```

Shell is built around process management. Lambda needs explicit `spawn()`/`wait()` which is more verbose.

### Interactive Features

```bash
!!                  # Repeat last command
!grep               # Last command starting with "grep"
Ctrl+R              # Reverse search history
fc                  # Edit last command in $EDITOR
```

Decades of interactive refinement. Lambda REPL is basic.

### Heredocs

```bash
# Shell: Heredocs are concise
cat <<EOF > config.txt
host: localhost
port: 8080
EOF
```

```lambda
// Lambda: More verbose
write(/config.txt, "host: localhost\nport: 8080\n")
```

### Implicit Glob Expansion

```bash
# Shell: Globs expand automatically
rm *.tmp
cp src/*.js dist/
```

```lambda
// Lambda: Requires explicit iteration
for f in .*.tmp { delete(f) }
```

### Exit Code Chaining

```bash
# Shell: Concise conditional execution
make && make test && make deploy
make || echo "Build failed"
```

Lambda needs explicit error checking - more verbose but clearer.

### Environment Manipulation

```bash
# Shell: Dynamic environment is natural
export PATH="$HOME/bin:$PATH"
cd /project && source .env && npm start
```

Mutable environment is shell's core model. Lambda's pure functional approach makes this awkward by design.

### Subshell Isolation

```bash
# Shell: Subshell isolates changes
(cd /tmp && rm -rf build)   # cd doesn't affect parent
pwd                          # Still in original dir
```

### Signals & Traps

```bash
trap "rm -f $tmpfile" EXIT
trap "echo Interrupted" INT
```

Deep OS integration. Lambda would need explicit API.

### Terseness

```bash
ls -la | grep foo       # Shell: terse
du -sh *
```

```lambda
/current.* where ~.name ~ /foo/   // Lambda: explicit
```

---

## 6. Design Trade-offs

| Shell Strength | Lambda Response |
|----------------|-----------------|
| Streaming pipes | Consider lazy iterators / generators |
| Job control | Accept verbosity, or add `&` syntax sugar |
| Glob expansion | Keep explicit (type safety wins) |
| Heredocs | Could add `"""` multi-line string syntax |
| `&&`/`||` chains | Keep explicit (clarity wins) |
| Environment mutation | Accept pure functional trade-off |
| Terseness | Accept - Lambda optimizes for correctness, not brevity |

**Bottom line**: Shell is optimized for **interactive process orchestration** with implicit behaviors. Lambda is optimized for **data transformation** with explicit, type-safe operations. Lambda shouldn't try to be a shell, but can borrow good ideas.

---

## 7. Future Considerations

### File Watching

```lambda
// Proposed syntax
watch(/src.**.*) | on_change(~, fn(file) {
    print("Changed: " + file);
    if file.ext == "ts" { compile(file) }
})
```

### Parallel Execution

```lambda
// Process files in parallel
/images.*.png | parallel(~, fn(f) {
    let resized = resize(f, 800, 600);
    write(f.parent ++ ("thumb_" + f.name), resized);
})
```

### Lazy Streaming (Potential)

```lambda
// Lazy iteration for large files
stream(/var.log.syslog) where ~ ~ /error/ | take(100)
```

---

## See Also

- [Lambda Path Implementation](Lambda_Path_Impl.md) - Path type internals
- [Lambda System Info](Lambda_Sysinfo.md) - `sys.*` scheme reference
