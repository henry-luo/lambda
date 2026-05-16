# Lambda / Radiant — libuv Integration Strategy

**Status:** Proposal
**Scope:** Establishes the boundary between Lambda's synchronous file I/O, libuv-based async I/O (JS/Node, HTTP server, resource loader), Radiant's GUI/animation loop, and the network resource scheduler.
**Audience:** Engine maintainers; informs `lambda/js/*`, `lambda/serve/*`, `lambda/network/*`, `radiant/*`, `lib/file*.c`, `lib/uv_loop.h`.

---

## 0. Executive Summary

Lambda already uses libuv selectively (`lib/uv_loop.h`, `lambda/js/js_fs.cpp`, HTTP server, `lambda/network/network_thread_pool.cpp`). This proposal codifies **where libuv belongs and where it does not**, and outlines concrete enhancements in each area.

| Area | Decision | Rationale |
|------|----------|-----------|
| 1. Synchronous file I/O (`lib/file.c`, `lib/file_utils.c`) | **Keep stdio. Do not migrate to `uv_fs_*`.** | Callers are synchronous (parser, transpiler, REPL, build). libuv adds cost with no benefit. |
| 2. JS / Node async I/O | **Stay on libuv. Do not roll our own loop.** | Node-compat surface is too large; cross-platform fs/net/timer semantics are libuv's core competency. |
| 3. Radiant UI event + animation loop | **GUI loop is host; libuv is a ticked guest.** | vsync/IME/main-thread ownership belong to GLFW + native; libuv handles non-GUI async on the same thread via `UV_RUN_NOWAIT`. |
| 4. Network resource loading | **Use a dedicated network scheduler.** Current `uv_queue_work` downloads are a bootstrap; the final HTTP backend should be curl multi on a network thread, with libuv only for wakeups/integration. | HTTP needs browser-style priority, cancellation, per-origin limits, connection reuse, and main-thread document mutation. |

The four sections below give the rationale, the integration contract, and the **enhancements beyond what exists today**. The appendix lists concrete hardening for `lib/file.c` / `lib/file_utils.c`.

---

## 1. Keep Lambda's Own File I/O (`lib/file.c`, `lib/file_utils.c`)

### 1.1 Decision

Retain `lib/file.c` and `lib/file_utils.c` as a **synchronous, stdio-based, zero-dependency** layer. **Do not route them through `uv_fs_*`.**

### 1.2 Rationale

| Argument | Detail |
|----------|--------|
| Callers are synchronous | Lambda transpiler, AST builder, input parsers (JSON/XML/HTML/PDF/CSS/MD), formatters, validator, REPL, build infrastructure, test runner. None of them own an event loop. |
| `uv_fs_*` sync mode is just wrapped stdio | Passing `NULL` callback to `uv_fs_*` runs the same syscalls plus per-call `uv_fs_req_t` setup/teardown. Pure overhead. |
| Async fs is not useful for these callers | A 50 KB `.ls` source file reads in microseconds; the JIT compile that follows takes orders of magnitude longer. Async fs would not improve wall-clock time. |
| Layering is correct as-is | Sync stdio for engine internals; libuv for callers that *already have* a loop (`js_fs`, HTTP server, resource loader). This mirrors Node's own `fs.readFileSync` vs `fs.promises.readFile` split. |
| Avoids cross-cutting dependency | Pulling libuv into the parser/build path means libuv must always be linked, even for headless lambda runs. Current split keeps libuv optional to most subsystems. |

### 1.3 Contract

| Layer | Caller | Model | Backend |
|-------|--------|-------|---------|
| `lib/file.c`, `lib/file_utils.c` | Lambda core, parsers, transpiler, build, REPL | Synchronous, blocking | POSIX stdio + platform calls |
| `lambda/js/js_fs.cpp` | JS `fs.*` (callback + promise + sync) | Async on loop; sync variants block | libuv `uv_fs_*` |
| `lambda/serve/` | HTTP server static-file path | Async on loop | libuv (`uv_fs_*`, `uv_fs_sendfile`) |
| `lambda/input/input_http.cpp` / `lambda/network/*` | Remote URL fetch | Sync today; scheduler-backed async for Radiant | libcurl; final Radiant path uses `NetworkScheduler` |

### 1.4 Enhancements Beyond Current Code

These are quality improvements to the **synchronous** layer; they do not change the libuv boundary.

1. **Replace `ftell`/`fseek` with `fstat(st_size)`.** Already calling `fstat` in `read_text_file` / `read_binary_file` — drop the `SEEK_END`/`ftell` round-trip and use `sb.st_size` directly. Fixes large-file correctness on 32-bit `long` platforms and avoids issues with non-seekable inputs.

2. **Drop `_mktemp` on Windows** in `write_text_file_atomic` (TOCTOU race). Use `GetTempFileNameA` with the destination directory as prefix, then `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` for the final swap.

3. **`fsync`/`FlushFileBuffers` before rename** in atomic write. Without this, a crash between rename and the page-cache flush can produce a present-but-empty file.

