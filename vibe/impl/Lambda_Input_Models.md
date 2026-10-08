# Textual 3D model input implementation

**Date:** 2026-10-08
**Status:** OBJ/MTL, glTF JSON, and ASCII Model3D input implemented.
**Design:** [WebGL design §14](../radiant/Radiant_Design_WebGL.md#14-textual-3d-asset-input).
**Base:** `96f9490f11`, worktree branch `codex/webgl-phase3`.

The parsers construct Input-owned Mark values under **D4.1.3**. Positional
operands are arrays and repeated records are elements, preserving boundaries
under **S2.5.1v2**, **S2.6.3**, and **S2.6.4**. glTF reuses the JSON map/array
mapping. The shared JSON parser now rejects incomplete containers, non-JSON
numeric spellings, unknown escapes, and malformed Unicode escapes.

Extension and MIME detection dispatch the four textual formats; A3D also has
content detection. HTTP Content-Type reverse lookup recognizes them. Native
and explicit WASM source lists include the parser; WASM execution was not
tested in this increment. External resources remain references, commands stay
inert, and application-specific bytes remain binary, as detailed in §14.
Scene import, resource decoding, rendering, and playback are separate work.

Validation on macOS:

| Check | Result |
|---|---|
| `test_input_model_gtest` | 16/16 pass |
| `test_mime_detect_gtest` | 13/13 pass |
| `input_model_formats.ls` golden via `test_lambda_gtest` | passes on `interp`, `auto`, and `jit` |
| Five input-baseline executables | 1,725/1,725 pass: HTML5 364, Markdown 1,343, YAML 4, ASCII math 3, LaTeX math 11 |
| Lambda baseline runner, 30 executable/script entries | 4,338/4,338 pass |
| `git diff --check` | clean |

`make build-lambda-baseline` built the host, runtime tests, input-baseline
tests, and Node modules successfully. The five input-baseline executables ran
from the main checkout so the tracked external-corpus links resolve, following
Developer Guide §7. The runtime baseline and the new integration golden ran
from the worktree with its rebuilt host.

The full input suite and aggregate test build are **not green**. A comparison
run with main-checkout binaries and a second run with rebuilt worktree input
binaries both report CSS roundtrip, PDF visual, and graph-parser LOC-budget
failures. The worktree run also reports the Markdown source-span check
`SourcePosTest.MarkdownSpanTextIsTheConstruct` and dynamic-loader diagnostics
for `_LIT_BOOL` in other harnesses. Fresh linking fails for
`test_dom_range_gtest` and `test_source_pos_bridge_gtest` on missing Radiant
geometry/form-control symbols. The aggregate build additionally fails to link
the display-list, retained-display-list, and state-store harnesses. These
symbols and failing source areas were not changed by this increment; no tests
or budgets were relaxed.

Local logs are under the worktree's `temp/`: `model-tests.log`,
`model-mime-tests.log`, `model-golden-{interp,auto,jit}.log`,
`model-test_*_gtest.log`, `model-runtime-baseline.log`,
`model-input-suite.log`, `model-input-build.log`, and `model-build-all.log`.
The full input comparison used the main-checkout suite manifest with
`LAMBDA_TEST_BIN_DIR` pointing to worktree executables; the new parser binary
was run separately because it is newly registered in the worktree manifest.
