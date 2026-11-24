# Shell pipelines in Lambda

**Status** · Working design, 2026-11-24. The reusable `pn` pipeline, symmetric file endpoints, `sh.stdin()`, binding of live stdout, and failure on an unexpected exit in any stage are adopted. The package, live process runtime and file sink await implementation. The Bash and PowerShell modules are deferred.

**Scope** · Compose external executables and structured Lambda operations with the existing `|>`. A procedure can produce output that its caller continues to pipe.

**Spec linkage** · **S10.1.2v6** governs pipe application, **S10.1.4v2** defines the file sink, **S12.1.1v5** governs procedural effects, and **S14.3.1v3–S14.3.3** govern streams, one-shot I/O and early termination. **D9.3.4** places shipped packages under `lambda.*`. **D11.1.2** governs process and file access. [Formal semantics](../doc/Lambda_Formal_Semantics.md) · [Formal design](../doc/Lambda_Formal_Design.md)

## A procedure is a reusable pipeline

```lambda
import sh: lambda.shell

pn some_piping(input) {
    input |> sh.sort |> sh.uniq(count: true)
}
pn main() {
    \.'data.txt' |> some_piping |> sh.head(lines: 10)
}
```

The file supplies bytes to `sort` on stdin. The sorted lines reach `uniq -c`, which emits a count for each run of equal lines. The caller adds `head -n 10` and the CLI writes those first ten output lines. Returning from `some_piping` passes the live output and its process ownership to the caller. Waiting for `uniq` or collecting its output at that return would defeat the caller's ability to stop it early.

### Starting with a command

```lambda
pn main() {
    sh.ls() |> sh.head(lines: 10)
}
```

Use **`sh.ls()` at the start**. The empty argument list invokes a source command. It receives empty stdin by default and can begin producing output immediately. Bare `sh.ls` is a reference to the exported procedure. Consequently, `sh.ls |> sh.head(lines: 10)` would pass that procedure as the input to `sh.head`, which expects a byte source. On the right, `input |> sh.sort` already means `sh.sort(input)`. These spellings follow ordinary Lambda calls and whole-value application under **D7.2.1** and **S10.1.2v6**.

To use the Lambda script itself as a command-line filter, take its inherited stdin explicitly. The caller can supply a file, another process, or a terminal.

```lambda
pn main() {
    sh.stdin() |> some_piping
}
```

`sh.stdin()` exposes the script's existing standard input as a one-shot byte source. A read consumes bytes from that input. A second call cannot replay them (**S14.3.2**).

### Inputs and command arguments

The first parameter of a filter is its pipeline input. A local path in that position supplies file bytes to stdin. Under **S2.4.1v3** and **S10.1.2v6**, `\.'data.txt' |> sh.sort` and `sh.sort(\.'data.txt')` express the same call. A program that needs a filename *operand* receives it in its argument list. For example, a proposed generic filter `source |> sh.cmd("grep", ["-f", "patterns.txt"])` sends `source` to stdin and lets `grep` open the separate pattern file.

Named wrappers translate documented Lambda arguments to individual executable arguments. For example, `count: true` supplies `-c` to `uniq`, and `lines: 10` supplies `-n` and `10` to `head`. A generic command takes an ordered argument array for other programs, repeated flags and operands. Each argument retains its boundary, including spaces. The host must provide a compatible executable. Importing the package selects neither a command interpreter nor a particular vendor's utilities.

### Routing one command's stderr (proposed)

Use a named `stderr` argument on the command that produces the diagnostics. Put positional arguments first. The process launcher consumes the option before it starts the executable. For example, this would write Git diagnostics to `errors.log` and send only stdout to `head`.

```lambda
sh.cmd("git", ["status", "--short"], stderr: \.'errors.log') |> sh.head(lines: 10)
```