4. **Thread-safe temp counter.** `s_temp_counter` in `file_temp_path` / `dir_temp_create` is a non-atomic `long`. Promote to `_Atomic long` or use `atomic_fetch_add` so network/cache workers (Section 4) can generate temp paths safely.

5. **Bounded recursion in `dir_walk_recursive`.** Currently unbounded — deep trees blow the stack. Convert to an explicit `ArrayList`-backed stack, with a configurable maximum depth (default 4096).

6. **Symlink-loop guard in `dir_walk` / `dir_delete` / `dir_copy`.** Track visited `(dev, ino)` pairs (via `lstat`) to avoid infinite loops on cyclic symlinks. Match `nftw`'s `FTW_PHYS` semantics.

7. **Document `MEM_CAT_TEMP` lifetime.** Add header doc that all returned `char*` are arena-allocated under `MEM_CAT_TEMP` and reclaimed at category reset; callers must not `free()` them directly. Optionally add `_dup` variants that take an explicit category.

8. **Glob: replace fragile inline matcher on Windows.** `file_find` has a hand-rolled wildcard matcher in the Windows branch that does not handle `?`/`[]` correctly. Either use `PathMatchSpecA` (shlwapi) or vendor a small portable `fnmatch` and use it on both platforms for consistency.

9. **Hash upgrade in `file_cache_path`.** DJB2 + 8 hex digits = 32-bit hash → ~50% collision risk at 77k entries. Use xxHash3 64-bit (already linked elsewhere via `lib/`) and 16 hex digits.

10. **Streaming reads for large files.** Add `read_text_file_streaming(filename, chunk_cb, user_data)` for files that don't fit comfortably in arena memory (e.g., large PDFs, datasets). Avoids the implicit "whole-file in `MEM_CAT_TEMP`" assumption.

These are listed in priority order; see Appendix A for sketches.

---

## 2. Keep libuv for JS / Node Async I/O

### 2.1 Decision

JavaScript runtime, Node-compatibility surface (`fs`, `net`, `dgram`, `dns`, `http`, `child_process`, timers, signals), and the HTTP server all **continue to use libuv**. Do not implement a custom event loop or async-fs abstraction.

### 2.2 Rationale

| Argument | Detail |
|----------|--------|
| Node-compat surface is huge | Jube's Node transpile target spans dozens of modules; each maps onto a libuv primitive. Reimplementing means re-deriving Node's idiosyncratic semantics (timer ordering, `process.nextTick`, `setImmediate`, microtask vs macrotask, signal handling). Any deviation breaks real-world npm packages. |
| Cross-platform I/O is the hard part | libuv abstracts kqueue (macOS), epoll (Linux), IOCP (Windows), plus thread-pool fallback for fs. ~30 kLOC of platform code, decades of edge-case fixes. |
| Battle-tested | Stream backpressure, half-closed TCP, TLS over pipes, graceful handle close, fd leaks under `EMFILE` — these took Node ~10 years to stabilize. |
| Cost is negligible | Static libuv ≈ 400 KB. Vs. the 8 MB release binary, immaterial. |
| Already integrated | `lib/uv_loop.h` provides the singleton loop; `lambda/js/js_fs.cpp` is fully wired. |

### 2.3 Contract

- **One default loop per process today**, accessed via `lambda_uv_loop()` in [lib/uv_loop.h](lib/uv_loop.h). This matches the current code and keeps JS, the HTTP server, `fs.watch`, and embedder wakeups simple.
- Treat the singleton as a **default runtime facility**, not as a permanent architectural constraint. New APIs should accept an explicit runtime/loop context where practical, or at least keep their state separate enough that moving to explicit contexts later is mechanical.
- This matters for retained Radiant documents, tests that run multiple runtimes in one process, embedders that already own an event loop, and future JS worker isolates. A process-global loop is convenient; process-global subsystem state is the part to avoid.
- Async-capable subsystems may share the default loop when they are part of the same runtime instance. Long-running policy engines, such as the final network scheduler, should own their own scheduler state and use the default loop only for wakeups/completion delivery.
- APIs that currently call `lambda_uv_loop()` directly should be considered legacy-compatible entry points. New lower-level helpers should prefer a context parameter such as `LambdaRuntime*`, `JsRuntime*`, `RadiantSession*`, or an explicit loop/integration handle.
- `lambda_uv_set_microtask_drain()` hooks JS task draining at libuv phase checkpoints. The drain order is `process.nextTick` first, then Promise/queueMicrotask jobs.
- Lambda Script itself (pure functional) **does not** expose raw async — async work is mediated by `io.*` modules or scheduler-backed APIs (Section 4) and returns synchronously unless the language explicitly grows async effects.

### 2.4 Enhancements Beyond Current Code

1. **Microtask ordering audit.** `lambda_uv_set_microtask_drain` is installed on both `uv_prepare_t` and `uv_check_t`, so JS task queues are checked before libuv enters poll and after poll callbacks complete. Timer and manually-dispatched async callbacks may still call `js_microtask_flush()` directly when they bypass normal libuv phase flow. Keep tests around Promise/timer/I/O ordering because mismatches here cause subtle npm breakage.

