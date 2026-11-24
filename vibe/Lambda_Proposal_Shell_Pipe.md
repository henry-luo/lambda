# Live subprocess pipelines in Lambda

**Status** · Proposal for discussion, 2026-11-23

**Scope** · Connect the live stdout of one external program to the stdin of
the next. Add a procedural API using the existing `|>` syntax.

**Spec linkage** · Whole-value application follows **S10.1.2v5**. Process
execution belongs in `pn` under **S12.1.1v5** and uses the I/O boundary in
**D11.1.2**. The later stream form follows **S14.3.1v3–S14.3.3**.
[Formal semantics](../doc/Lambda_Formal_Semantics.md#s101-union-pipe-and-filter) ·
[Formal design](../doc/Lambda_Formal_Design.md#d111-trust-and-io)

## First release

Add `io.pipeline(stages, options?)`. Each stage is a map with `program` and
`args`. Use the same explicit argument arrays as `io.exec`. The new procedure
launches the programs directly and connects adjacent stages with OS pipes.
This example expresses Bash's `sort data.txt | uniq -c` with the proposed API.

```lambda
pn main() {
    let result = ([
        {program: "sort", args: ["data.txt"]},
        {program: "uniq", args: ["-c"]}
    ] |> io.pipeline({timeout_ms: 30000}))^
    result
}
```

Under **S10.1.2v5**, `stages |> io.pipeline(options)` calls
`io.pipeline(stages, options)`. The left operand is ordinary data. The
procedure owns execution and the live byte connections. The current
`io.exec` waits for one program, captures its output, and gives it empty
stdin. A chain of `io.exec` calls would therefore wait for each program to
finish before the next starts. [Lambda `io.exec`](../doc/Lambda_Sys_Func.md#ioexecprogram-args-options)

### Execution and results

- Validate the stage list and options before launching a child. In the
  first release, give the first stage empty stdin, matching `io.exec`.
  Close the parent's copies of the pipe ends as soon as they are no longer
  needed so EOF and broken-pipe behavior reach the right children.
- Run the stages concurrently. Let OS pipes carry intermediate bytes and
  provide backpressure. Drain the final stdout and every stage's stderr
  concurrently, including after a capture limit is reached. This lets a
  noisy stage finish even when its retained diagnostics are truncated.
- Return the final stdout, an ordered array of per-stage exit statuses and
  stderr captures, and timeout and truncation indicators. Preserve raw bytes,
  including NUL and trailing newlines. Follow **S2.2.2v2** for empty captures.
  A child that exits nonzero reports its status in the result, as in
  `io.exec`. Invalid input and launch failures raise Lambda errors.
- Use one deadline for the pipeline. On timeout, task cancellation, or a
  partial launch failure, close the pipes, terminate remaining children,
  and reap every child before returning or propagating the error. This
  follows the ownership and cancellation model in **S13.3.1v2–S13.3.2**.
- Treat an expected broken pipe after a downstream stage completes early as
  normal upstream shutdown. `yes | head -n 1` should succeed. Keep the
  upstream termination visible in its stage result so callers can
  distinguish it from an ordinary successful exit. The existing raw-byte
  process proposal covers this cleanup under **S14.3.3** and **PL8**.

### Implementation and verification

Reuse the native process backend and `io.exec` argument validation where
applicable. Put pipe creation, descriptor or handle inheritance, and process
cleanup in the process backend. Apply the existing process execution policy
under **D11.1.2**. Root Lambda values under **D6.3.3** while copying process
inputs into native-owned storage before parking the task.

Cover a transfer larger than the OS pipe buffer, exact binary output,
concurrent stderr draining past the capture limit, early downstream exit,
a failed launch after another stage has started, timeout, and task
cancellation. Check that each failure path reaps the children it started.

## Later stream composition

After the general stream executor is available, expose the same process
backend as a stage over a live binary stream. One possible spelling is

```lambda
source |> io.through("sort", []) |> io.through("uniq", ["-c"]) |> collect
```

The stream value carries the plan and the terminal starts execution under
**S14.3.1v3**. A live process stream is one-shot and `pn`-only under
**S14.3.2**. The forcing scope owns the children and cancels upstream work
when a downstream consumer stops under **S14.3.3**. Byte-to-record adapters
can let ordinary Lambda mapping and filtering sit between external commands.
The stream form builds on the existing raw-byte process proposal, whose
spawn API is still open (**O-PL2**).
[Stream design and status](../doc/Lambda_Formal_Semantics.md#s143-streams-and-laziness) ·
[Raw-byte subprocess proposal](Lambda_Design_Pipeline.md#6-layer-0-raw-byte-process-spawn-pl8--the-real-cli-pipeline-enabler)
