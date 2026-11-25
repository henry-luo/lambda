# Shell pipelines in Lambda

**Status** · Working design, 2026-11-25. The reusable `pn` pipeline, symmetric file endpoints, `sh.stdin()`, per-item stdin encoding, binding and per-line processing of live stdout, and failure on an unexpected exit in any stage are adopted. The macOS host implements live byte pipelines and deferred per-item stages on the interpreter and MIR JIT. The Bash and PowerShell modules are deferred.

**Scope** · Compose external executables and structured Lambda operations with the existing `|>`. A procedure can produce output that its caller continues to pipe.

**Spec linkage** · **S10.1.2v7** governs pipe application, **S10.1.4v2** defines the file sink, **S12.1.1v5** governs procedural effects, and **S14.3.1v3–S14.3.3** govern streams, one-shot I/O and early termination. **D9.3.4** places shipped packages under `lambda.*`. **D11.1.2** governs process and file access. [Formal semantics](../doc/Lambda_Formal_Semantics.md) · [Formal design](../doc/Lambda_Formal_Design.md)

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

Use **`sh.ls()` at the start**. The empty argument list invokes a source command. It receives empty stdin by default and can begin producing output immediately. Bare `sh.ls` is a reference to the exported procedure. Consequently, `sh.ls |> sh.head(lines: 10)` would pass that procedure as the input to `sh.head`, which expects a byte source. On the right, `input |> sh.sort` already means `sh.sort(input)`. These spellings follow ordinary Lambda calls and whole-value application under **D7.2.1** and **S10.1.2v7**.

To use the Lambda script itself as a command-line filter, take its inherited stdin explicitly. The caller can supply a file, another process, or a terminal.

```lambda
pn main() {
    sh.stdin() |> some_piping
}
```

`sh.stdin()` exposes the script's existing standard input as a one-shot byte source. A read consumes bytes from that input. A second call cannot replay them (**S14.3.2**).

### Inputs and command arguments

The first parameter of a filter is its pipeline input. A local path in that position supplies file bytes to stdin. Under **S2.4.1v3** and **S10.1.2v7**, `\.'data.txt' |> sh.sort` and `sh.sort(\.'data.txt')` express the same call. A program that needs a filename *operand* receives it in its argument list. For example, a proposed generic filter `source |> sh.cmd("grep", ["-f", "patterns.txt"])` sends `source` to stdin and lets `grep` open the separate pattern file.