2. **`process.nextTick` queue.** Implemented as a distinct queue in `lambda/js/js_event_loop.cpp`. It drains before Promise microtasks at each JS task checkpoint, including callbacks queued with extra `nextTick` arguments.

3. **Tunable thread pool size.** Set `UV_THREADPOOL_SIZE` (env or `uv_os_setenv` before first `uv_*` call) based on CPU count. Default of 4 is fine for fs+dns, but starves under image decode + handler dispatch. Recommend `min(cores * 2, 16)`. Expose via `build_lambda_config.json`.

4. **Loop-thread assertion in debug builds.** Add `LAMBDA_ASSERT_LOOP_THREAD()` macro that aborts when any `uv_*` handle is touched from a non-loop thread. libuv is *not* thread-safe across handles; this catches concurrent-access bugs early.

5. **Handle leak detection.** On `lambda_uv_cleanup()`, walk `uv_walk(loop, ...)` to enumerate any still-active handle. Log each with the file/line of its creation (capture via thin wrapper macros). Currently silent leaks become Valgrind noise.

6. **Decouple `fs.watch` from main fs path.** `uv_fs_event_t` is the one stdio-can't-do feature you really need from libuv. Stabilize it before adding more Node-fs surface area; it currently crashes per `vibe/jube/Transpile_Node2.md`.

7. **TLS via mbedTLS, not OpenSSL.** Already chosen for the HTTP server — extend the same TLS layer to JS `tls`/`https` to avoid linking two TLS stacks in one binary.

8. **Graceful shutdown protocol.** Today `lambda_uv_stop()` calls `uv_stop`; needed: a documented two-phase shutdown — (a) refuse new work, (b) drain in-flight handles with deadline, (c) `uv_close` everything, (d) `uv_run(UV_RUN_DEFAULT)` until empty, (e) `uv_loop_close`. Tested via a harness.

9. **Loop integration hook for embedders.** Expose `uv_backend_fd(loop)` + `uv_backend_timeout(loop)` through `lib/uv_loop.h` so that Radiant (Section 3) and any future embedder can integrate without including `<uv.h>` directly in GUI code.

---

## 3. Radiant UI Event and Animation Loop

### 3.1 Decision

The **GUI loop owns the main thread**. libuv participates as a *ticked guest* via `uv_run(loop, UV_RUN_NOWAIT)`. The animation loop is driven by **vsync / display refresh**, not libuv timers.

### 3.2 Rationale

| Argument | Detail |
|----------|--------|
| Vsync, not millisecond ticks | Animations need ~16.67 ms aligned to the display's refresh callback (`CVDisplayLink` on macOS, `IDXGISwapChain::GetFrameLatencyWaitableObject` on Windows, `wl_surface_frame`/`glXWaitForSbcOML` on Linux). libuv timers fire on `epoll_wait`/`kqueue` timeout granularity (≥ 1 ms, often worse under load). Driving animations from libuv produces visible jitter. |
| Main-thread ownership | macOS: only the main thread may pump `NSApp`; Windows: a window's owning thread must call `GetMessage`. libuv knows nothing about either. If `uv_run` blocks, the UI freezes. |
| Input fidelity | GLFW / native message pump owns multi-touch, IME composition, dead keys, DPI changes, clipboard, accessibility. Routing input through libuv loses platform fidelity. |
| Already partially in place | [radiant/window.cpp](radiant/window.cpp) drives `glfwPollEvents` + `glfwWaitEventsTimeout((1.0/60.0) - dt)` in `main_loop`; libuv is reachable via `lambda_uv_loop()`. |

### 3.3 Contract

```text
while (!glfwWindowShouldClose(window)) {
    // 1. Drain platform events (input, resize, focus, IME)
    glfwPollEvents();

    // 2. Tick libuv non-blockingly — runs due timers, completes async I/O,
    //    fires resource-loader after_work callbacks
    uv_run(lambda_uv_loop(), UV_RUN_NOWAIT);

    // 3. Drain JS microtasks + process.nextTick
    drain_microtasks();

    // 4. Run animation tick using the *display* timestamp, not uv_now()
    double frame_time = display_link_now();
    animation_scheduler_tick(scheduler, frame_time);

    // 5. Layout dirty subtrees, paint, present (vsync-synchronized)
    if (is_dirty()) { layout(); paint(); present(); }

    // 6. Idle wait — sleep until either input or libuv has work
    if (!has_active_animations && !is_dirty()) {
        double timeout = compute_idle_timeout();
        glfwWaitEventsTimeout(timeout);
    }
}
```

Key invariants:
- `uv_run` is called with `UV_RUN_NOWAIT` — **never** `UV_RUN_DEFAULT` from the GUI thread.
- Animation timestamps come from the platform display link, not `uv_now()`.
- Wake-from-idle integrates libuv via `compute_idle_timeout()` — described below.

### 3.4 Enhancements Beyond Current Code

