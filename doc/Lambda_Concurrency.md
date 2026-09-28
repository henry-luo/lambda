# Lambda Concurrency

Lambda procedures run concurrently as **tasks**: lightweight children started with `start`, awaited with `wait`, and connected by bounded mailboxes. Concurrency is structured — every task belongs to the block that started it — and colorless: there are no `async`/`await` keywords, and a call that suspends looks like any other call (S13).

> **Related Documentation**:
> - [Lambda Procedural](Lambda_Procedural.md) — `pn`, `var`, `while`, and the rule that concurrency lives in procedures
> - [Lambda Error Handling](Lambda_Error_Handling.md) — `T^E`, postfix `^`, and the statement handler used below
> - [Lambda System Functions](Lambda_Sys_Func.md) — the other built-in functions
> - [LambdaJS](JS_DOM_Support.md) — the JavaScript engine that shares the event loop

---

## Table of Contents

1. [Overview](#overview)
2. [Starting and Waiting](#starting-and-waiting)
3. [Colorless Calls](#colorless-calls)
4. [Structured Scope](#structured-scope)
5. [Messages](#messages)
6. [Waiting for the First Result](#waiting-for-the-first-result)
7. [Timeouts and Cancellation](#timeouts-and-cancellation)
8. [The Capture Rule](#the-capture-rule)
9. [Asynchronous File Reads](#asynchronous-file-reads)
10. [JavaScript Interoperability](#javascript-interoperability)
11. [Function Reference](#function-reference)
12. [Implementation Status](#implementation-status)

---

## Overview

- Concurrency is available only inside a `pn`: `start` and the other task operations are built-in procedures, and calling one from `fn` context is error E224 (S13.1.1v2).
- A task is started with `start(target, args, options)` and yields an opaque **handle**. Handles compare by identity; only the concurrency built-ins operate on them (S13.1.3v2).
- Tasks share nothing mutable: a started procedure may not capture a `var`, and tasks communicate by message and by immutable values (S13.1.4).
- Failures are values: waiting on a task yields its result or its error, including cancellation and timeouts (S13.1.5).
- All tasks run cooperatively on one event loop, shared with JavaScript Promises. Examples below are run with `lambda run file.ls`, which calls `main()` and prints its result.

## Starting and Waiting

`start(target, args = [], options = {})` enqueues a child procedure without blocking the caller. `target` must be a `pn`, and `args` is an array of its arguments. `wait(handle)` parks until the child finishes and yields its value, or its error on the `^` channel:

```lambda
pn child(value) {
    sleep(1)^
    return value + 1
}

pn main() {
    let handle = start(child, [41])
    wait(handle)^
}
```

```text
42
```

`sleep(ms)` parks the current task on the shared timer queue. Every operation that can fail — `wait`, `sleep`, `send`, `receive`, `select` — returns `T^E`, so the caller engages the error with `^` or one of the other forms in [Timeouts and Cancellation](#timeouts-and-cancellation).

## Colorless Calls

A procedure that suspends is called like any other: the compiler finds the procedures that can park and turns them into resumable state machines, so nothing at the call site changes (S13.1.2v2). A direct call through several ordinary `pn` frames can suspend without any annotation:

```lambda
pn scaled(n) { sleep(n)^; return n * 10 }
pn both() { scaled(1) + scaled(2) }

pn main() { both() }
```

```text
30
```

## Structured Scope

A started handle is owned by the nearest lexical block (S13.3.1):

- **Normal exit joins.** When the block ends, it waits for its children.
- **Error exit cancels, then joins.** When an error leaves the block, its children are cancelled first.
- **Returning a handle transfers it** to the caller. Storing or sending a handle grants the capability to wait on it or send to it, not ownership.

```lambda
pn noisy(n) {
    sleep(n)^
    print("child " ++ n ++ "\n")
    return n
}

pn main() {
    {
        let h = start(noisy, [5])
        print("block end\n")
    }
    print("after block\n")
}
```

```text
block end
child 5
after block
```

The error exit cancels the child before it finishes:

```lambda
pn child() {
    sleep(50)^
    print("child finished\n")
    return 1
}

pn failing() int^ {
    let h = start(child)
    raise error("boom")
}

pn main() {
    failing() ^ { print("caught: " ++ ^.message ++ "\n") }
    "done"
}
```

```text
caught: boom
"done"
```

At the end of a run, `lambda run` drains any task still pending; `--no-drain` returns without waiting.

## Messages

Every task has one **mailbox**, addressed by its handle. `send(handle, value)` appends to it without blocking, and `receive()` removes the oldest message, parking while the mailbox is empty. There is no channel type and no selective receive: dispatch on the message with `match` (S13.2.1).

```lambda
pn worker() {
    let message = receive()^
    return "done: " ++ message
}

pn main() {
    let handle = start(worker)
    send(handle, "job")^
    wait(handle)^
}
```

```text
"done: job"
```

A task that serves many messages loops over `receive()` and stops on a message it recognizes:

```lambda
pn server() {
    var total = 0
    var running = true
    while (running) {
        let msg = receive()^
        match msg {
            case {op: 'add'} { total = total + msg.n }
            case {op: 'stop'} { running = false }
            default { }
        }
    }
    return total
}

pn main() {
    let h = start(server)
    send(h, {op: 'add', n: 2})^
    send(h, {op: 'add', n: 3})^
    send(h, {op: 'stop'})^
    wait(h)^
}
```

```text
5
```

Messages from one sender arrive in the order sent, and a task's completion becomes visible only after the messages it sent before finishing (S13.2.3).

**Backpressure is an error value.** A mailbox holds 1024 messages. `send` never blocks and never drops: when the mailbox is full it returns error 320, *task mailbox full* (S13.2.2).

```lambda
pn idle() { sleep(2000)^; return 0 }

pn main() {
    let h = start(idle)
    var i = 0
    while (i < 1100) {
        let r: null | error = send(h, i);
        if (r is error) { cancel(h); return [i, r.code, r.message] }
        i = i + 1
    }
    null
}
```

```text
[1024, 320, "task mailbox full"]
```

## Waiting for the First Result

`select(h1, h2, …)` parks until one of the handles completes and yields that handle; ties are resolved in readiness order. It accepts `timeout: ms` like `wait`.

```lambda
pn slow(ms, tag) { sleep(ms)^; return tag }

pn main() {
    let a = start(slow, [50, "a"])
    let b = start(slow, [5, "b"])
    let first = select(a, b)^;
    [first == b, wait(first)^, wait(a)^]
}
```

```text
[true, "b", "a"]
```

## Timeouts and Cancellation

`wait(handle, timeout: ms)` gives up after `ms` milliseconds with error 310, *task wait timed out*. The timeout applies to the **waiter** only: the task keeps running and can still be awaited (S13.3.2).

```lambda
pn slow() { sleep(200)^; return "late" }

pn main() {
    let h = start(slow)
    let early: string | error = wait(h, timeout: 10);
    [early.code, early.message, wait(h)^]
}
```

```text
[310, "task wait timed out", "late"]
```

`cancel(handle)` requests cancellation. Any holder of the handle may cancel, and cancelling twice is harmless. Cancellation is cooperative: the task observes it at its next park point (`sleep`, `wait`, `receive`, `select`, `io.read`), and waiting on it yields error 319, *task cancelled*.

```lambda
pn forever() { sleep(10000)^; return 1 }

pn main() {
    let h = start(forever)
    cancel(h)
    let r: int | error = wait(h);
    [r.code, r.message]
}
```

```text
[319, "task cancelled"]
```

Because the task operations are procedures, their errors are engaged in one of three ways: propagate with postfix `^`, receive the outcome in a binding typed `T | error` as above, or handle it with a **statement-position handler**, which runs its body on error and continues with the next statement (S7.6.7v3):

```lambda
pn forever() { sleep(10000)^; return 1 }

pn main() {
    let h = start(forever)
    cancel(h)
    wait(h) ^ { print("wait failed: " ++ ^.message ++ "\n") }
    "continued"
}
```

```text
wait failed: task cancelled
"continued"
```

Propagation lets a caller decide:

```lambda
pn slow() { sleep(200)^; return "late" }

pn quick_or_fail() string^ {
    let h = start(slow)
    wait(h, timeout: 10)^
}

pn main() {
    quick_or_fail() ^ { print("gave up: " ++ ^.message ++ "\n") }
    "done"
}
```

```text
gave up: task wait timed out
"done"
```

A value-producing handler over a procedure call, such as `let r = wait(h) ^ { 0 }`, is not one of the forms: a `pn` handler is statement-only (S7.6.7v3).

> **Not yet implemented.** The compiler does not yet reject a value-producing handler over a procedure call; it compiles, and the binding receives `null` whether the call succeeds or fails. Use one of the three forms above.

## The Capture Rule

A started procedure may not capture a `var` of the procedure that starts it: tasks share only immutable values and messages, so the number of threads behind them is never observable (S13.1.4). Pass the value as an argument, copy it to a `let`, or send it:

```lambda error=E221
pn main() {
    var n = 1
    pn inner() { return n + 1 }
    let h = start(inner)
    wait(h)^
}
```

The compiler reports `` `start` cannot capture mutable var 'n'; copy it to a `let` value or use message passing ``. Passing the value works:

```lambda
pn worker(v) { return v * 2 }

pn main() {
    var mutable = 21
    let handle = start(worker, [mutable])
    wait(handle)^
}
```

> **Not yet implemented.** A procedure arrow passed directly to `start`, as in `start(pn () => { return n + 1 })`, is not yet checked by the capture rule. It captures a snapshot of `n`, so it cannot observe later writes, but it should be rejected like the named procedure above.

## Asynchronous File Reads

`io.read(target)` reads a local file through the event loop, parking the task instead of blocking the other tasks. It returns the file's contents as a string, or error 401 when the file does not exist:

```lambda
pn main() {
    let text = io.read("notes.txt")^;
    [len(text), split(trim(text), "\n")]
}
```

With `notes.txt` holding two lines, `hello` and `world`:

```text
[12, ["hello", "world"]]
```

## JavaScript Interoperability

Lambda tasks and JavaScript Promises share one event loop (S13.1.2v2):

- A Lambda `pn` exported to JavaScript is a Promise-returning function there.
- Lambda can `wait` on a Promise imported from a JavaScript module; fulfillment resumes the waiting task and rejection becomes a Lambda error value.
- Promise reactions run as JavaScript microtasks; Lambda task resumes run afterward, at macrotask position.
- `toPromise(handle)` adapts a Lambda handle for JavaScript. It needs an initialized JavaScript runtime and otherwise fails with error 502.

See [Lambda_Modules.md](Lambda_Modules.md) for importing JavaScript modules and [JS_DOM_Support.md](JS_DOM_Support.md) for the engine.

## Function Reference

| Function | Result | Behavior |
|----------|--------|----------|
| `start(target, args = [], options = {})` | handle | Start a child procedure without blocking. `target` is a `pn`, `args` an array. `options.mode` is `'task'` (default); `'thread'` and `'process'` are compile error E501, *not implemented yet*. |
| `wait(handle)` | `T^E` | Park until the task finishes; yield its value or its error. |
| `wait(handle, timeout: ms)` | `T^E` | As above, but give up after `ms` with error 310; the task keeps running. |
| `select(h1, h2, …, timeout: ms)` | `handle^E` | Park until one handle completes and yield it. |
| `send(handle, value)` | `null^E` | Append to the task's mailbox; error 320 when the mailbox (1024 messages) is full. |
| `receive()` | `item^E` | Remove the oldest message from the current task's mailbox, parking while it is empty. |
| `sleep(ms)` | `null^E` | Park on the shared timer queue. |
| `self()` | handle | The current task's handle. |
| `cancel(handle)` | `null` | Request cancellation; idempotent. The task observes it at its next park point. |
| `io.read(target)` | `string^E` | Read a local file without blocking other tasks. |
| `toPromise(handle)` | JS Promise | Adapt a handle for JavaScript; needs a JavaScript runtime (error 502 otherwise). |

Error codes: 310 *task wait timed out*, 319 *task cancelled*, 320 *task mailbox full* (see [Lambda_Error_Handling.md](Lambda_Error_Handling.md#error-code-categories)).

## Implementation Status

| Feature | Status |
|---|---|
| Tasks, mailboxes, `select`, timeouts, cancellation, structured scope | Implemented (S13.1–S13.3) |
| Isolated workers: `mode: 'thread'` and `mode: 'process'` | Not yet implemented — compile error E501 (S13.1.3v2) |
| Pairwise, bit-reproducible numeric reductions | Not yet implemented (S13.4.1) |
| Parallel stream pipelines | Not yet implemented; streams themselves are pending (S13.4.2, S14.3) |
| Value-producing handler over a procedure call | Should be a compile error (S7.6.7v3); currently compiles and yields `null` |
| Capture rule for procedure arrows passed to `start` | Not yet enforced (S13.1.4) |