Use `sh.git("log", "--oneline")` as the short form of `sh.cmd("git", ["log", "--oneline"])`. Both launch Git with the same two arguments in the same order. The generic form also accepts a program name and an argument array assembled at runtime. [User guide](../doc/Lambda_Procedural.md#shell-pipelines)

Named wrappers translate documented Lambda arguments to individual executable arguments. For example, `count: true` supplies `-c` to `uniq`, and `lines: 10` supplies `-n` and `10` to `head`. A generic command takes an ordered argument array for other programs, repeated flags and operands. Each argument retains its boundary, including spaces. The host must provide a compatible executable. Importing the package selects neither a command interpreter nor a particular vendor's utilities.

### Open: distinguishing piped input from command arguments

The shorthand needs to distinguish a pipeline input from executable arguments
without guessing from their types:

```lambda
sh.grep("error", "app.log")       // arguments: "error", "app.log"
sh.stdin() |> sh.grep("error")   // input: stdin; argument: "error"
```

Today the compiler sees the pipe, but **S10.1.2v7** defines whole-value
application as inserting the piped value before the written arguments. The
called procedure receives the resulting argument values without their origin.
Consequently, the shorthand currently treats a first text value as a command
argument and a first path, list, array, binary value, or live process output as
stdin. Binary exposes the ambiguity: it can be either stdin or a valid
executable argument. Today `sh.cmd` with an argument array can pass a binary
first argument. Type dispatch alone cannot tell how the caller supplied it.

One proposed direction is to expose two distinct views at the call boundary:

- The **Lambda Script argument list** contains the arguments explicitly written
  in the call, in their source order, without a pipe-injected input.
- The **runtime C argument list** contains the effective arguments passed to
  the callee, including the input inserted by the pipe. The runtime also needs
  to identify whether an input was actually inserted; a C ABI list by itself
  does not describe argument origin.

For the second example, the written arguments are `["error"]`, while the
effective arguments are `[sh.stdin(), "error"]`. A direct call written as
`sh.grep(sh.stdin(), "error")` has the same effective arguments, but both are
explicitly written. These are conceptual views, not names for existing APIs or
a decision to expose native pointers to Lambda code.

**Open design:** decide how a procedure can inspect these views or equivalent
origin metadata, including bare callable pipes, indirect calls, named and
variadic arguments, forwarding, and both interpreter and JIT execution. A
script-visible distinction may make piped and direct calls observably different,
so it must be reconciled with **S10.1.2v7** before adoption. No language or C
ABI change is adopted by this note.

### Routing one command's stderr

Use a named `stderr` argument on the command that produces the diagnostics. Put positional arguments first. The process launcher consumes the option before it starts the executable. For example, this writes Git diagnostics to `errors.log` and sends only stdout to `head`.

```lambda
sh.cmd("git", ["status", "--short"], stderr: \.'errors.log') |> sh.head(lines: 10)
```

The same option works on the filter wrappers, for example `sh.sort(source, stderr: dest)` when `dest` holds a local file path. A path creates or truncates the file under the usual output policy (**D11.1.2**, **D11.2.2**). If opening it fails, report the error before starting that child and clean up any upstream processes. Routing diagnostics leaves the command's exit status visible. A future merge option could connect stderr to the same OS pipe as stdout at launch. Its spelling is open.

### Writing stdout to a file

File endpoints are symmetric. A path at the start supplies the first command's stdin. A path at the end receives the last command's stdout. The same reusable procedure connects them.

```lambda
pn main() {
    \.'data.txt' |> some_piping |> \.'output.txt'
}
```

The destination may also be a variable. With `let dest = \.'output.txt'`, `sh.ls() |> dest` means `output(sh.ls(), dest)`. Dispatch depends on the values at the connection, so live output returned through a procedure behaves like output returned directly by a command. The rule applies only in procedural code, with live process stdout on the left and a local file path on the right. An ordinary record piped to a path, or live stdout piped to a plain filename string, still fails the whole-value callable check under **S10.1.2v7**. A quoted path step keeps the dot in a filename under **S2.4.1v3**.

Under **S10.1.4v2** the sink has the same result and error behavior as explicit `output`. It drains stdout incrementally, writes its exact bytes regardless of the filename extension, and keeps stderr separate. It waits for cleanup and returns the byte count on a successful write. Use `(sh.ls() |> dest)^` to propagate a write error. A sink failure closes the pipe and reaps upstream children. File writes use the existing I/O policy under **D11.1.2** and **D11.2.2**. [Procedural file output](../doc/Lambda_Procedural.md#file-output)

To append, use the existing output option.

```lambda
(sh.ls() |> output(\.'output.txt', {mode: "append"}))^
```

An explicit `output(dest, {atomic: true})` streams live stdout into a staged file. Publication follows successful pipeline completion. A stage failure leaves the prior file in place. [File write policy, D11.1.2 and D11.2.2](../doc/Lambda_Formal_Design.md#d112-apps-and-scripts)

The original general file write and append operators were retired when `|>` became the pipe. The adopted file sink covers the live process case. **SO29** still tracks general file write and append syntax. [Historical ruling C6a](Lambda_Semantics_Formal2.md#c6a-pending-file-writeappend-syntax) · [Formal pipe rules](../doc/Lambda_Formal_Semantics.md#s101-union-pipe-and-filter)

## Internal and external commands

| Connection | What crosses it |
|---|---|
| Internal → internal | Lambda values. A record keeps its fields and types, and a sequence can feed the existing mapping and filtering operations. |
| External → external | An OS pipe connects stdout to the next stdin. Bytes, including NUL and trailing newlines, pass through exactly. Every stage keeps a separate stderr channel and exit status. |
| External → internal | Per-item mapping and filtering decode UTF-8 lines. An explicit decoder can instead produce other record types, such as JSON values. |
| Internal → external | A list, array or stream of Lambda values supplies each item followed by LF (S14.3.6v2). Callers can explicitly encode another format. A binary source already supplies bytes. |

For example, external `sh.ls()` produces the listing format of the installed executable. Code that needs entries with a name, size and type should consume structured directory data. Splitting a display listing on whitespace loses information about filenames.

### Mapping and filtering stdout by line

A per-item stage receives one line at a time from live command stdout. Each example runs independently in a `pn`.

```lambda
sh.git("log", "--oneline") |: contains(~, "fix")
sh.git("log", "--oneline") |> upper(~)
```

The filter emits matching lines. The map emits one transformed value per line. Combining the stages decodes stdout once. Each stage captures its lexical inputs when created and runs as the consumer pulls records. A binding or a procedure return carrying the live stdout gets the same behavior. Further per-item stages operate on the resulting Lambda values. A following external stage receives those values on stdin under S14.3.6v2. Whole-value connections from raw stdout such as `cmd |> sh.sort` and `cmd |> dest` transmit exact bytes. [Formal pipe and stream rules, S10.1.2v7, S10.1.6v3 and S14.3.5](../doc/Lambda_Formal_Semantics.md#s143-streams-and-laziness)

The decoder carries partial UTF-8 characters and partial lines across OS reads. It strips LF and CRLF. A lone CR is content. For example, the bytes for `"a\r\n\nlast"` yield three strings, `"a"`, `""` and `"last"`. A single LF yields one empty string. Empty stdout yields zero lines, and a trailing terminator adds no extra line. Invalid UTF-8, including an incomplete character at EOF, raises a decoding error when consumed. [Binary pipeline design, PL2–PL4](Lambda_Design_Pipeline.md#3-semantics-the-re-chunking-license-pl3-and-the-dispatch-rule-pl4)

### Sending Lambda items to stdin

```lambda
("pear", "apple") |> sh.sort
["pear", "apple"] |> sh.sort
```

Each expression sends `pear\napple\n` to stdin. Under **S14.3.6v2**, each outer list or array item contributes its representation followed by one LF. Strings contribute their UTF-8 contents verbatim, including embedded newlines. An empty list or array sends zero bytes, while an empty string item contributes one LF. Other items use the ordinary CLI result rendering. A nested array remains one item, and a rendered map can span several physical lines. A caller needing one machine-readable record per physical line can explicitly format each item as compact JSON. [List content, S2.5.6](../doc/Lambda_Formal_Semantics.md#s25-lists-and-blocks)

The same encoding applies incrementally to values yielded by a Lambda filter or map.

```lambda
sh.git("log", "--oneline")
    |: contains(~, "fix")
    |> upper(~)
    |> sh.sort
```

Here `sh.sort` receives each uppercase string followed by LF. Passing through line processing converts CRLF to LF and supplies a final LF for an unterminated input line. A direct external-to-external connection preserves the original bytes. [Shell stream rules, S14.3.5–S14.3.6v2](../doc/Lambda_Formal_Semantics.md#s143-streams-and-laziness)

### Nushell as prior art

Nushell makes both kinds of pipeline available in one language. Its internal commands exchange values, and user-defined commands can accept and return streaming input. The caret selects an external executable even when a built-in shares the name. In Nu, `^sort data.txt | ^uniq -c` connects two external processes. In `open --raw data.txt | lines | uniq --count`, the built-in counts distinct values across the input and returns rows with `value` and `count` fields. Sorting is optional for that frequency table. Lambda adopts the distinction through an imported `sh` namespace and UTF-8 lines for per-item stages, with explicit adapters for other formats. [Nushell pipelines](https://www.nushell.sh/book/pipelines.html) · [External commands](https://www.nushell.sh/book/running_externals.html) · [Custom commands](https://www.nushell.sh/book/custom_commands.html) · [Built-in uniq](https://www.nushell.sh/commands/docs/uniq.html)

## Execution and completion

Calling an external `pn` starts its child and returns a live, one-shot process output as soon as the child is connected. A subsequent stage takes that output as its stdin. A consumer may still be attaching when a producer fills the OS pipe. The producer can block while the calling Lambda task continues to attach the next stage. This is an effectful process launch under **S12.1.1v5** and **PL8**. Wrapping an already-running handle in a general `stream(handle)` retains the terminal-forcing rule of **S14.3.1v3–S14.3.3**. [Raw-byte process design](Lambda_Design_Pipeline.md#6-layer-0-raw-byte-process-spawn-pl8--the-real-cli-pipeline-enabler)

An ordinary binding such as `let revision = sh.cmd("git", ["rev-parse", "HEAD"])` holds the live stdout from that invocation. It can be piped onward once. To obtain a revision string, a consumer must read the bytes and decode the text. Removing the trailing line ending is a separate choice. The capture and decoding API remains open.

The CLI consumes a live output returned by `main`, writes the final stdout bytes or encoded records, and waits for completion. By default, each child writes diagnostics to the inherited stderr while its stdout is piped. The programmatic API for retaining and inspecting ordered statuses and diagnostics remains open. This gives the ordinary pipeline a completion point without requiring a plan variable or a separate `sh.run`. Julia offers the explicit-plan alternative with `run(pipeline(...))`. [Julia external programs](https://docs.julialang.org/en/v1/manual/running-external-programs/)

If a later launch fails, the owner closes open pipe ends, terminates outstanding children and reaps them. Timeout, cancellation and abandoned output also require cleanup. Any future diagnostic capture must keep draining stderr after it reaches its retention limit. A successful early consumer such as `head` can cause an expected upstream broken pipe. Preserve that stage status and allow the pipeline to complete successfully. Under **S14.3.4**, any unexpected stage failure makes pipeline completion fail, even when a later stage succeeds or a file sink writes all the bytes it receives. A terminal surfaces the failure after draining and cleanup. The runtime records each child's status as it reaps the child. The CLI reports a failed pipeline with a nonzero exit. The completion API and the choice of numeric CLI status when several stages fail remain open. [Task and stream ownership, S13.3.1v2–S13.3.2 and S14.3.3](../doc/Lambda_Formal_Semantics.md) · [Original shell pipe proposal](Lambda_Proposal_Shell_Pipe.md)

Set `timeout_ms:` on `sh.cmd`, `sh.ls`, or a filter wrapper to bound the connected pipeline. The deadline is relative to its first child launch. If multiple stages set a deadline, the earliest expiry wins. Omit the argument or use zero for no deadline from that stage. For example, `sh.cmd("sleep", ["30"], timeout_ms: 250) |> sh.head` reports a timeout and reaps both children. [Task scope and completion, S13.3.1v2–S13.3.2 and S14.3.4](../doc/Lambda_Formal_Semantics.md#s143-streams-and-laziness)

## Deferred interpreter modules

The initial package is `lambda.shell` for direct executable calls. Both `lambda.shell.bash` and `lambda.shell.powershell` are deferred. If a later design introduces an interpreter module, the existing import rules can express `import sh: lambda.shell.bash`. The alias is local to the importer under **D9.3.2–D9.3.4**.

There is little benefit in selecting Bash to run external `sort` or `uniq`. The executable supplies their options and behavior. Interpreter selection matters for Bash built-ins and expansions, or for a PowerShell pipeline of cmdlets that pass objects within one PowerShell session. On Windows, for example, PowerShell's `ls` can resolve to the `Get-ChildItem` alias. That is a different operation from launching an external `ls`. [PowerShell aliases](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_aliases)

If added, an interpreter module should expose an explicit script invocation that runs a complete script in one interpreter process. Input and output at the Lambda boundary still use bytes. A PowerShell script can choose a serialization such as JSON when returning objects to Lambda. Ordinary `sh.*` arguments continue to pass directly to executables. This leaves interpreter choice local to the stage that needs it and avoids reparsing argument data as shell source.

## Appendix · Runtime dependency

[Implementation checkpoint](impl/Lambda_Impl_Shell_Piping.md#current-checkpoint)

The earlier proposal's `io.pipeline(stages, options?)` describes a batch of known stages. Its pipe creation, concurrent draining, cancellation and status collection can share a native backend with this package. The reusable `pn` additionally requires incremental attachment and ownership transfer across procedure returns. Today's `io.exec` waits and captures one child with empty stdin. The decisive integration case is a producer larger than the pipe buffer returned through a helper to an early-exiting consumer, with every child subsequently reaped. [Original proposal](Lambda_Proposal_Shell_Pipe.md) · [Current io.exec contract](../doc/Lambda_Sys_Func.md#ioexecprogram-args-options)