#### 3.4.1 Wake-from-idle integration

Today's `glfwWaitEventsTimeout((1.0/60.0) - dt)` wakes every ~16 ms even when nothing is happening. Two upgrades:

**(a) Compute idle timeout from libuv.**
```c
double compute_idle_timeout(void) {
    int uv_timeout_ms = uv_backend_timeout(lambda_uv_loop());
    if (uv_timeout_ms < 0) uv_timeout_ms = 1000;     // libuv has no deadline → cap at 1s
    double anim_timeout = animation_next_deadline(); // seconds until next frame, or +INF
    double t = uv_timeout_ms / 1000.0;
    if (anim_timeout < t) t = anim_timeout;
    return t;
}
```
Result: when the page is fully idle, the process sleeps until either GUI input arrives, a network response lands, or an animation needs a tick. Saves battery on laptops and lets tests run faster on CI.

**(b) Cross-thread wakeups via `uv_async_t`.**
The network thread pool (Section 4) needs to nudge the GUI thread when a download completes during an idle wait. Pattern:
```c
// On the GUI thread, during setup:
uv_async_init(loop, &gui_wake_async, NULL);

// From any worker thread, when a result is ready:
uv_async_send(&gui_wake_async);   // signal-safe, thread-safe
```
Because `gui_wake_async` is registered on the loop, `uv_async_send` causes `uv_backend_timeout` to return 0 immediately, which means `compute_idle_timeout()` returns 0, which means `glfwWaitEventsTimeout` returns instantly. The GUI loop is now responsive to network events without polling.

On platforms where `glfwWaitEventsTimeout` cannot be unblocked externally, supplement with `glfwPostEmptyEvent()` from the async callback.

#### 3.4.2 Vsync-driven animation source

Replace any `uv_timer_t`-driven animation logic with a platform display-link:

| Platform | Source |
|----------|--------|
| macOS | `CVDisplayLink` or `CADisplayLink`; deliver `CVTimeStamp::hostTime` to the scheduler |
| Windows | DWM frame pacing (`DwmFlush`) + `QueryPerformanceCounter`; move to `IDXGISwapChain::GetFrameLatencyWaitableObject` if Radiant grows a D3D swapchain |
| Linux/X11 | `glXGetSyncValuesOML` / `glXWaitVideoSyncSGI` once the GLX surface is explicitly owned by the clock layer |
| Linux/Wayland | `wl_surface_frame` callback once Radiant owns the Wayland surface rather than only the GLFW abstraction |
| Fallback | High-resolution monotonic clock + tracked refresh estimate |

Implementation should hide the platform source behind a small `RadiantFrameClock`
facade. The window loop asks the frame clock for the current frame timestamp and
the next useful wake deadline; `animation_scheduler_tick()` only receives the
timestamp and does not know whether it came from a native display link or the
monotonic fallback.

`requestAnimationFrame` follows the same rule: it is a JS queue drained from the
Radiant frame loop, not a `setTimeout(16)` shim and not a libuv timer. New rAF
callbacks scheduled during a frame wait for the next frame, and
`cancelAnimationFrame` cancels queued callbacks before delivery.

Initial backend mapping:
- macOS uses `CVDisplayLink` and wakes the GLFW loop only when animation/video work is waiting for the next frame.
- Windows uses a frame thread paced by `DwmFlush` when available, falling back to a QPC timer thread.
- Linux uses a `timerfd`-paced native high-resolution frame thread under GLFW. True X11/Wayland display callbacks require passing the actual GLX/Wayland surface into this layer, so they remain a follow-up behind the same facade.

This integrates with the existing animation design in [vibe/radiant/Radiant_Render_Threading_and_Animation.md](vibe/radiant/Radiant_Render_Threading_and_Animation.md).

#### 3.4.2a Video frame invalidation

Video playback follows the same rule as animation: **never use libuv timers to drive frames**. The video backend owns decode/playback timing and signals Radiant through `RdtVideoCallbacks::on_frame_ready`.

Radiant should treat that callback as a video-only invalidation:
- mark `DocState::video_frame_pending`;
- wake the GUI loop with the same lightweight wake path used by the frame clock (`glfwPostEmptyEvent()` today);
- on the next GUI tick, blit cached video placements without rebuilding layout or the display list;
- clear `video_frame_pending` after the cached blit/full render consumes the frame.

This avoids the old `has_active_video → redraw every display frame` behavior. Paused video and low-FPS media no longer burn a full-rate render loop, while active playback still presents promptly when the backend reports a fresh frame.

#### 3.4.3 `requestAnimationFrame` bridge for JS

When JS calls `requestAnimationFrame(cb)`, do **not** route it through `uv_timer_t`. Instead, register the callback against the animation scheduler so it fires from the display-link tick with the correct frame timestamp. This matches browser semantics (rAF callback receives the high-resolution frame time) and avoids one-frame lag.

#### 3.4.4 Multi-window / multi-loop discipline

