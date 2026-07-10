# Lambda JS Threading Proposal

## Current Status

LambdaJS currently has two materially different CLI execution paths:

- Normal JS CLI execution (`lambda.exe js ...`) runs non-UI JS through a helper that creates a
  pthread with a 256 MB stack, then calls `transpile_js_to_mir_len()` on that thread.
- JS batch execution (`lambda.exe js-test-batch`) runs its whole stdin-driven batch loop on the
  process/main thread, so each script uses the process default stack.

The normal JS CLI helper exists because modern JavaScript workloads can be stack-hungry:

- deeply recursive user code, such as `test/js/tco.js`;
- generated validator/library code, such as Ajv's `new Function` validator paths;
- debug builds and MIR/JIT helper frames, which add native call depth.

Recent observations:

- `test/js/tco.js` passes through focused JS gtest/direct JS CLI execution, but crashes in
  `js-test-batch` around the non-tail-recursive Ackermann stress case.
- `test/js/lib_ajv.js` now passes direct JS CLI and focused gtest after fixing heap teardown order,
  but still crashes in `js-test-batch` before completing.
- Focused JS gtests now skip unrelated batch warm-up scripts, so hostobj/DOM-focused work no longer
  inherits these batch-only stack crashes.

The remaining issue is therefore not that these scripts are inherently broken. The issue is that
`js-test-batch` does not run on the same JS execution stack as ordinary JS CLI execution.

## Proposal

Adopt this invariant:

> All non-UI JavaScript execution entrypoints run on the JS execution stack.

For the current CLI/test surface, that means:

1. Keep the existing 256 MB JS execution-stack wrapper for `lambda.exe js ...`.
2. Change `js-test-batch` so the whole batch worker loop runs inside one pthread with the same
   256 MB stack.
3. Keep the batch loop continuous inside that one worker thread. Do not spawn one thread per script.

The batch-loop shape should remain:

- one process;
- one batch worker thread;
- one stdin/stdout protocol loop;
- hot heap/preamble reuse within that worker;
- existing per-script crash recovery and reset logic within the worker.

This keeps batch behavior close to today's design while giving batch scripts the same stack budget
as ordinary JS CLI scripts.

## Why Not Set The Main Thread Stack?

Setting the process/main thread stack to 256 MB is less portable and less controllable:

- macOS main-thread stack is effectively fixed at process creation/link/load time.
- Linux behavior depends on inherited `ulimit -s` / `setrlimit`, and the main stack already exists
  by the time `main()` runs.
- Windows uses linker `/STACK` style configuration.

A dedicated JS execution thread is more explicit: the stack size belongs to the JS runtime boundary,
not to the whole process.

## Caveat 1: Crash Recovery Signals

`js-test-batch` currently catches synchronous crash signals such as `SIGSEGV`/`SIGBUS` with
`sigsetjmp`/`siglongjmp`.

Running the batch loop on a worker thread should still work for synchronous faults: the signal is
delivered to the thread that faults, and the batch crash handler can recover inside that same worker
thread.

Required invariant:

- install and use the crash recovery state from the batch worker thread;
- keep `batch_crash_jmp`, `batch_crash_active`, and related state scoped to the worker's execution
  path;
- do not let the main waiting thread participate in per-script crash recovery.

## Caveat 2: Timeout Signals

`js-test-batch` also uses `alarm()`/`SIGALRM` for per-script timeouts. This is trickier than crash
signals because `SIGALRM` is process-directed and can be delivered to any thread that has not blocked
it.

If the batch loop moves to a worker thread, timeout delivery must be made deterministic.

Initial implementation option:

- block `SIGALRM` in the main waiting thread before starting the batch worker;
- leave `SIGALRM` unblocked in the batch worker;
- install the timeout handler from the batch worker thread;
- restore signal masks after the worker exits.

Longer-term option:

- replace `alarm()` with a timeout mechanism that targets the batch worker explicitly or avoids
  process-global signals.

The first option is smaller and matches the current implementation style. The second option is safer
for future multi-threaded JS/runtime work.

## Expected Result

After this change:

- direct JS CLI, focused JS gtests, and `js-test-batch` use the same JS stack budget;
- `tco.js` and `lib_ajv.js` should no longer appear as batch-only crash-recovery scripts merely due
  to process default stack limits;
- future stack-heavy JS libraries should behave more consistently across CLI and test runners.

This does not replace proper JS stack-overflow hardening. Eventually, runaway JS recursion should
raise a JS `RangeError` rather than depending on native stack exhaustion. The threading proposal is
the immediate consistency fix.