The same option should work on named wrappers, for example `sh.sort(source, stderr: dest)` when `dest` holds a local file path. A path would create or truncate the file under the usual output policy (**D11.1.2**, **D11.2.2**). If opening it fails, report the error before starting that child and clean up any upstream processes. Routing diagnostics leaves the command's exit status visible. A future merge option could connect stderr to the same OS pipe as stdout at launch. Its spelling is open.

### Writing stdout to a file

File endpoints are symmetric. A path at the start supplies the first command's stdin. A path at the end receives the last command's stdout. The same reusable procedure connects them.

```lambda
pn main() {
    \.'data.txt' |> some_piping |> \.'output.txt'
}
```

The destination may also be a variable. With `let dest = \.'output.txt'`, `sh.ls() |> dest` means `output(sh.ls(), dest)`. Dispatch depends on the values at the connection, so live output returned through a procedure behaves like output returned directly by a command. The rule applies only in procedural code, with live process stdout on the left and a local file path on the right. An ordinary record piped to a path, or live stdout piped to a plain filename string, still fails the whole-value callable check under **S10.1.2v6**. A quoted path step keeps the dot in a filename under **S2.4.1v3**.

Under **S10.1.4v2** the sink has the same result and error behavior as explicit `output`. It drains stdout incrementally, writes its exact bytes regardless of the filename extension, and keeps stderr separate. It waits for cleanup and returns the byte count on a successful write. Use `(sh.ls() |> dest)^` to propagate a write error. A sink failure closes the pipe and reaps upstream children. File writes use the existing I/O policy under **D11.1.2** and **D11.2.2**. [Procedural file output](../doc/Lambda_Procedural.md#file-output)

To append, use the existing output option.

```lambda
(sh.ls() |> output(\.'output.txt', {mode: "append"}))^
```

The original general file write and append operators were retired when `|>` became the pipe. The adopted file sink covers the live process case. **SO29** still tracks general file write and append syntax. [Historical ruling C6a](Lambda_Semantics_Formal2.md#c6a-pending-file-writeappend-syntax) · [Formal pipe rules](../doc/Lambda_Formal_Semantics.md#s101-union-pipe-and-filter)

## Internal and external commands

| Connection | What crosses it |
|---|---|
| Internal → internal | Lambda values. A record keeps its fields and types, and a sequence can feed the existing mapping and filtering operations. |
| External → external | An OS pipe connects stdout to the next stdin. Bytes, including NUL and trailing newlines, pass through exactly. Every stage keeps a separate stderr channel and exit status. |
| External → internal | An explicit decoder frames the bytes as lines or parses a format such as JSON. The following Lambda stage can then select fields or compute on values. |
| Internal → external | An explicit encoder chooses the byte representation of structured input, such as UTF-8 lines or JSON. A binary source already supplies bytes. |

For example, external `sh.ls()` produces the listing format of the installed executable. Code that needs entries with a name, size and type should consume structured directory data. Splitting a display listing on whitespace loses information about filenames.

### Nushell as prior art

Nushell makes both kinds of pipeline available in one language. Its internal commands exchange values, and user-defined commands can accept and return streaming input. The caret selects an external executable even when a built-in shares the name. In Nu, `^sort data.txt | ^uniq -c` connects two external processes. In `open --raw data.txt | lines | uniq --count`, the built-in counts distinct values across the input and returns rows with `value` and `count` fields. Sorting is optional for that frequency table. Lambda adopts the distinction through an imported `sh` namespace and explicit byte adapters. [Nushell pipelines](https://www.nushell.sh/book/pipelines.html) · [External commands](https://www.nushell.sh/book/running_externals.html) · [Custom commands](https://www.nushell.sh/book/custom_commands.html) · [Built-in uniq](https://www.nushell.sh/commands/docs/uniq.html)

## Execution and completion

Calling an external `pn` starts its child and returns a live, one-shot process output as soon as the child is connected. A subsequent stage takes that output as its stdin. A consumer may still be attaching when a producer fills the OS pipe. The producer can block while the calling Lambda task continues to attach the next stage. This is an effectful process launch under **S12.1.1v5** and **PL8**. Wrapping an already-running handle in a general `stream(handle)` retains the terminal-forcing rule of **S14.3.1v3–S14.3.3**. [Raw-byte process design](Lambda_Design_Pipeline.md#6-layer-0-raw-byte-process-spawn-pl8--the-real-cli-pipeline-enabler)

An ordinary binding such as `let revision = sh.cmd("git", ["rev-parse", "HEAD"])` holds the live stdout from that invocation. It can be piped onward once. To obtain a revision string, a consumer must read the bytes and decode the text. Removing the trailing line ending is a separate choice. The capture and decoding API remains open.

The proposed CLI consumes a live output returned by `main`, writes the final stdout bytes, reports stderr separately, and waits for completion. A programmatic consumer can drain the output and inspect ordered per-stage statuses and diagnostics. This gives the ordinary pipeline a completion point without requiring a plan variable or a separate `sh.run`. Julia offers the explicit-plan alternative with `run(pipeline(...))`. [Julia external programs](https://docs.julialang.org/en/v1/manual/running-external-programs/)

If a later launch fails, the owner closes open pipe ends, terminates outstanding children and reaps them. Timeout, cancellation and abandoned output also require cleanup. Stderr must drain concurrently even when retained diagnostics reach their capture limit. A successful early consumer such as `head` can cause an expected upstream broken pipe. Preserve that stage status and allow the pipeline to complete successfully. Under **S14.3.4**, any unexpected stage failure makes pipeline completion fail, even when a later stage succeeds or a file sink writes all the bytes it receives. A terminal surfaces the failure after draining and cleanup. The CLI reports a failed pipeline with a nonzero exit, and ordered stage statuses remain available for diagnosis. The completion API and the choice of numeric CLI status when several stages fail remain open. [Task and stream ownership, S13.3.1v2–S13.3.2 and S14.3.3](../doc/Lambda_Formal_Semantics.md) · [Original shell pipe proposal](Lambda_Proposal_Shell_Pipe.md)

## Deferred interpreter modules

The initial package is `lambda.shell` for direct executable calls. Both `lambda.shell.bash` and `lambda.shell.powershell` are deferred. If a later design introduces an interpreter module, the existing import rules can express `import sh: lambda.shell.bash`. The alias is local to the importer under **D9.3.2–D9.3.4**.

There is little benefit in selecting Bash to run external `sort` or `uniq`. The executable supplies their options and behavior. Interpreter selection matters for Bash built-ins and expansions, or for a PowerShell pipeline of cmdlets that pass objects within one PowerShell session. On Windows, for example, PowerShell's `ls` can resolve to the `Get-ChildItem` alias. That is a different operation from launching an external `ls`. [PowerShell aliases](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_aliases)

If added, an interpreter module should expose an explicit script invocation that runs a complete script in one interpreter process. Input and output at the Lambda boundary still use bytes. A PowerShell script can choose a serialization such as JSON when returning objects to Lambda. Ordinary `sh.*` arguments continue to pass directly to executables. This leaves interpreter choice local to the stage that needs it and avoids reparsing argument data as shell source.

## Appendix · Runtime dependency

The earlier proposal's `io.pipeline(stages, options?)` describes a batch of known stages. Its pipe creation, concurrent draining, cancellation and status collection can share a native backend with this package. The reusable `pn` additionally requires incremental attachment and ownership transfer across procedure returns. Today's `io.exec` waits and captures one child with empty stdin. The decisive integration case is a producer larger than the pipe buffer returned through a helper to an early-exiting consumer, with every child subsequently reaped. [Original proposal](Lambda_Proposal_Shell_Pipe.md) · [Current io.exec contract](../doc/Lambda_Sys_Func.md#ioexecprogram-args-options)