If/when Radiant supports multiple top-level windows:
- Start with one default libuv loop shared by the process, not one loop per window.
- Each window/document owns its own Radiant state, frame scheduling, event state, and network scheduler/session state.
- Avoid storing window-specific state behind `lambda_uv_loop()` globals. The shared loop may carry wakeup handles, but the callback should dispatch into explicit per-window/session objects.
- `uv_async_t` is the cross-thread wake primitive; `glfwPostEmptyEvent()` remains the GUI wake fallback.

#### 3.4.5 Headless / WebDriver mode

For `radiant/webdriver/` and CI test runs, the GUI loop variant should still call `uv_run(loop, UV_RUN_NOWAIT)` each tick, but skip `present()`. This keeps async resource loading working in headless tests.

#### 3.4.6 Frame budget tracking

Add a per-frame budget tracker that records: time in `glfwPollEvents`, in `uv_run`, in network completion delivery, in animation tick, in layout/paint. If any phase exceeds its budget (e.g., completion delivery > 4 ms), log a warning. Catches the case where too much resource processing leaks into a frame.

---

## 4. Network Resource Scheduler

### 4.1 Decision

Use a dedicated **NetworkScheduler** for Radiant resource loading. The final architecture is:

```text
libcurl owns HTTP.
Radiant owns document mutation and rendering.
libuv owns wakeups and embedder integration.
```

The current `uv_queue_work` + blocking `curl_easy_perform()` path is a valid bootstrap, but it should not be the final HTTP architecture. The target HTTP backend is a dedicated network thread running `curl_multi_*`, with browser-style scheduling, cancellation, per-origin limits, connection reuse, cache policy, and completion delivery back to the Radiant main thread.

### 4.2 Rationale

| Argument | Detail |
|----------|--------|
| Blocking downloads do not scale | `curl_easy_perform()` consumes one worker thread per in-flight resource. A page with many images, fonts, CSS files, and scripts can saturate a worker pool while most threads are simply waiting on sockets. |
| libuv's worker pool is global | `uv_queue_work` shares capacity with `uv_fs_*`, `uv_getaddrinfo`, JS async fs, and other subsystems. Long HTTP downloads should not starve unrelated runtime work. |
| HTTP has its own policy | Browsers need priority, per-origin caps, redirects, cookies, retries, cancellation, cache revalidation, connection reuse, and HTTP/2 multiplexing. These belong in a network scheduler, not in a generic worker pool. |
| curl multi is the right HTTP primitive | `curl_multi_*` drives many transfers from one network thread, reuses connections, supports HTTP/2 multiplexing, and exposes socket/timeouts for event-loop integration if needed later. |
| Radiant state is main-thread state | DOM, CSSOM/style, layout trees, `Item` graphs, and JS runtime state must be mutated on the owning Radiant/JS thread. Network threads produce bytes and metadata; main thread consumes them. |
| libuv is still useful | `uv_async_t`, `uv_timer_t`, and optional `uv_poll_t` integration are good glue for waking the main loop and integrating with embedders. They should not own resource-loading policy. |

### 4.3 Contract

```text
Radiant main thread
  ├── discovers resources
  ├── submits ResourceRequest objects
  ├── receives ResourceCompletion records
  ├── parses CSS/scripts and updates document state
  └── schedules reflow/repaint/frame work

NetworkScheduler
  ├── priority queues (CRITICAL > HIGH > NORMAL > LOW)
  ├── per-origin and global in-flight caps
  ├── cancellation by document/navigation/request id
  ├── cookie/cache/retry/redirect policy
  ├── completion queue
  └── wakeup bridge to Radiant (`uv_async_t` and/or `glfwPostEmptyEvent`)

HTTP backend, final
  └── dedicated network thread with `CURLM*` (`curl_multi_*`)

HTTP backend, transitional
  └── dedicated blocking-curl worker pool, not libuv's global pool

Decode/cache helpers
  ├── CPU decode pool for image/font/lottie/gif decode
  └── bounded cache pipeline for persistence and metadata updates
```

**Threading rules:**
- DOM, layout tree, CSS state, JS runtime state, and `Item` graphs are **never** mutated from a network or decode worker.
- HTTP workers/network thread operate on byte buffers, headers, status codes, cache paths, and simple metadata only.
- CSS parsing, script execution, style insertion, layout invalidation, and repaint scheduling run on the Radiant main thread or the owning JS/runtime thread.
- Completion delivery is an ownership transfer: `ResourceCompletion` is pushed to a thread-safe queue, then the main loop is woken.

**Cache I/O rule:**
- Cache persistence is part of the network scheduler, not the GUI loop. The default path is a bounded cache pipeline with a small in-flight limit, usually 2-4 writes.
- Large resource bodies should be streamed to cache files by the network/cache backend or written by cache workers. They should not be submitted as unbounded `uv_fs_*` work on the GUI/default loop.
- Small metadata reads/writes may use `uv_fs_*` on the default loop only when they are known to be cheap and their callbacks do not perform heavy work.
- The cache layer must expose backpressure: if the cache queue is full, keep the resource in memory briefly, skip persistence for low-priority entries, or apply an eviction policy.
- Cache write failure should degrade caching, not page loading. The resource load succeeds if the bytes were fetched and can be delivered to the document.

