# Lambda REPL Design Document

**Date:** 2026-10-06 (rewritten for the persistent interpreter session)
**Authority:** `doc/Lambda_Formal_Semantics.md` S16.7.4–S16.7.6 (echo, no
rebinding, session kinds); `doc/Lambda_Formal_Design.md` D8.1.1v17 (one T0
session, no replay, no Tree-sitter, interrupt, unsupported entries). Decision
record: [`Lambda_Design_Repl_Interp.md`](Lambda_Design_Repl_Interp.md) (RI1–RI6).
Detailed design: `doc/dev/lambda/LR_01_Compilation_Pipeline.md` §6.

## Overview

`lambda` with no arguments opens an interactive session. Each complete entry is
parsed by the C parser, built against the session's retained global scope,
planned into the persistent module slab, and executed by the T0 interpreter.
Earlier entries are never re-parsed, re-compiled or re-run, so per-entry cost
does not grow with session length. An entry is all-or-nothing: a rejected or
failed entry is rolled back whole and the next entry sees the last good state.

## Session kinds (S16.7.6)

| Invocation | Kind | Entry context |
|---|---|---|
| `lambda` | functional | top-level content, exactly as in a script file; `var`, assignment and `pn` calls are E224 |
| `lambda run` (no script) | procedural | statements of one persistent implicit `pn` body; `var` persists, `pn` calls run. The session announces at start that entries carry out effects |

## Entry lifecycle

| Status | Meaning | Session state | Output |
|---|---|---|---|
| OK | built and ran | published | the value, unless the entry is only declarations or statements (S16.7.4) |
| INCOMPLETE | the parser needs more input | untouched | continuation prompt `.. ` |
| REJECTED | parse, type, plan or T0-support failure | rolled back | the diagnostic, then `Entry rolled back.` |
| FAILED | the entry ran and completed with an error or a fault | rolled back, slab snapshot restored | the error, then `Entry rolled back.` |

- **Completeness.** An unclosed bracket, string or block comment keeps the
  continuation prompt without parsing. Balanced input goes to the session; the
  parser's INCOMPLETE status (`let x =`, a trailing `+`) also continues.
- **Redefinition** of a session name is E209, as in a file (S16.7.5). `clear`
  starts a new session.
- **Imports** load and initialize their module cone before the entry runs; a
  module whose initializer already ran in the session is not re-run.
- **Unsupported entries.** An entry T0 cannot run is rejected with the
  construct's name; it is never compiled on its own.
- **Interrupt.** Ctrl-C while an entry runs faults it at the next T0 call or
  loop back-edge; the entry is rolled back and the session continues. Code
  already promoted to a MIR satellite is not polled and stops at its next
  return to T0.
- **Tiers.** The session is always T0. `--tier`/`LAMBDA_EXEC_BACKEND` governs
  satellite promotion only: `auto` promotes hot functions at the ordinary
  thresholds, `interp` never promotes, `jit` promotes at the first call.

## Commands

| Command | Effect |
|---|---|
| `quit`, `q`, `exit` | leave the REPL |
| `help`, `h` | show help |
| `clear` | start a new session; every binding is dropped |
| `.env` | list the session's bindings with their current values |
| `.type <expr>` | show the expression's static type without running it |
| `.time <entry>` | run the entry and report its wall time |
| `.load <file>` | run a file as one entry; a failure rolls the whole file back |
| `.save <file>` | write the session's accepted entries to a file |
| Tab | complete session names, system functions and keywords (interactive terminal only) |

Commands are recognized only at the start of a fresh entry and only in these
exact spellings.

## History

An interactive terminal restores history from `~/.lambda_history` at start and
writes it back at exit. `LAMBDA_REPL_HISTORY` names another file; an empty value
turns persistence off. Piped input never reads or writes the history file.

## Key files

| File | Purpose |
|---|---|
| `lambda/main.cpp` | `run_repl`: the loop, commands, echo, SIGINT arming |
| `lambda/main-repl.cpp` | prompts, bracket fast path, line editor wiring, completion, history file |
| `lambda/runtime/runner.cpp` | `interp_repl_session_*`: the entry transaction, diagnostics, session services |
| `lambda/runtime/interp.cpp` | fragment execution, import-cone initialization, interrupt polling |
| `lambda/runtime/terminal_host.cpp` | completion request/reply and history transfer with `lambda.io.terminal` |
| `lmd/package/io/terminal.ls` | the line editor (history, kill ring, completion insertion) |

## Testing

`test/test_lambda_repl_gtest.cpp` drives `lambda.exe` over a pipe. The
`LambdaReplSessionTests` group covers the echo rule, E209, parser-driven
continuation, both session kinds, rollback of a failed procedural entry, file
imports, tier agreement and SIGINT.

## Open items

- An unbound name and some operator type errors complete with a bare error that
  has no diagnostic, in files and in the REPL alike. The REPL then prints a
  generic "completed with an error and no diagnostic" line. Whether an unbound
  name is a compile error needs a ruling.
- Member completion after `.` offers nothing; it would need the receiver's
  static shape.
