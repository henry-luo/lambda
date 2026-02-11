# Lambda Worker Proposal

## Executive Summary

This proposal introduces **Workers** to the Lambda runtime to enable concurrent processing. Currently, Lambda assumes single-threaded execution, which limits its ability to leverage modern multi-core processors and handle I/O-bound workloads efficiently. By adding worker support, Lambda can execute computationally expensive tasks in parallel and maintain responsiveness for interactive applications.

**Key Design Decisions**:
1. **Worker processes at user level** — User code creates worker processes only; threads remain internal to Lambda runtime
2. **Module-based workers** — Workers run Lambda modules (packages) with `pn main()` entry points
3. **Unified with CLI** — Workers integrate naturally with existing `cmd()` and pipe operators

**Reference**: The design draws inspiration from:
- **Node.js Child Processes**: `child_process` module with IPC via serialized messages
- **Erlang/Elixir Processes**: Actor model with message passing
- **Unix Process Model**: `fork()`, pipes, process isolation

---

## Table of Contents

1. [Motivation](#motivation)
2. [Design Goals](#design-goals)
3. [Design Options Comparison](#design-options-comparison)
4. [Proposed Design](#proposed-design)
5. [Language Syntax](#language-syntax)
6. [API Functions](#api-functions)
7. [CLI Integration](#cli-integration)
8. [Runtime Implementation](#runtime-implementation)
9. [Memory Model](#memory-model)
10. [Error Handling](#error-handling)
11. [Migration Path](#migration-path)
12. [Open Questions](#open-questions)

---

## Motivation

### Current Limitations

1. **Single-threaded execution**: Lambda scripts run sequentially, unable to utilize multiple CPU cores
2. **Blocking operations**: I/O operations (file reads, network requests) block the entire runtime
3. **No parallelism**: CPU-intensive tasks like data transformation, parsing, and rendering cannot be parallelized
4. **Interactive latency**: Long-running computations freeze interactive applications

### Use Cases

| Use Case | Benefit from Workers |
|----------|---------------------|
| Batch document processing | Process multiple files in parallel |
| Large data transformations | Split work across cores |
| PDF/SVG rendering | Render pages concurrently |
| Network I/O | Non-blocking fetches with callbacks |
| Background validation | Validate schemas without blocking UI |
| Image processing | Parallel pixel/tile operations |

---

## Design Goals

1. **Simplicity**: Workers should be easy to create and communicate with
2. **Safety**: No shared mutable state; prevent data races by design
3. **Compatibility**: Preserve Lambda's pure functional semantics
4. **Performance**: Minimal overhead for worker creation and messaging
5. **Portability**: Work across macOS, Linux, and Windows

---

## Design Options Comparison

### Option A: Worker Threads (Shared Memory)

Similar to **Node.js Worker Threads** or **Java Threads**.

```
┌─────────────────────────────────────────────────────┐
│                  Lambda Process                      │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐             │
│  │ Main    │  │ Worker  │  │ Worker  │             │
│  │ Thread  │  │ Thread 1│  │ Thread 2│             │
│  └────┬────┘  └────┬────┘  └────┬────┘             │
│       │            │            │                   │
│       └────────────┴────────────┘                   │
│              Shared Memory Space                    │
│         (Heap, Name Pool, Constants)                │
└─────────────────────────────────────────────────────┘
```

**Implementation Approach**:
- Use `pthread` (POSIX threads) or C++11 `std::thread`
- Threads share the same address space
- Each thread has its own Lambda `EvalContext` and stack
- Shared data structures require synchronization (mutex/lock-free)

**Pros**:

| Advantage | Description |
|-----------|-------------|
| ✅ Fast creation | Threads spawn in microseconds |
| ✅ Low memory | Shared heap, code, and constants |
| ✅ Efficient data transfer | No serialization; pass pointers |
| ✅ SharedArrayBuffer-like | Can implement shared binary buffers |
| ✅ Single-process debugging | All threads visible in one debugger session |

**Cons**:

| Disadvantage | Description |
|--------------|-------------|
| ❌ Data races | Shared memory requires careful synchronization |
| ❌ GIL complexity | May need Global Interpreter Lock (like Python) |
| ❌ Complex memory management | Reference counting across threads is tricky |
| ❌ Crash propagation | One thread crash kills entire process |
| ❌ Platform differences | Windows threads behave differently from POSIX |
| ❌ Lambda semantics | Violates pure functional model if state is shared |

**Thread-Safety Challenges for Lambda**:
- `NamePool` requires thread-safe interning (lock on insert)
- `ShapePool` needs synchronization for map shape caching
- Reference counting needs atomic operations (`__sync_fetch_and_add`)
- Memory pools (`Pool`, `Arena`) are not thread-safe

---

### Option B: Worker Processes (Isolated Memory)

Similar to **Node.js Child Processes** or **Erlang/Elixir Processes**.

```
┌─────────────────┐    IPC     ┌─────────────────┐
│  Main Process   │◄─────────►│ Worker Process 1 │
│  ┌───────────┐  │  (pipes)   │  ┌───────────┐  │
│  │EvalContext│  │            │  │EvalContext│  │
│  │Heap       │  │            │  │Heap       │  │
│  │NamePool   │  │            │  │NamePool   │  │
│  └───────────┘  │            │  └───────────┘  │
└─────────────────┘            └─────────────────┘
         │                              │
         │         ┌─────────────────┐  │
         └────────►│ Worker Process 2 │◄┘
                   │  ┌───────────┐  │
                   │  │EvalContext│  │
                   │  │Heap       │  │
                   │  └───────────┘  │
                   └─────────────────┘
```

**Implementation Approach**:
- Use `fork()` on Unix or `CreateProcess()` on Windows
- Each process has isolated memory space
- Communication via IPC: pipes, Unix sockets, or shared memory segments
- Message passing with serialization (JSON-like binary format)

**Pros**:

| Advantage | Description |
|-----------|-------------|
| ✅ Complete isolation | No shared state; no data races |
| ✅ Crash containment | Worker crash doesn't affect main process |
| ✅ Pure functional | Naturally fits Lambda's immutable model |
| ✅ Simple memory management | Each process has independent ref counting |
| ✅ No GIL needed | Full parallelism without locks |
| ✅ Security | Workers can be sandboxed |

**Cons**:

| Disadvantage | Description |
|--------------|-------------|
| ❌ Higher memory usage | Each process duplicates runtime |
| ❌ Slower startup | Process spawn is ~1-10ms vs ~10μs for threads |
| ❌ Serialization overhead | Data must be serialized for transfer |
| ❌ More OS resources | File descriptors, process table entries |
| ❌ Complex IPC | Pipes/sockets require careful protocol design |
| ❌ Windows complexity | No `fork()`; need different implementation |

---

### Comparison Summary

| Criterion | Worker Threads | Worker Processes |
|-----------|---------------|------------------|
| **Memory Usage** | Low (shared) | High (duplicated) |
| **Startup Time** | ~10μs | ~1-10ms |
| **Data Transfer** | Fast (pointer) | Slow (serialize) |
| **Isolation** | Weak | Strong |
| **Crash Safety** | Poor | Good |
| **Implementation** | Complex | Moderate |
| **Lambda Fit** | Poor (mutable state issues) | Good (pure functional) |
| **Debugging** | Easier | Harder |

---

### Recommendation: Hybrid Approach

**Primary**: Worker Processes (Option B) for safety and Lambda semantics alignment.

**Secondary**: Lightweight thread pool for internal async I/O (already exists in `network_thread_pool.cpp`).

**Rationale**:
1. Lambda's pure functional nature maps naturally to isolated processes
2. Reference counting + shared memory is error-prone
3. Crash isolation is critical for production reliability
4. Serialization cost is acceptable for typical workloads (data < 1MB)
5. Process startup cost can be mitigated with worker pools

---

## Proposed Design

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Lambda Runtime                          │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                    Main Process                        │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌───────────────┐  │  │
│  │  │ WorkerPool  │  │ MessageBus  │  │ ResultCollector│  │  │
│  │  └──────┬──────┘  └──────┬──────┘  └───────┬───────┘  │  │
│  └─────────┼────────────────┼─────────────────┼──────────┘  │
│            │                │                 │             │
│    ┌───────▼───────┐  ┌─────▼─────┐  ┌───────▼───────┐    │
│    │ Worker Proc 1 │  │ Worker 2   │  │ Worker Proc N │    │
│    │ (data proc)   │  │ (render)   │  │ (validation)  │    │
│    └───────────────┘  └───────────┘  └───────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### Core Components

1. **Worker Process**: Isolated Lambda runtime running a module with `pn main()`
2. **WorkerPool**: Manages pre-spawned worker processes for parallel execution
3. **Channel**: Bidirectional communication via stdin/stdout pipes
4. **Pipe Integration**: Workers can be chained with `|` like shell commands

---

## Language Syntax

### Running a Module as Worker

The core concept: **A worker is a Lambda module with `pn main()` run in a separate process.**

```lambda
// processor.ls - a module that can run as worker
import io;

pn main() {
    // Read input from stdin (sent by parent)
    let data = input('stdin, 'json)
    
    // Process data
    let result = data | heavy_computation(~)
    
    // Write output to stdout (received by parent)
    output(result, 'stdout, 'json)
}

fn heavy_computation(item) {
    // ... CPU-intensive work
    item
}
```

```lambda
// main.ls - parent that spawns workers
import processor: .processor;

pn main() {
    // Option 1: Run module as worker process
    let worker = run(processor)
    
    // Option 2: Run module file directly  
    let worker = run("./processor.ls")
    
    // Send data and receive result
    worker.send({items: [1, 2, 3, 4, 5]})
    let result = worker.receive()
    
    worker.close()
}
```

### Worker Communication Patterns

#### Pattern 1: Request-Response (Single Message)

```lambda
import processor: .processor;

pn main() {
    let worker = run(processor)
    
    // Send input, wait for single output
    let result = worker.call({data: large_dataset})
    
    print("Processed: ", result)
}
```

#### Pattern 2: Streaming (Multiple Messages)

```lambda
// stream_processor.ls
pn main() {
    // Process stream of inputs
    for msg in input.stream('stdin', 'json') {
        let result = process(msg)
        output(result, 'stdout', 'json')
    }
}
```

```lambda
// main.ls
pn main() {
    let worker = run("./stream_processor.ls")
    
    // Send multiple messages
    for item in items {
        worker.send(item)
        let result = worker.receive()
        print(result)
    }
    
    worker.close()
}
```

#### Pattern 3: Fire-and-Forget (Background)

```lambda
pn main() {
    // Run in background, don't wait
    let worker = run("./background_task.ls", {detach: true})
    
    // Continue without waiting
    print("Background task started with PID: ", worker.pid)
}
```

### Pipe Syntax for Workers

**Key insight**: Workers should integrate with Lambda's pipe operator `|` just like shell commands.

```lambda
pn main() {
    // Pipe data through worker (like Unix pipes)
    let result = data | run(processor) | format(~, 'json)
    
    // Chain multiple workers
    let result = input_data 
        | run(parser)      // parse in worker
        | run(transformer) // transform in worker  
        | run(validator)   // validate in worker
    
    // Mix with cmd() - external shell commands
    let result = data
        | run(processor)           // Lambda worker
        | cmd("jq", ".items[]")    // External command
        | run(aggregator)          // Lambda worker
}
```

### Shell-Style Process Operators

**Key insight**: Adopt familiar shell operators for process control: `&&`, `||`, `&`

#### Background Execution: `&`

```lambda
pn main() {
    // Run in background (non-blocking)
    let worker = run(processor) &
    
    // Continue immediately without waiting
    print("Worker started with PID: ", worker.pid)
    
    // Do other work...
    
    // Later, wait for completion
    worker.wait()
}
```

#### Sequential with Short-Circuit: `&&` and `||`

```lambda
pn main() {
    // && : Run next only if previous succeeds (exit code 0)
    run(validator) && run(processor) && run(publisher)
    
    // || : Run next only if previous fails (exit code != 0)
    run(primary_server) || run(fallback_server)
    
    // Combined patterns
    run(setup) && run(main_task) || run(cleanup_on_error)
}
```

#### Parallel Execution: `&` with Multiple Processes

```lambda
pn main() {
    // Start multiple workers in parallel
    let w1 = run(analyzer1) &
    let w2 = run(analyzer2) &
    let w3 = run(analyzer3) &
    
    // All three running concurrently
    // Wait for all to complete
    let results = [
        w1.wait(),
        w2.wait(),
        w3.wait()
    ]
}
```

#### Comparison with Shell

| Operator | Shell Meaning | Lambda Meaning |
|----------|---------------|----------------|
| `&` | Run in background | Run process async, return handle |
| `&&` | Run if previous succeeded | Run if previous exit code = 0 |
| `\|\|` | Run if previous failed | Run if previous exit code ≠ 0 |
| `\|` | Pipe stdout→stdin | In-memory transformation |
| `\|>` | N/A | Process pipe (serialize→stdin) |

---

## API Functions

### run() — Core Worker Function

The `run()` function is the primary way to spawn worker processes.

```lambda
// Signature
run(module: module | string, options?: map) -> Worker

// Options
{
    detach: bool,      // Run in background (default: false)
    timeout: int,      // Timeout in milliseconds (default: none)
    env: map,          // Environment variables to pass
    cwd: string,       // Working directory
    stdin: 'pipe | 'inherit | 'null,   // stdin handling (default: 'pipe)
    stdout: 'pipe | 'inherit | 'null,  // stdout handling (default: 'pipe)
    stderr: 'pipe | 'inherit | 'null,  // stderr handling (default: 'inherit)
}
```

**Examples:**
```lambda
// Run imported module
import processor: .processor;
let worker = run(processor)

// Run script file
let worker = run("./scripts/processor.ls")

// Run with options
let worker = run(processor, {
    timeout: 30000,        // 30 second timeout
    env: {DEBUG: "true"},  // pass environment
    cwd: "./data"          // set working directory
})

// Background process
let worker = run("./daemon.ls", {detach: true})
```

### Worker Instance Methods

```lambda
// Communication
worker.send(data: any) -> void           // send data to worker's stdin (serialized)
worker.receive() -> any                   // blocking receive from worker's stdout
worker.receive(timeout: int) -> any?      // receive with timeout (returns null on timeout)
worker.call(data: any) -> any            // send + receive in one call

// Lifecycle
worker.close() -> void                   // close stdin, wait for worker to finish
worker.kill() -> void                    // force kill worker (SIGKILL)
worker.wait() -> int                     // wait for worker to exit, return exit code
worker.is_alive() -> bool                // check if worker is still running

// Properties (read-only)
worker.pid: int                          // OS process id
worker.exit_code: int?                   // exit code (null if still running)
```

---

## CLI Integration

### Integration with cmd()

`cmd()` executes external shell commands. Workers (`run()`) execute Lambda modules. Both should work seamlessly together.

**Current cmd() signature:**
```lambda
cmd(command: string, args...: string) -> string  // returns stdout as string
```

**Unified model**: Both `cmd()` and `run()` return process handles that can be piped.

```lambda
pn main() {
    // External command
    let files = cmd("ls", "-la")
    
    // Lambda worker
    let worker = run(processor)
    let result = worker.call(data)
    
    // They should compose naturally
}
```

### Pipe Operator Integration

**Goal**: Enable Unix-style pipelines mixing Lambda workers and shell commands.

```
┌──────────┐    pipe    ┌──────────┐    pipe    ┌──────────┐
│  data    │ ─────────► │ Worker 1 │ ─────────► │ cmd()    │
│ (Lambda) │   stdin    │ (Lambda) │   stdout   │ (shell)  │
└──────────┘            └──────────┘            └──────────┘
```

#### Syntax Options

**Option A: Explicit pipe functions**
```lambda
pn main() {
    let result = data 
        |> run(parser)           // pipe to worker
        |> cmd("jq", ".[]")      // pipe to shell command
        |> run(aggregator)       // pipe to worker
}
```

**Option B: Unified with current pipe `|`**
```lambda
pn main() {
    // When RHS is a process (run/cmd), treat as process pipe
    let result = data 
        | run(parser)
        | cmd("sort")
        | run(aggregator)
}
```

**Option C: Process-specific operator `|>`**
```lambda
pn main() {
    // |> for process piping (data serialization)
    // |  for in-memory transformation (current behavior)
    let result = data 
        |> run(parser)           // serialize data, pipe to process
        | transform(~)           // in-memory transformation  
        |> cmd("grep", "error")  // pipe to shell command
}
```

**Recommendation**: Option C — distinct operator for clarity.

### cmd() Enhancement for Piping

Enhance `cmd()` to support input piping:

```lambda
// Current: cmd() with no stdin
let output = cmd("ls", "-la")

// Enhanced: cmd() with piped input
let output = data | cmd("grep", "pattern")   // data piped to stdin
let output = cmd("cat", "file.txt") | cmd("grep", "error")  // chain commands

// Full example
pn main() {
    let json_data = {items: [1, 2, 3]}
    
    // Pipe Lambda data through jq
    let filtered = json_data 
        | format(~, 'json)       // serialize to JSON string
        |> cmd("jq", ".items[]") // pipe to jq
        | input(~, 'json)        // parse result back
    
    print(filtered)  // [1, 2, 3]
}
```

### Process Handle Unification

Both `run()` and `cmd()` return a **Process** handle with similar interface:

```lambda
// Process type (returned by both run() and cmd())
Process {
    pid: int,
    stdin: WriteStream?,    // if stdin = 'pipe
    stdout: ReadStream?,    // if stdout = 'pipe
    stderr: ReadStream?,    // if stderr = 'pipe
    
    // Methods
    send(data) -> void,     // write to stdin
    receive() -> any,       // read from stdout
    wait() -> int,          // wait for exit, return code
    kill() -> void,         // terminate process
}
```

### Pipeline Execution Model

```lambda
pn main() {
    // This pipeline:
    let result = input_data 
        |> run(parser)
        |> cmd("sort")
        |> run(formatter)
    
    // Is equivalent to:
    let p1 = run(parser)
    let p2 = cmd("sort")  
    let p3 = run(formatter)
    
    // Connect pipes: p1.stdout -> p2.stdin -> p3.stdin
    connect_pipe(p1.stdout, p2.stdin)
    connect_pipe(p2.stdout, p3.stdin)
    
    // Send input, collect output
    p1.send(input_data)
    p1.close_stdin()
    let result = p3.receive_all()
}
```

### Format Negotiation

When piping between Lambda workers and shell commands, format handling:

```lambda
pn main() {
    // Lambda → Lambda: Binary serialization (efficient)
    let r1 = data |> run(worker1) |> run(worker2)
    
    // Lambda → Shell: JSON by default (human-readable)
    let r2 = data |> cmd("jq", ".")
    
    // Shell → Lambda: Parse as string, optionally parse JSON
    let r3 = cmd("echo", '{"a":1}') | input(~, 'json)
    
    // Explicit format control
    let r4 = data 
        | format(~, 'json)      // serialize to JSON
        |> cmd("jq", ".items")  // process with jq
        | input(~, 'json)       // parse result
}
```

### Parallel Execution with `&`

```lambda
pn main() {
    // Fan-out: same data to multiple workers using &
    let w1 = run(analyzer1) &
    let w2 = run(analyzer2) &
    let w3 = run(analyzer3) &
    
    // Send same data to all
    w1.send(data)
    w2.send(data)
    w3.send(data)
    
    // Fan-in: collect results
    let r1 = w1.receive()
    let r2 = w2.receive()
    let r3 = w3.receive()
    
    let combined = [r1, r2, r3] | flatten(~)
}
```

---

## Runtime Implementation

### Phase 1: Process-Based Workers

#### 1.1 Worker Process Spawning

```cpp
// worker_process.hpp
struct WorkerProcess {
    pid_t pid;                    // process ID
    int stdin_pipe[2];            // main → worker
    int stdout_pipe[2];           // worker → main
    int stderr_pipe[2];           // worker → main (errors)
    WorkerState state;            // idle, running, terminated
    int exit_code;                // exit code (-1 if still running)
    const char* module_path;      // path to Lambda module
};

// Spawn worker process running a Lambda module
WorkerProcess* worker_spawn(const char* module_path, WorkerOptions* opts) {
    WorkerProcess* worker = (WorkerProcess*)pool_calloc(sizeof(WorkerProcess));
    worker->module_path = module_path;
    worker->exit_code = -1;
    
    // Create pipes
    pipe(worker->stdin_pipe);
    pipe(worker->stdout_pipe);
    if (opts && opts->capture_stderr) {
        pipe(worker->stderr_pipe);
    }
    
    worker->pid = fork();
    if (worker->pid == 0) {
        // Child process: redirect stdio
        dup2(worker->stdin_pipe[0], STDIN_FILENO);
        dup2(worker->stdout_pipe[1], STDOUT_FILENO);
        close(worker->stdin_pipe[1]);
        close(worker->stdout_pipe[0]);
        
        // Set working directory if specified
        if (opts && opts->cwd) {
            chdir(opts->cwd);
        }
        
        // Execute: ./lambda.exe run <module_path>
        execl("./lambda.exe", "lambda.exe", "run", module_path, NULL);
        
        // If execl fails
        _exit(127);
    }
    
    // Parent process: close unused ends
    close(worker->stdin_pipe[0]);
    close(worker->stdout_pipe[1]);
    worker->state = WORKER_RUNNING;
    
    return worker;
}
```

**Key insight**: Workers are spawned by executing `./lambda.exe run <module.ls>`. This:
1. Reuses existing CLI infrastructure
2. Ensures consistent runtime behavior
3. Simplifies implementation (no embedding)

#### 1.2 Message Protocol

```cpp
// message_protocol.h
// Framing protocol for Lambda IPC

// Message header (fixed 8 bytes)
typedef struct MessageHeader {
    uint32_t length;        // payload length (excluding header)
    uint16_t type;          // message type
    uint16_t flags;         // MSG_FLAG_* flags
} MessageHeader;

// Message types
enum MessageType {
    MSG_DATA       = 0x01,  // Lambda data (serialized Item)
    MSG_EOF        = 0x02,  // End of stream
    MSG_ERROR      = 0x03,  // Error message
    MSG_PING       = 0x04,  // Keep-alive ping
    MSG_PONG       = 0x05,  // Keep-alive response
};

// Message flags
enum MessageFlags {
    MSG_FLAG_JSON   = 0x01,  // Payload is JSON (not binary)
    MSG_FLAG_LAST   = 0x02,  // Last message in sequence
    MSG_FLAG_STREAM = 0x04,  // Part of streaming sequence
};

// Write framed message
bool write_message(int fd, MessageType type, const uint8_t* data, size_t len) {
    MessageHeader header = {
        .length = (uint32_t)len,
        .type = type,
        .flags = 0
    };
    
    // Write header + payload atomically
    struct iovec iov[2] = {
        {&header, sizeof(header)},
        {(void*)data, len}
    };
    return writev(fd, iov, 2) == sizeof(header) + len;
}

// Read framed message (blocking)
bool read_message(int fd, MessageHeader* header, uint8_t* buffer, size_t max_len) {
    // Read header
    if (read(fd, header, sizeof(*header)) != sizeof(*header)) {
        return false;  // EOF or error
    }
    
    // Validate length
    if (header->length > max_len) {
        return false;  // Message too large
    }
    
    // Read payload
    size_t total = 0;
    while (total < header->length) {
        ssize_t n = read(fd, buffer + total, header->length - total);
        if (n <= 0) return false;
        total += n;
    }
    
    return true;
}
```

#### 1.3 Serialization Strategy

```cpp
// serialization.cpp
// Use Lambda's existing pack/unpack for efficient binary serialization

// Serialize Lambda Item to binary
size_t serialize_item(Item item, uint8_t* buffer, size_t max_len, bool as_json) {
    if (as_json) {
        // JSON format for cmd() interop
        StringBuf sb;
        stringbuf_init(&sb);
        format_json(item, &sb);
        size_t len = min(sb.len, max_len);
        memcpy(buffer, sb.data, len);
        stringbuf_free(&sb);
        return len;
    } else {
        // Binary format for Lambda-to-Lambda (more efficient)
        return pack_item(item, buffer, max_len);
    }
}

// Deserialize binary to Lambda Item
Item deserialize_item(const uint8_t* buffer, size_t len, EvalContext* ctx, bool is_json) {
    if (is_json) {
        return parse_json((const char*)buffer, len, ctx);
    } else {
        return unpack_item(buffer, len, ctx);
    }
}
```

#### 1.4 Worker Event Loop (Child Side)

The worker process runs a standard Lambda module with `pn main()`. Communication happens via `input('stdin)` and `output(..., 'stdout)`:

```cpp
// In lambda-proc.cpp, enhance stdin/stdout handling

// input('stdin, 'json) - reads framed messages
Item builtin_input_stdin(EvalContext* ctx, Item format) {
    MessageHeader header;
    uint8_t buffer[MAX_MESSAGE_SIZE];
    
    if (!read_message(STDIN_FILENO, &header, buffer, sizeof(buffer))) {
        return ItemNull;  // EOF
    }
    
    bool is_json = (header.flags & MSG_FLAG_JSON) || get_symbol_id(format) == SYM_JSON;
    return deserialize_item(buffer, header.length, ctx, is_json);
}

// output(data, 'stdout, 'json) - writes framed messages  
void builtin_output_stdout(EvalContext* ctx, Item data, Item format) {
    uint8_t buffer[MAX_MESSAGE_SIZE];
    bool as_json = get_symbol_id(format) == SYM_JSON;
    
    size_t len = serialize_item(data, buffer, sizeof(buffer), as_json);
    
    uint16_t flags = as_json ? MSG_FLAG_JSON : 0;
    write_message(STDOUT_FILENO, MSG_DATA, buffer, len);
}
```

#### 1.5 WorkerPool Implementation

```cpp
// worker_pool.cpp
struct WorkerPool {
    WorkerProcess** workers;      // array of worker processes
    int count;                    // number of workers
    const char* module_path;      // module each worker runs
    pthread_mutex_t mutex;        // pool access lock
    pthread_cond_t available;     // signal when worker becomes available
};

WorkerPool* worker_pool_create(const char* module_path, int n) {
    WorkerPool* pool = (WorkerPool*)pool_calloc(sizeof(WorkerPool));
    pool->module_path = module_path;
    pool->workers = (WorkerProcess**)pool_calloc(n * sizeof(WorkerProcess*));
    pool->count = n;
    
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->available, NULL);
    
    // Pre-spawn workers
    for (int i = 0; i < n; i++) {
        pool->workers[i] = worker_spawn(module_path, NULL);
    }
    
    return pool;
}

// Parallel map: distribute items across workers
Item worker_pool_map(WorkerPool* pool, Item items, EvalContext* ctx) {
    Array* arr = get_array(items);
    int n = arr->count;
    
    // Results array
    Array* results = create_array(n);
    
    // Simple round-robin distribution
    for (int i = 0; i < n; i++) {
        WorkerProcess* worker = pool->workers[i % pool->count];
        
        // Send item to worker
        worker_send(worker, arr->items[i]);
    }
    
    // Collect results in order
    for (int i = 0; i < n; i++) {
        WorkerProcess* worker = pool->workers[i % pool->count];
        results->items[i] = worker_receive(worker);
    }
    
    return make_array_item(results);
}
```

### Phase 2: Pipe Operator Integration

#### 2.1 Grammar Extension

```js
// grammar.js additions
pipe_expression: $ => prec.left(PREC.PIPE, seq(
    $._expression,
    choice('|', '|>'),  // | for in-memory, |> for process
    $._expression
)),

// run() is a regular function call
run_expression: $ => seq(
    'run',
    '(',
    choice($.identifier, $.string),  // module ref or path
    optional(seq(',', $.map_literal)),  // options
    ')'
),
```

#### 2.2 Pipe Operator Transpilation

```cpp
// transpile.cpp - handle |> operator
void transpile_process_pipe(Node* pipe_node, TranspileContext* ctx) {
    Node* lhs = pipe_node->children[0];
    Node* rhs = pipe_node->children[1];
    
    // lhs |> rhs becomes:
    // tmp_worker = <rhs evaluation>
    // tmp_worker.send(<lhs evaluation>)
    // tmp_result = tmp_worker.receive()
    
    emit_code(ctx, "{\n");
    emit_code(ctx, "  Item _pipe_data = ");
    transpile_expr(lhs, ctx);
    emit_code(ctx, ";\n");
    
    emit_code(ctx, "  WorkerProcess* _pipe_proc = ");
    transpile_expr(rhs, ctx);  // should be run(...) or cmd(...)
    emit_code(ctx, ";\n");
    
    emit_code(ctx, "  worker_send(_pipe_proc, _pipe_data);\n");
    emit_code(ctx, "  worker_close_stdin(_pipe_proc);\n");
    emit_code(ctx, "  Item _pipe_result = worker_receive(_pipe_proc);\n");
    emit_code(ctx, "  worker_wait(_pipe_proc);\n");
    emit_code(ctx, "  _pipe_result;\n");
    emit_code(ctx, "}\n");
}
```

### Phase 3: cmd() Enhancement

```cpp
// lambda-proc.cpp - enhanced cmd() with stdin support

typedef struct CmdProcess {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
} CmdProcess;

// cmd() now returns a process handle when used in pipes
Item builtin_cmd(EvalContext* ctx, Item command, Item args, Item stdin_data) {
    CmdProcess* proc = spawn_cmd(get_string(command), args);
    
    if (!is_null(stdin_data)) {
        // Write stdin data
        const char* input = get_string(stdin_data);
        write(proc->stdin_fd, input, strlen(input));
        close(proc->stdin_fd);
    }
    
    // Read stdout
    StringBuf output;
    stringbuf_init(&output);
    char buffer[4096];
    ssize_t n;
    while ((n = read(proc->stdout_fd, buffer, sizeof(buffer))) > 0) {
        stringbuf_append(&output, buffer, n);
    }
    
    // Wait for process
    int status;
    waitpid(proc->pid, &status, 0);
    
    return make_string_item(output.data, output.len);
}
```

---

## Memory Model

### Message Passing Semantics

```
Main Process                    Worker Process
┌─────────────┐                ┌─────────────┐
│ Item A      │   serialize    │             │
│ (ref_cnt=1) │ ──────────────►│ Item A'     │
└─────────────┘    (copy)      │ (ref_cnt=1) │
                               └─────────────┘
```

**Rules**:
1. All messages are **deep-copied** (serialized/deserialized)
2. No reference sharing between processes
3. Each process manages its own reference counts
4. Circular references are handled by serialization

### Serialization Formats

| Context | Format | Reason |
|---------|--------|--------|
| Lambda → Lambda worker | Binary (pack/unpack) | Efficient, preserves types |
| Lambda → Shell command | JSON string | Universal, human-readable |
| Shell → Lambda | String (parse if needed) | Flexible parsing |

---

## Error Handling

### Worker Error Types

```lambda
// Error types for workers
WorkerError {
    kind: 'spawn_failed | 'terminated | 'timeout | 'io_error,
    message: string,
    pid: int?,
    exit_code: int?,
    cause: Error?
}
```

### Error Propagation

```lambda
pn main() {
    let worker = run("./processor.ls")
    
    // Using error result pattern
    let result^err = worker.call(data)
    if err {
        match err.kind {
            'timeout -> print("Worker timed out"),
            'terminated -> print("Worker crashed with code: ", err.exit_code),
            _ -> print("Error: ", err.message)
        }
    }
    
    // Or check exit code
    worker.close()
    if worker.exit_code != 0 {
        print("Worker failed with exit code: ", worker.exit_code)
    }
}
```

### Timeout Handling

```lambda
pn main() {
    let worker = run("./slow_task.ls", {timeout: 30000})  // 30 second timeout
    
    let result = worker.receive(timeout: 5000)  // 5 second timeout for this receive
    if result == null {
        print("Receive timed out")
        worker.kill()  // force kill
    }
}
```

---

## Migration Path

### Phase 1: Core Infrastructure (v0.1)
- [ ] `run()` function to spawn worker processes
- [ ] Process handle with `send()`, `receive()`, `close()`, `kill()`, `wait()`
- [ ] Message framing protocol over pipes
- [ ] Binary serialization using existing pack/unpack
- [ ] Basic error handling (exit codes, timeouts)

### Phase 2: CLI Integration (v0.2)
- [ ] `|>` pipe operator for process piping
- [ ] Enhanced `cmd()` with stdin piping support
- [ ] `input('stdin)` / `output(..., 'stdout)` enhancements
- [ ] Format negotiation (JSON for shell, binary for Lambda)

### Phase 3: Process Operators (v0.3)
- [ ] `&` operator for background/async execution
- [ ] `&&` operator for sequential success chaining
- [ ] `||` operator for fallback on failure
- [ ] Cross-platform Windows support (`CreateProcess` + named pipes)

### Future: WorkerPool (TBD)
- [ ] `WorkerPool.create(module, n)` API for managed worker pools
- [ ] `pool.map()` for parallel processing
- [ ] Worker reuse and lifecycle management
- [ ] Load balancing across workers

---

## Open Questions

1. **Pipe Operator Choice**: Should `|>` be the process pipe, or should `|` be overloaded based on RHS type?

2. **Module Resolution**: When a worker runs a module, how are relative imports resolved? 
   - Option A: Relative to worker module file
   - Option B: Relative to main process working directory

3. **Streaming vs Batch**: Should `input('stdin)` read all available data, or support streaming iteration?

4. **Windows Implementation**: 
   - Use `CreateProcess()` + named pipes
   - Or use anonymous pipes like Unix?
   - Performance implications of no `fork()`

5. **Debugging Support**:
   - Log forwarding from workers to main?
   - Attach debugger to worker process?
   - Worker process identification in `ps` output?

6. **Resource Limits**:
   - Should workers have configurable memory limits?
   - CPU time limits for worker tasks?
   - Use cgroups on Linux?

---

## Summary

This proposal introduces process-based workers to Lambda with:

1. **Module-centric design**: Workers are Lambda modules with `pn main()`
2. **Simple API**: `run(module)` to spawn, `worker.send()/receive()` to communicate
3. **CLI integration**: `|>` operator chains workers and `cmd()` naturally
4. **Shell-style operators**: `&` (background), `&&` (success chain), `||` (fallback)
5. **Process isolation**: Complete memory isolation, crash containment
6. **Pure functional fit**: Message passing preserves immutability semantics

The design prioritizes simplicity and safety over raw performance, aligning with Lambda's philosophy as a data processing language.

---

## References

- [Node.js Child Processes](https://nodejs.org/api/child_process.html)
- [Erlang Processes](https://www.erlang.org/doc/reference_manual/processes.html)
- [Go Goroutines and Channels](https://go.dev/tour/concurrency/1)
- [Unix Process Model](https://en.wikipedia.org/wiki/Process_(computing))
- [POSIX Pipes](https://man7.org/linux/man-pages/man7/pipe.7.html)