### 4.4 Target Architecture

#### 4.4.1 `NetworkScheduler`

Add a scheduler object owned by the browsing session or document loader:

```c
typedef struct NetworkScheduler NetworkScheduler;
typedef struct ResourceRequest ResourceRequest;
typedef struct ResourceCompletion ResourceCompletion;
typedef struct ResourceHandle ResourceHandle;

NetworkScheduler* network_scheduler_create(NetworkSchedulerConfig* config);
void network_scheduler_destroy(NetworkScheduler* scheduler);
ResourceHandle* network_scheduler_submit(NetworkScheduler* scheduler,
                                         const ResourceRequest* request);
bool network_scheduler_cancel(ResourceHandle* handle);
void network_scheduler_cancel_document(NetworkScheduler* scheduler,
                                       uint64_t document_id);
int network_scheduler_drain_completions(NetworkScheduler* scheduler,
                                        ResourceCompletionCallback cb,
                                        void* user_data);
```

The scheduler owns request queues, origin accounting, cancellation state, retry state, cache policy, and tracing. Radiant owns what the completion means for the document.

#### 4.4.2 HTTP backend: curl multi on a network thread

The final backend runs a single network thread with one `CURLM*`:

```text
network thread:
  1. pull dispatchable requests from scheduler queues
  2. attach each request to a `CURL*`
  3. drive `curl_multi_perform` / `curl_multi_poll`
  4. collect completed transfers
  5. push `ResourceCompletion`
  6. wake Radiant main thread
```

This gives many in-flight transfers without one thread per socket. It also makes HTTP/2 multiplexing and connection reuse normal rather than accidental.

#### 4.4.3 Transitional backend: dedicated blocking-curl pool

Before curl multi lands, move blocking `curl_easy_perform()` off libuv's global worker pool and into a dedicated network pool:

```text
network worker:
  1. dequeue highest-priority dispatchable request
  2. call `curl_easy_perform`
  3. write/cache bytes if needed
  4. push `ResourceCompletion`
  5. wake Radiant main thread
```

This preserves the current implementation style while gaining true priority, cancellation, per-origin caps, and isolation from JS/fs/DNS work.

#### 4.4.4 Completion delivery

Use `uv_async_t` when the Radiant loop is integrating libuv, and `glfwPostEmptyEvent()` as a GUI wake fallback:

```c
// network thread or cache/decode worker:
completion_queue_push(scheduler, completion);
uv_async_send(&scheduler->wake_async);

// Radiant main loop:
uv_run(lambda_uv_loop(), UV_RUN_NOWAIT);
network_scheduler_drain_completions(scheduler, deliver_completion, doc);
```

The async callback should be tiny: mark "network completions available" and wake the GUI loop. Heavy delivery still happens in a controlled point of the Radiant tick.

#### 4.4.5 Priority and concurrency

Use four FIFO queues or a small heap:

| Priority | Examples |
|----------|----------|
| CRITICAL | Main document, blocking redirects |
| HIGH | Render-blocking CSS, fonts needed for first layout |
| NORMAL | Images, SVGs, non-blocking styles |
| LOW | Prefetch, async scripts, speculative loads |

Add:

```c
typedef struct NetworkSchedulerConfig {
    int max_global_transfers;
    int max_transfers_per_origin;
    int max_cache_writes;
    int max_decode_jobs;
} NetworkSchedulerConfig;
```

Requests beyond the cap stay parked. When a transfer completes or is cancelled, the scheduler promotes the next eligible request.

#### 4.4.6 Cancellation

Cancellation is required for navigation, iframe removal, document destruction, and tests:

- If queued: remove the request and deliver a cancelled completion if the caller requested one.
- If in curl multi: remove the easy handle from the multi handle, clean it up, and deliver a cancelled completion.
- If in blocking-curl transition backend: set a cancel flag checked by `CURLOPT_XFERINFOFUNCTION`; return non-zero to abort.
- If post-download decode/cache is already running: let it finish and discard at delivery if the document/navigation id no longer matches.

#### 4.4.7 Decode pipeline

Decoding should be separate from HTTP policy:

- Network thread downloads bytes and metadata.
- Decode pool handles CPU-heavy image/font/animation decoding.
- Main thread receives either decoded paint-ready objects or metadata that is safe to install.
- If decoding can mutate Radiant structures, split it: worker decodes raw data, main thread creates/attaches Radiant objects.

#### 4.4.8 Cache pipeline

Cache writes are bounded and lower priority than visible rendering:

- Use a small cache queue (`max_cache_writes`, default 2-4).
- Prefer streaming response bodies directly to cache files for large resources, especially in the curl multi backend.
- Use cache workers or the network backend for large writes. Do not route large cache writes through the GUI loop's `uv_fs_*`.
- Keep cache metadata updates small, atomic, and separate from body persistence.
- If `uv_fs_*` is used for small cache metadata operations, its callback should only enqueue a completion or update cache bookkeeping; it should not parse resources, mutate documents, or do decode work.
- Failed cache writes should not fail the page load unless the resource itself is unavailable.

