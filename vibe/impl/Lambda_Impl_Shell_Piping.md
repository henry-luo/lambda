# Shell pipeline implementation

## Current checkpoint

### Implementation

- 2026-11-25, main checkout on `master` at `cb10b30bab` with the working-tree shell changes. The [shell piping design](../Lambda_IO_Shell_Piping.md) records the adopted package surface.
- **S10.1.2v7**, **S10.1.4v2**, **S12.1.1v5** and **S14.3.4–S14.3.6v2** govern application, file sinks, effects, completion and encoding. The macOS CLI implements direct executable sources, reusable procedures, inherited stdin, byte-exact OS pipes, local file sinks, and list and array input. `sh.git("log", "--oneline")` uses the same executable and arguments as `sh.cmd("git", ["log", "--oneline"])`.
- Per-item mapping and filtering capture the RHS in a procedure closure on both execution tiers. The callback evaluates when a consumer pulls a record and retains lexical inputs across procedure returns. The UTF-8 decoder carries partial lines and code points across reads and preserves blank and final unterminated lines. A downstream process receives the encoded values incrementally while the terminal drains stdout. Precise roots trace retained values under **D6.3.3**.
- The executing task owns the native processes and pipe ends. Task completion and CLI completion reap abandoned children. A successful downstream stage can stop an upstream producer. Unexpected stage failures survive a successful downstream exit, redirected stderr or completed file writes. The earliest `timeout_ms:` deadline bounds the connected processes and wakes a parked per-item consumer. Explicit atomic output publishes the staged file after successful completion.
- Process and file access uses the existing unbrokered admission boundary under **D11.1.2**. Named stderr paths keep diagnostics separate. By default the child inherits stderr.

### Qualification

- `make test-shell-piping` passed on the macOS arm64 debug host. The runner at `test/shell/pipe/test_shell_piping.py` replays standalone fixtures on interpreter, JIT, and automatic selection. Set `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` for the precise-root checks. [Shell pipeline fixtures and benchmark](../../test/shell/pipe/README.md)
- The shell suite checks 1 MiB byte-exact and mapped transfers, argument boundaries, Git and direct command sources, early exit, reaping, cancellation, deadline cleanup, stderr routing, byte count, append and atomic output. Record cases check lazy captures, UTF-8 and CRLF split across reads, blank lines, embedded NUL, unterminated lines, invalid input and incremental writes through a waiting child.
- Twelve command-composition cases exercise log ranking, NUL-delimited filenames, text transforms, file copies with `tee`, line counts with `wc`, and chains that switch between shell bytes and Lambda values more than once. They also cover symmetric file output, early `grep -q` success and a nonzero upstream exit after producing output (**S10.1.4v2**, **S14.3.4–S14.3.6v2**). All three backend settings pass with normal and forced GC. See `temp/shell-pipeline/composed-tests.log` and `composed-tests-gc.log` in the same directory.
- The exact symmetric file example passed in a separate working directory with `LAMBDA_HOME` set, on all three backend settings.
- Under **S14.3.3** and **D7.5.1**, the interpreter now consumes a returned shell stream before its task drain. A 1 MiB backpressure case failed with a three-second deadline on the old auto build and now passes on interpreter, JIT, and auto, including forced GC. The optimized 100 MiB mixed benchmark dropped from 5.94 s to 0.96 s in auto, and all three backend settings produced the same bytes before the deadline. See `temp/shell-pipeline/ordering-fix/` for the test and release logs. [Benchmark report](../../test/shell/pipe/README.md#retest-after-the-ordering-fix)
- After the ordering fix, `make test-lambda-baseline JOBS=8` passed 6,125 of 6,129 checks with local listeners and offline media services available. The forced-GC ordinary-procedure parity fixture passes. The CLI completion hook looks up an existing task without creating one before it roots the result. See `temp/shell-pipeline/ordering-fix/baseline.log`.
- The remaining four failures also reproduce on the unmodified `cb10b30bab` debug host built with the same Input and Radiant checkouts. They are the checked-store diagnostic and the editor media guard, unknown-source and reload-safety fixtures. Control output is in `temp/shell-pipeline/head-controls.log`. The [Lambda issue ledger](../Lambda_Issue_Ledger.md#checked-store-diagnostics-lose-their-message) records the diagnostic failure.

### Follow-up scope

- Linux and Windows runtime qualification is pending. Live atomic publication uses the existing POSIX file-anchor backend.
- The ordered status and diagnostic inspection API, capture and decoding API, and numeric CLI status selection for multiple failures remain design questions. Bash and PowerShell interpreter packages remain deferred.

- Direct wrappers for common external commands accept a source or executable arguments first. A first text value is an argument; a first path, list, array, binary value or process output is a source. Because binary may also be an executable argument, `sh.cmd` disambiguates a binary first argument. The source commands `sh.printf`, `sh.yes` and `sh.sleep` accept executable arguments directly. Whole-value pipe application supplies the source under **S10.1.2v7**; **S14.3.5–S14.3.6v2** continue to govern bytes and value encoding. The generic `sh.cmd` retains argument-array and process-option support.