#### 4.4.9 Tracing and diagnostics

Record `(request_id, document_id, priority, origin, url, t_queued, t_started, t_headers, t_finished, bytes, status, cache_state, cancelled)` under a network trace flag. This is essential for debugging "why did first paint wait?" and "why did navigation retain old downloads?"

#### 4.4.10 TLS backend

When practical, point libcurl at the same TLS backend used by the server (`CURLSSLBACKEND_MBEDTLS`) to avoid two TLS stacks and unify certificate-store handling. This is an integration target, not a blocker for the scheduler refactor.

---

## 5. Implementation Plan

A suggested phasing that evolves the current network code toward the final architecture without requiring a flag-day rewrite.

| Phase | Scope | Files | Notes |
|-------|-------|-------|-------|
| P1 | `lib/file.c` hardening (Appendix A items 1–4) | `lib/file.c`, `lib/file_utils.c` | Pure improvements, no API change |
| P2 | Expose `uv_backend_timeout` / `uv_backend_fd` via `lib/uv_loop.h` | `lib/uv_loop.h`, `lib/uv_loop.c` | Enables P3 |
| P3 | Radiant idle-wake integration (§3.4.1) | `radiant/window.cpp` | Battery + responsiveness win |
| P4 | Main-thread resource delivery | `lambda/network/network_resource_manager.cpp`, `resource_loaders.cpp`, `radiant/window.cpp` | Workers produce `ResourceCompletion`; CSS/script/layout mutations move to Radiant main thread |
| P5 | `uv_async_t` / GUI wake bridge | `lambda/network/*`, `radiant/window.cpp`, `lib/uv_loop.*` | Wakes Radiant when completions arrive; removes 50 ms polling dependency |
| P6 | Introduce `NetworkScheduler` facade | `lambda/network/network_scheduler.*`, `network_resource_manager.*` | New API wraps current backend first; callers stop depending on `NetworkThreadPool` directly |
| P7 | Priority queues + per-origin/global caps | `lambda/network/network_scheduler.*` | Browser-quality scheduling independent of backend |
| P8 | Cancellation and document/navigation ids | `lambda/network/*`, `radiant/browsing_session.*` | Required for navigation and document teardown |
| P9 | Dedicated blocking-curl backend | `lambda/network/network_scheduler.*`, `network_downloader.*` | Moves long HTTP work off libuv's global worker pool while keeping `curl_easy_perform` |
| P10 | Bounded cache/decode queues | `lambda/network/*`, `radiant/surface.cpp`, font/image loaders | Prevents cache writes and CPU decode from stalling frames |
| P11 | curl multi network-thread backend | `lambda/network/curl_multi_backend.*` | Final HTTP architecture: many transfers, connection reuse, HTTP/2 multiplexing |
| P12 | Vsync animation source (§3.4.2) | `radiant/frame_clock.*`, `radiant/window.cpp`, `lambda/js/js_event_loop.*`, `radiant/script_runner.cpp` | Implemented: frame clock facade with native macOS/Windows/Linux pacing and rAF delivery from Radiant's frame loop, not libuv timers |
| P13 | `process.nextTick` queue + microtask audit (§2.4.1, §2.4.2) | `lambda/js/*`, `lib/uv_loop.*` | Implemented: separate nextTick queue, prepare/check JS task checkpoints, and ordering coverage |
| P14 | Handle leak detection + loop-thread assertions (§2.4.4, §2.4.5) | `lib/uv_loop.c`, debug builds only | Debugging quality |

---

## Appendix A — `lib/file.c` and `lib/file_utils.c` Hardening

Concrete sketches for the enhancements in §1.4. None introduce libuv; all are stdio-level.

### A.1 Drop `ftell`/`fseek` from `read_text_file` / `read_binary_file`

```c
// Replace:
//   fseek(file, 0, SEEK_END);
//   long fileSize = ftell(file);
//   rewind(file);
// With:
off_t fileSize = sb.st_size;   // already populated by fstat above
```

Benefit: correct for >2 GB files on 32-bit platforms; works on non-seekable streams (pipes, sockets) if the caller passed them; fewer syscalls.

### A.2 Atomic write on Windows: `GetTempFileNameA`

```c
#ifdef _WIN32
char dir[MAX_PATH], tmp[MAX_PATH];
GetFullPathNameA(filename, MAX_PATH, dir, NULL);
char* slash = strrchr(dir, '\\'); if (slash) *slash = '\0';
if (!GetTempFileNameA(dir, "atm", 0, tmp)) {
    log_error("write_text_file_atomic: GetTempFileNameA failed: %lu", GetLastError());
    return -1;
}
// ... write to tmp ...
if (!MoveFileExA(tmp, filename,
                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileA(tmp);
    return -1;
}
#endif
```

Avoids `_mktemp`'s TOCTOU race; `MOVEFILE_WRITE_THROUGH` ensures the rename is flushed.

### A.3 `fsync` before rename

```c
#ifndef _WIN32
fflush(f);
fsync(fileno(f));    // ensure data on disk before rename
#endif
fclose(f);
if (rename(tmp, filename) != 0) { ... }
```

Without `fsync`, a crash between `rename` and the page-cache flush can yield a present-but-empty file — disastrous for configs, save files, the build cache.

### A.4 Thread-safe temp counter

```c
#include <stdatomic.h>
static atomic_long s_temp_counter = 0;

char* file_temp_path(const char* prefix, const char* suffix) {
    long n = atomic_fetch_add(&s_temp_counter, 1) + 1;
    // ... snprintf using n ...
}
```

Required for Section 4's network/cache workers, which may call temp-path helpers concurrently.

### A.5 Bounded `dir_walk_recursive`

```c
typedef struct { char* path; } WalkFrame;

int dir_walk(const char* dir_path, FileWalkCallback cb, void* ud) {
    ArrayList* stack = arraylist_new(64);
    arraylist_append(stack, mem_strdup(dir_path, MEM_CAT_TEMP));
    int max_depth_seen = 0;

    while (stack->length > 0) {
        char* cur = (char*)arraylist_pop(stack);
        ArrayList* entries = dir_list(cur);
        if (!entries) { mem_free(cur); continue; }

        for (int i = entries->length - 1; i >= 0; i--) {  // reverse for DFS
            DirEntry* e = entries->data[i];
            char* full = file_path_join(cur, e->name);
            bool descend = cb(full, e->is_dir, ud);
            if (e->is_dir && descend) arraylist_append(stack, full);
            else mem_free(full);
        }
        // free entries, free cur
    }
    arraylist_free(stack);
    return 0;
}
```

Eliminates stack overflow on pathological trees; also makes cancellation trivial (early return).

### A.6 Symlink-loop guard

Maintain a `HashSet<(dev_t, ino_t)>` in the walk context; on each directory entry, `lstat` and refuse to descend if already visited. Matches `nftw(..., FTW_PHYS)` semantics.

### A.7 Document `MEM_CAT_TEMP` lifetime in `lib/file.h`

Add a header-level doc block:

```c
/*
 * Memory ownership conventions:
 *   - Functions returning `char*` allocate via mem_alloc(MEM_CAT_TEMP).
 *   - Returned buffers are valid until MEM_CAT_TEMP is reset.
 *   - Callers MUST NOT call free(); use mem_free() only if explicit early
 *     reclamation is needed.
 *   - Functions returning `const char*` (e.g. file_path_basename) return
 *     pointers into the caller's input — no allocation, no free.
 */
```

### A.8 Replace inline glob in `file_utils.c` Windows branch

Use `PathMatchSpecA` from `shlwapi.dll`:

```c
#ifdef _WIN32
  #include <shlwapi.h>
  #pragma comment(lib, "shlwapi.lib")
  bool match = PathMatchSpecA(base, ctx->name_pattern);
#else
  bool match = (fnmatch(ctx->name_pattern, base, 0) == 0);
#endif
```

Handles `*`, `?`, and `[abc]` correctly on both platforms.

### A.9 `file_cache_path` collision rate

```c
#include "xxhash.h"
uint64_t hash = XXH3_64bits(key, strlen(key));
snprintf(buf, need, "%s/%016llx%s", cache_dir, (unsigned long long)hash, ext);
```

At 1 M cache entries, DJB2-32 collision probability ≈ 11%; XXH3-64 ≈ 3 × 10⁻⁸. The cost difference is unmeasurable.

### A.10 Streaming reader

```c
typedef bool (*FileChunkCallback)(const char* chunk, size_t len, void* ud);

int read_file_streaming(const char* filename,
                        FileChunkCallback cb, void* ud) {
    FILE* f = fopen(filename, "rb");
    if (!f) return -1;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (!cb(buf, n, ud)) break;
    }
    fclose(f);
    return 0;
}
```

For large PDFs / datasets where pulling the entire file into `MEM_CAT_TEMP` is wasteful.

---

## Appendix B — Summary Decision Table

| Question | Answer |
|----------|--------|
| Migrate `lib/file.c` to `uv_fs_*`? | **No** — keep synchronous, stdio-based. |
| Roll our own loop for JS / Node? | **No** — libuv stays. |
| Should the GUI loop be libuv-driven? | **No** — GUI loop is host; libuv runs `UV_RUN_NOWAIT` each tick. |
| Should animations use `uv_timer_t`? | **No** — vsync / display-link. |
| Should network downloads use libuv? | **Partly** — use libuv for wakeups/integration; final HTTP scheduling belongs to `NetworkScheduler` + curl multi. |
| Custom network scheduler *and* libuv? | **Yes** — dedicated scheduler for HTTP/cache/decode policy, libuv for `uv_async_t` wakeups and optional `uv_poll_t` integration. |
| TLS stack? | Standardize on **mbedTLS** for server, JS, and libcurl. |

---

*End of proposal.*
