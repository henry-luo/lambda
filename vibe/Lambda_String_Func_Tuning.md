# Lambda String Function Tuning — Proposal

**Date:** 2026-09-24

**Status:** PROPOSED. One slice has landed: T28-5.1, `replace()` and `find()` on the shared literal scan kernel (commit `8491fe595`, recorded in [Tune28 §9.10](impl/Lambda_Impl_Tune28.md)). Everything in §5 is a candidate, and nothing there is scheduled.

**Spec linkage:** no ruling change is proposed. The work must preserve S17.1.1 (split), S2.6.4 (adjacent strings merge), S4.7.1 and S4.8.1 (a float spells as its shortest round-trip decimal), D4.3.1 and D4.3.2v2 (objects never move; string bytes stay inline), and D5.3.3 (a helper's arguments are caller-rooted borrows). Points that touch a ruling are collected in §8 for the user to decide.

**IDs:** extends the T28-5 series, whose home is [Tune28 §9.10](impl/Lambda_Impl_Tune28.md). T28-5.1 is the landed slice. Candidates are referred to by section number here (for example §5.2-3) and get Tune IDs when a round schedules them.

**Scope:** byte-level text processing in the C/C++ code: the `lib/` string kernels, Lambda's string builtins, LambdaJS's string methods, the input parsers (`lambda/input/`) and the output formatters (`lambda/format/`). Out of scope: JIT-generated code, because MIR has no vector instructions and S13.4.1 keeps user-written reductions sequential.

**Evidence:** release builds of `8491fe595` on Apple Silicon (arm64); line numbers are as of that commit. Scripts, generated inputs and `sample` profiles are in `temp/replace-find-fastpath/temp/perf/` (scratch, not committed). Each claim carries one of three evidence levels:
- **measured** — timed or profiled in this study;
- **verified** — reproduced, or read in the code, in this study;
- **survey** — reported by a read-only code survey and not yet checked independently.

---

## 1. Summary

In `beng/revcomp2`, 18 calls like `replace(s, "A", "1")` over a 10 KB sequence took 0.79 ms of the 0.99 ms run. The literal paths of `replace()` and `find()` called `memcmp` at every byte position of the haystack, once to count matches and again to copy. T28-5.1 moved them onto the `memchr`-based kernel that `split()` got in T28-5, and added a branch-free byte substitution that the compiler vectorizes. revcomp2 became 6.4× faster and regexredux2 1.9× faster.

A follow-up study looked for the same pattern everywhere else. It had four parts:
- profiles of six input parsers and four output formatters on large documents;
- a micro-benchmark of the `lib/str.c` scanners;
- four read-only code surveys (lib, parsers, formatters, runtime and LambdaJS);
- scratch experiments on the biggest hot spots, all reverted.

The byte-at-a-time pattern is common. The largest single wins, though, are algorithmic: rescans that make a linear job quadratic, a list walk on every allocation, and a float formatter that makes up to 34 libc calls per number.

| Change | Status | Workload | Before | After |
|---|---|---|---:|---:|
| `replace()`/`find()` on the literal kernel | landed, T28-5.1 | beng/revcomp2 | 1.284 ms | 0.201 ms (6.4×) |
| same | landed, T28-5.1 | beng/regexredux2 | 1.181 ms | 0.607 ms (1.9×) |
| `arena_owns` checks the current chunk first | experiment | Markdown parse, 9 MiB | 304 ms | 151 ms (2.0×) |
| shortest float spelling probes 15, 16, 17 digits | experiment, byte-identical output | `format(d, 'json')`, 43 MiB out | 1,076 ms | 389 ms (2.8×) |
| no per-byte line/column tracking (upper bound) | experiment | JSON parse, 33 MiB | 178 ms | 128 ms (1.4×) |

Most of this needs no hand-written SIMD. libc `memchr`, bulk copies, and loops written so the compiler vectorizes them cover it (§2.4). The study also found correctness bugs. The most visible is that `lastIndexOf` returns wrong positions in both Lambda and LambdaJS (§5.1-1).

---

## 2. The mechanism: why the new way is faster

### 2.1 Where a byte-at-a-time loop spends its time

A loop that handles one byte per iteration pays a fixed toll on every byte. Four tolls dominate:

1. **A library call per byte.** The old `replace()`/`find()` loops called `memcmp(p, needle, len)` at every position. That is a call, argument setup, and the callee's own size dispatch, just to compare what is usually one byte. `SourceTracker::advance(1)` is likewise an out-of-line call per character in the JSON string loop.
2. **Bookkeeping per byte.**
   - `stringbuf_append_char` checks for overflow and capacity, then stores the byte, a NUL terminator and two length fields. The byte store may alias the buffer header, so the compiler reloads the lengths every time.
   - `SourceTracker::advance` tests for end of input, a newline and a UTF-8 continuation byte, then updates the offset and column, for every byte.
3. **A data-dependent branch per byte.** Is this a quote, a backslash, a control byte? On text these branches mispredict often, and each miss costs on the order of 10–15 cycles.
4. **No vectorization.** An early `break`, a call, or possible pointer aliasing inside the loop stops clang from using 16-byte vector instructions. The loop can then do no better than roughly one byte per cycle, even when its body is cheap.

Measured, the old `replace()` cost about 6 ns per byte: 18 calls over 10 KB took 1.12 ms, and each call makes a count pass and a copy pass. libc `memchr` scans at about 30 GB/s, 0.03 ns per byte (§4.4). The per-byte gap is roughly 100×.

### 2.2 The replacement: search in bulk, act only at the hits

The fix separates the **search** from the **action**:
- The search asks one question of every byte: is this byte special? A routine that works a block at a time answers it for 8–32 bytes per step, with one branch per block.
- The action runs only at the hits: copy a replacement, handle an escape, record a match.

The cost changes from n × (per-byte toll) to n / (block width) + hits × (action). In typical text the hits (quotes, escapes, newlines, delimiters) are rare, so the second term is small. When hits are dense, technique C below removes the branches altogether.

Worked example: `replace(s, "A", "1")` on 10 KB of DNA with about 2,500 matches.
- **Old:** 10,000 `memcmp` calls to count the matches, then 10,000 more to copy.
- **New:** one SWAR count over 1,250 eight-byte words, then `translate_byte`. That is a branch-free select that clang compiles to NEON, 64 bytes per iteration, so about 160 iterations.
- **Net:** revcomp2's 18 such calls now cost about as much as not making them. revcomp2 runs in 0.201 ms; with the calls deleted, it ran in 0.207 ms (measured earlier on an older release binary).

### 2.3 Techniques

**A. Candidate scan (substring search).**
- **How it works:** `memchr` jumps to the next occurrence of the needle's first byte, and `memcmp` checks the rest of the needle only at those candidates. libc `memchr` is vectorized (NEON on arm64, AVX2 on x86-64), so the scan runs near memory speed with one branch per block.
- **Where:** landed as `literal_find` (`lambda/runtime/lambda-eval.cpp:6909`), shared by `split`, `replace` and `find`.
- **Weakness:** a very common first byte produces many candidates. The refinement is to filter on a second byte, as `str_find` already does, or on the needle's rarest byte.

**B. Run scan and bulk copy (parsing and escaping).**
- **How it works:** find the next stop byte (quote, backslash, control byte, `<`, `&`, delimiter) and copy the whole clean run before it with one `memcpy` or `stringbuf_append_str_n`. The per-byte bookkeeping of §2.1 is then paid once per run.
- **Choosing the stop search, by stop-set size:**
  - one byte: `memchr`;
  - two or three bytes: SWAR (technique D);
  - a larger set: a 256-entry table (few branches, but scalar), or a vector byte-class lookup if profiling ever justifies it (§2.4).

**C. Branch-free transforms the compiler vectorizes (per-byte rewrites with no search).**
- **How it works:** `translate_byte` computes `dest[i] = src[i] == from ? to : src[i]` over the whole string.
  - With no early exit, no call and `__restrict` pointers, clang `-O3` emits NEON `cmeq.16b`/`bit.16b` unrolled four times: 64 bytes per iteration. This was checked in the release binary's disassembly (Appendix A.1).
  - This is SIMD without intrinsics, and it is portable to every build target.
- **Rules for vectorizability:** no early exit, no calls, no aliasing, a simple counter, and narrow accumulators (technique E).

**D. Word-at-a-time (SWAR).**
- **How it works:** process 8 bytes per 64-bit operation, as `lib/str.c` does throughout.
- **Speed:** on Apple Silicon `str_find_byte` matches libc `memchr` (29.5 vs 30.5 GB/s, §4.4). On x86-64, AVX2 `memchr` works on wider blocks.
- **Trap:** the common zero-byte test `(v - 0x0101…) & ~v & 0x8080…` can also flag the byte just above a real match, because of borrow propagation.
  - Searching forward for the first match, the extra flag is harmless.
  - Searching backward for the last match, it is wrong. That is the `str_rfind_byte` bug (§5.1-1).
  - Where every flagged position matters, use the exact form `~(((v & 0x7F7F…) + 0x7F7F…) | v | 0x7F7F…)` (Appendix A.3).

**E. Counting and reducing in narrow lanes.**
- **Counting:** with a `size_t` accumulator (`n += s[i] == c`), the vectorizer widens every compare result to 64 bits. That plain loop is slower than SWAR (7.1 vs 12.9 GB/s). Accumulating in `uint8_t` lanes for at most 255 iterations, then widening, costs one compare and one subtract per 16 bytes.
- **ASCII check:** a check without an early exit (OR the bytes together, test once per block) runs at 109 GB/s. The early-exit SWAR `str_is_ascii` runs at 21 GB/s, 5× slower (§4.4).

**F. Laziness: don't compute per byte what is rarely needed.**
- **JSON line/column:** the JSON parser keeps line and column current for every byte, but they are only needed when an error is reported. An offset plus a lazily built line index is enough: find newlines with `memchr`, binary-search on demand. Removing the per-byte tracking entirely gives the upper bound, JSON parse 1.4× faster (§4.5).
- **`trim` and slice results** can inherit `is_ascii` from the source instead of rescanning.
- **`format()`'s complex-number pre-pass:** it walks the whole tree looking for complex numbers before formatting, which is 17–18% of HTML/XML output time. The check can happen during the formatting walk instead.

**G. Removing quadratic rescans.** These are the largest single wins, and each fix is usually a line or two. A scan restarted from a fixed point on every step turns O(n) into O(n²):
- `strlen` of the rest of the document for every `\u` escape;
- a walk back to the start of the line for every YAML character;
- a walk over every arena chunk for every list growth;
- a first-fit walk of a free list for every allocation;
- a UTF-8 walk from byte 0 for every character index.

§5.2 lists them with measurements.

**H. Fewer probes for the shortest float spelling.**
- **Old behaviour:** `lambda_finite_double_to_shortest` searched precisions 1 to 17, each with `snprintf("%.*e")` and an `sscanf` parse-back: up to 34 libc calls per number.
- **Why three probes are enough:** any decimal of at most 15 significant digits survives a round trip through a double (DBL_DIG = 15). So when the shortest spelling has k ≤ 15 digits, rounding the double to 15 digits reproduces it exactly, padded with zeros that the routine already strips. When the shortest spelling needs 16 or 17 digits, the 15-digit probe fails and the 16- and 17-digit probes are the same ones the old loop made. Probing 15, 16, 17 therefore gives the same output as probing 1…17, in at most three tries.
- **Measured:** JSON output 2.8× faster, and 43 MiB of output byte-identical (§4.5).
- **Further step:** a Ryu/Schubfach-class algorithm would cut more (§8-5).

### 2.4 Why not hand-written SIMD first

- **The toll is the cost, not the scan width.** Most of the measured cost is the per-byte toll (calls, bookkeeping, branches). Bulk-copying JSON string runs alone gained 7%, and dropping per-byte tracking gained 40%; neither needed intrinsics.
- **Vector width comes for free, portably.** `memchr` and compiler vectorization already reach it, and the same source builds for arm64 NEON, x86-64 SSE2/AVX2, Windows and the WASM build.
- **SWAR already keeps up.** On this machine the existing SWAR scanner equals libc `memchr`.
- **Where intrinsics would pay:**
  - **Target:** classifying a multi-byte stop set 16 bytes at a time with a vector compare or table lookup (`vqtbl1q_u8`, `pshufb`). Examples: JSON strings (`"`, `\`, bytes below 0x20) and HTML text (`<`, `&`, NUL).
  - **Proposal:** add at most one small `lib/` helper with a scalar fallback, and only if profiles taken after technique B still show the stop search itself dominating.
  - §8-4 asks whether intrinsics are acceptable at all.

### 2.5 What must not change

- **Output is byte-identical:**
  - leftmost, non-overlapping matches (S17.1.1 for split; `replace` and `find` follow the same rule);
  - the same `is_ascii` flags;
  - the same float spellings (S4.7.1, S4.8.1).
- **UTF-8:** ASCII stop bytes and one-byte needles never occur inside a multi-byte sequence, so byte-level scanning is safe. A result may carry `is_ascii` only when every one of its bytes is proven ASCII.
- **GC:**
  - A `String`'s `chars` pointer stays valid across the allocation of a result. Objects never move and string bytes stay inline (D4.3.1, D4.3.2v2), and a helper's arguments are caller-rooted (D5.3.3).
  - Data-zone pointers (list buffers, packed arrays) do not stay valid. Reload them after any allocation, because D4.3.1 compacts the data zone.
- **Verification:** every change goes through the protocol in §7.

---

## 3. What has been done

| ID | Date | Change | Result |
|---|---|---|---|
| — ([record](<impl/Lambda_Tune_String (done).md>)) | before T28 | `is_ascii` flag on `String`; O(1) `len`, indexing and slicing for ASCII strings; interned one-character ASCII strings; pointer-identity fast path in `fn_eq` | AWFY `json2`: string indexing had been 95.6% of CPU; 113× faster |
| T28-5 ([Tune28 §9.10](impl/Lambda_Impl_Tune28.md)) | 2026-09-19 | `split` uses a `memchr` candidate scan instead of `memcmp` at every byte, in both its count pass and its split pass | three_way_merge2 and log_pipeline2 19–25% faster |
| T28-5.1 (commit `8491fe595`) | 2026-09-24 | see list below | see results table below |

T28-5.1 made four changes:
- The kernel becomes `literal_find` with `ignore_case`, and the literal paths of `replace()` and `find()` use it.
- `count_literal_matches` absorbs `split_literal_match_count` and counts a one-byte needle with SWAR `str_count_byte`.
- `translate_byte` handles one-byte-for-one-byte replace-all.
- `ascii_case_fold` probing covers `ignore_case`.

T28-5.1 results, release builds of the same HEAD, median of 7 runs:

| Benchmark | Before (ms) | After (ms) | Speedup |
|---|---:|---:|---:|
| beng/revcomp2 | 1.284 | 0.201 | 6.39× |
| beng/revcomp | 1.263 | 0.305 | 4.14× |
| beng/regexredux2 | 1.181 | 0.607 | 1.95× |
| beng/regexredux | 1.435 | 0.664 | 2.16× |
| text/three_way_merge2 | 1976 | 1846 | 1.07× |
| text/prettier_ast2 | 518.7 | 493.0 | 1.05× |

knucleotide2 and log_pipeline2 read 0.98× on interleaved runs with overlapping ranges; log_pipeline2 calls none of the changed builtins.

T28-5.1 was verified four ways:
- a 4,000-case differential fuzz of `replace`/`find`/`split` against the control binary, byte-identical;
- 11 new cases in `test/lambda/find_replace_options.ls`;
- the `LambdaOptStrings.LiteralSplitKernelAvoidsBytewiseComparisons` pin, which now also checks that `fn_replace_impl` and `fn_find_impl` call `literal_find`;
- the Lambda baseline, 5858/5858.

---

## 4. Measurements

### 4.1 Parse throughput (release, one run, MiB = 2^20 bytes)

| Input | Size | Parse time | Throughput |
|---|---:|---:|---:|
| raw text read (`'text'`) | 33.2 MiB | 7.8 ms | ~4,300 MiB/s |
| JSON | 33.2 MiB | 174 ms | 191 MiB/s (Node `JSON.parse`: 479) |
| CSV | 37.0 MiB | 158 ms | 234 MiB/s |
| XML | 22.8 MiB | 141 ms | 162 MiB/s |
| YAML | 10.6 MiB | 149 ms | 71 MiB/s |
| HTML | 13.4 MiB | 197 ms | 68 MiB/s |
| Markdown | 9.0 MiB | 301 ms | 30 MiB/s |

### 4.2 Where parse time goes (macOS `sample`, top-of-stack share, release build with symbols)

| Format | Top costs | Allocator share |
|---|---|---:|
| JSON | `SourceTracker::advance` 23%, `parse_string` 23%, `arena_alloc_aligned` 8% | 12.5% |
| CSV | `parse_csv_field` 34%, `map_put_via_shape_transition` 16%, `memmove` 11% | 16.8% |
| XML | `pool_take_block` 22%, `parse_element` 10%, `stringbuf_append_char` 8%, `__mprotect` 6% | 45.4% |
| YAML | `parse_plain_scalar` 21%, `parse_double_quoted` 7%, `make_scalar` 4% | 28.6% |
| HTML | `strcmp` 22% (plus ~5% in call stubs), `pool_take_block` 19%, `html5_tokenize_next` 8%, `html5_insert_character` 7% | 34.2% |
| Markdown | `list_reserve_capacity` 58% (the `arena_owns` walk, via `parse_inline_spans` → `list_push`), `strpbrk` 12% | 14.6% |

### 4.3 Where output time goes (`format(d, fmt)` on the parsed documents)

| Format | Throughput | Escaping loop | Tag-name compares | Float formatting | `format_contains_complex` |
|---|---:|---:|---:|---:|---:|
| JSON | 40 MiB/s | — | — | 73% | — |
| HTML | 129 MiB/s | 27% | 22% | 6% | 17% |
| XML | 205 MiB/s | 30% | 11% | — | 18% |
| Markdown | 187 MiB/s | 51% | 13% | 1% | 7% |

### 4.4 `lib/str.c` scanners (1 MiB buffer, cache-resident, arm64, `-O3 -march=native`, GB = 10^9 bytes)

| Scanner | `lib/str.c` (SWAR) | Alternative | Reading |
|---|---:|---:|---|
| find byte | 29.5 GB/s | libc `memchr` 30.5 GB/s | parity on Apple Silicon |
| count byte | 12.9 GB/s | plain `size_t` loop 7.1 GB/s | SWAR wins; narrow lanes would beat both (technique E) |
| is-ASCII | 21.4 GB/s | block OR-reduce, no early exit, 109 GB/s | 5× headroom |
| find 3-byte needle | 5.9 GB/s | libc `memmem` 1.3 GB/s | `str_find` is 4.7× faster; don't use `memmem` |

### 4.5 Experiments (scratch edits in a worktree, reverted)

| Experiment | Before | After | Note |
|---|---:|---:|---|
| `arena_owns` checks `arena->current` first | Markdown 304.1 ms | 151.3 ms | 2.01× |
| float probes 15, 16, 17 with `strtod` | JSON out 1,075.9 ms | 389.3 ms | 2.76×; 45,161,871 bytes, identical SHA |
| JSON string runs appended in bulk | JSON parse 178.5 ms | 167.2 ms | 1.07× |
| plus span-based `SourceTracker::advance` | 179.4 ms | 170.6 ms | no further gain (bulk runs alone: 167.5 ms in the same run): the tracker cost is spread over every token |
| no line/column tracking at all (upper bound) | 178.5 ms | 127.8 ms | 1.40×; CSV and YAML unchanged (they don't track this way) |

### 4.6 Pathological inputs (verified reproducers)

| Input | Smaller | Larger (2×) | Growth |
|---|---:|---:|---:|
| JSON string of `éx` repeated | 137 KiB: 20.6 ms | 273 KiB: 80.9 ms | 3.9× (quadratic) |
| YAML, 50 lines of long plain scalars | 489 KiB: 570 ms | 977 KiB: 2,266 ms | 4.0× (quadratic) |

HTML, the same document shape in two languages:

| Language | Size | Parse time | Peak RSS |
|---|---:|---:|---:|
| English | 3.0 MiB | 38.5 ms | 119 MB |
| Chinese | 3.1 MiB | 134.9 ms | 507 MB |

The Chinese version takes 3.5× the time and 4.3× the memory.

---

## 5. Places to tune

Each item names the location as of `8491fe595`, what the code does now, the technique from §2.3, and its evidence level. "Already fast" notes record what is done well and should not be redone.

### 5.1 Correctness defects found along the way

Fix these first. Items 1–3 were reproduced on both tiers and filed in the [Issue Ledger](Lambda_Issue_Ledger.md) on 2026-09-24. Item 4 waits on §8-1, and item 5 needs verifying before it is filed.

1. **`str_rfind_byte` returns a position past the real last match** (`lib/str.c:266`). **verified**; filed as [LR05-14](Lambda_Issue_Ledger.md#lr05-14).
   - **Cause:** the SWAR false positive of technique D.
   - **Reproducers:**
     - `last_index_of("dir/.hidden", "/")` returns 4 in Lambda.
     - `"dir/.hidden".lastIndexOf("/")` returns 4 in LambdaJS; Node returns 3.
     - `"abcdefgh"` searching for `"b"` returns 2 instead of 1.
     - Node-compatible `Buffer.lastIndexOf` goes through the same function.
   - **Why tests missed it:** every match in `test/lib/test_str_gtest.cpp` sits in the scalar tail.
   - **Fix:** the exact zero-byte mask (Appendix A.3), plus both reproducers as tests.
2. **Indexing a non-ASCII symbol returns a broken one-byte symbol.** **verified**; filed as [LR05-15](Lambda_Issue_Ledger.md#lr05-15).
   - `'café'[3]` returns `'\xC3'`, while `len('café')` is 4 and `"café"[3]` is `"é"`.
   - `item_at` treats every symbol as ASCII (`lambda/runtime/lambda-data-runtime.cpp`, `item_at`). This violates S2.5.8.
   - The text sequence operations that read through `item_at` inherit it: `reverse('café')` is `'\xC3fac'` and `sort('bé')` is `'b\xC3'`.
3. **The markup formatters silently drop large text** (`lambda/format/format-utils.cpp:179`, in `format_markup_string_safe_ex`). **verified**, reproduced; filed as [LR09-31](Lambda_Issue_Ledger.md#lr09-31).
   - HTML and XML drop text over 1 MiB, logging only an error; attributes may reach 32 MiB.
   - Reproduced: a 1,200,000-character paragraph formats to 79 characters of HTML and 120 of XML.
   - JSX drops text over 10,000 bytes, and LaTeX drops strings of 64 KiB or more (confirmed in code).
   - See §8-2.
4. **`string(x)` of a float gives 6 significant digits** (`"0.493457"`), while `format` and printing give the shortest round-trip spelling (`0.49345686049371`). **verified**, behaviour. See §8-1.
5. **Survey only; verify before filing.**
   - **TOML:** calls `tracker.advance(-1)` (`input-toml.cpp`). The `size_t` parameter turns −1 into `SIZE_MAX`, so every later position points at the end of the file.
   - **JSON:** `skip_whitespace` moves the pointer without advancing the tracker, so line and column drift.
   - **YAML:** truncates a string at a `"\0"` escape, because `createStringItem(sb->str)` measures with `strlen`.
   - **XML:** `xml-stylesheet` detection uses an unbounded `strstr`/`strchr`.
   - **LambdaJS:**
     - `repeat` truncates counts of 2³¹ or more with `(int)n`.
     - On non-ASCII strings, three results are byte offsets instead of UTF-16 indices: the `replace` callback offset, regex `search()`, and the insertion points of `replaceAll("", r)`.
6. **Dropped:** a survey claim that HTML parsing stops at the first `<![CDATA[`. It did not reproduce: content after a CDATA section inside SVG survives.

### 5.2 Algorithmic and quadratic paths (technique G unless noted)

1. **`\u` escapes run `strlen` on the rest of the document.** **measured**
   - **Where:** `parse_escape_char` (`lambda/input/input-utils.hpp:124`), also `input-toml.cpp:55` and `input-mark.cpp:105`. Survey: the same per paragraph in `markup/inline/inline_special.cpp`.
   - **Why it's quadratic:** the decoder reads at most 10 bytes, but each escape measures everything after it.
   - **Measured:** quadratic (§4.6). Python's default `json.dumps` escapes all non-ASCII text this way.
   - **Fix:** `strnlen(*pos, 10)`, or pass the end pointer.
2. **YAML plain scalars walk back to the start of the line for every character.** **measured**
   - **Where:** `parse_plain_scalar` calls `line_start_pos` (`lambda/input/input-yaml.cpp:789`, defined at `:240`).
   - **Measured:** quadratic on long lines (§4.6).
   - **Fix (survey, exact equivalent):** test `p->pos == 0 || p->src[p->pos - 1] == '\n'` instead.
3. **`arena_owns` walks every arena chunk** (`lib/arena.c:578`). **measured**
   - **Why it's hot:** `list_reserve_capacity` (`lambda/runtime/collection_runtime.cpp:46`) calls it on every growth of an arena-backed list. A list being built lives in the newest chunk, at the end of the walk. This is 58% of Markdown parse time.
   - **Fix:** check `arena->current` first (Appendix A.2). Markdown parse drops from 304 to 151 ms.
4. **Shortest float spelling** (technique H; `lambda/core/lambda-decimal.cpp:106`). **measured**
   - **Why it matters:** it is 73% of JSON output time. It also serves LambdaJS number-to-string and, per S4.7.1, mixed float/decimal comparison and hashing.
   - **Measured:** 2.8× faster, with identical output.
5. **HTML: every non-ASCII byte becomes its own token.** **measured** cost; **survey** safety of the fix.
   - **Where:** the DATA-state batch scan stops at `c >= 0x80` (`lambda/input/html5/html5_tokenizer.cpp:38`).
   - **What happens (survey):** each such byte becomes a pool-allocated token of about 88 bytes plus an arena string, never freed, and is dispatched through the tree builder.
   - **Measured:** 3.5× the time and 4.3× the memory on Chinese text (§4.6).
   - **Fix:** drop `c >= 0x80` from the stop set. The survey reports that the per-byte paths emit such bytes unchanged, so output would not change.
6. **The pool allocator walks free lists** (`lib/mempool.c:314`). **measured**, profile.
   - **What happens:** `pool_take_block` calls `pool_find_suitable`, which walks a size bin's free list until a block fits. That is 22% of XML parse time and 19% of HTML.
   - **Also:** fixed-size commits (`pool_commit_more`) add `__mprotect` at 5–6%.
   - **Fix:** round the request up to the next bin and take its head, TLSF-style O(1), and grow commits geometrically. This needs its own design round.
7. **HTML scope checks compare tag names with `strcmp`.** **measured**, profile.
   - **Where:** the `has_element_in_scope_generic` family (`lambda/input/html5/html5_parser.cpp:377`).
   - **Cost:** up to a dozen `strcmp`s per open element per check; 22% of HTML parse time, plus about 5% in call stubs.
   - **Fix:** intern tag names to IDs at tokenization, so scope membership becomes a bit test.
8. **Markdown emphasis rescans the rest of the paragraph from every opener** (`find_all_runs`, `markup/inline/inline_emphasis.cpp`). It also mallocs a copy per `*` or `_`. **survey**
   - **Fix:** one CommonMark delimiter-stack pass.
9. **The XML formatter renders each map into a new buffer and copies it up** (`format_map_reader`, `lambda/format/format-xml.cpp:41`). **survey**
   - **Cost:** O(n × depth) bytes copied, and the buffers are never freed.
10. **UTF-8 walks from byte 0 for every character index.** **survey**
    - **Where:** `utf8_char_to_byte` (`lib/utf.c:176`) has no ASCII skip. On non-ASCII strings these all recompute from byte 0: `item_at`, `fn_substring` (three passes), `vector_text_items` (`reverse`/`sort`/`unique`), and LambdaJS `.length`, `charAt`, `charCodeAt` and `slice`.
    - **Cost:** per-character loops are O(n²). The ASCII case was fixed by the earlier string-tuning round (§3).
11. **LambdaJS `+=` always copies both sides** (`js_concat_strings_fast`, `lambda/js/js_runtime_value.cpp:1381`). **survey**
    - **Cost:** there is no rope and no append buffer, so building a string in a loop is O(n²). Seen in `revcomp.js` (per character), `prettier_ast.js` and `fast_diff.js`.
12. **Hidden `strlen` in four `lib/str.c` scanners:** `str_scan_quoted`, `str_scan_balanced`, `str_scan_balanced_quoted` and `str_scan_top_level`. **survey**
    - **Cost:** each call measures the rest of the buffer before a short scan.
    - **Callers in loops:** CSS animation, inline SVG rendering, `@font-face` parsing and the Markdown inline parser.

### 5.3 `lib/` kernels (fix once, benefit everywhere)

1. **One search kernel.**
   - **Now:** `literal_find` and `str_find` (`lib/str.c:288`) duplicate each other.
   - **Proposal:**
     - Make `str_find` the `memchr` candidate scan, with its existing second-byte filter, bounded at the last valid start.
     - Have `literal_find` call it (rule 13).
     - Add `str_rfind` as the backward twin, using the exact mask. This also fixes §5.1-1.
     - Route LambdaJS string `replace`/`replaceAll` through the kernel.
   - **Why the JS route matters:** they use libc `memmem` today, which is 4.7× slower than `str_find` on macOS (measured). On Windows the fallback calls `memcmp` at every position (survey).
2. **`str_is_ascii` stops early on every 8-byte word** (`lib/str.c:680`). A block OR-reduce is 5× faster (measured).
   - It runs on every string creation. `string_from_strview_arena` (`lib/string.c:18`) scans for ASCII and then copies the bytes. Merge the two into one copy-and-check pass (survey).
3. **`str_count_byte` (`lib/str.c:400`) and `utf8_count` (`lib/utf.c:121`):** count in `uint8_t` lanes per block (technique E). `str_count_byte` is now on the split/replace count path.
4. **Set scanners test membership by looping over the set string for every input byte.** **survey**
   - **Where:** `str_scan_until_any` and `str_skip_chars`, and their `strn_` variants, via `str_char_in_set`.
   - **Callers:** CSV unquoted fields (`input-csv.cpp:79`) and `skip_to_newline` (22 call sites across the kv, vcf, ics, eml and yaml parsers).
   - **Fix:** `memchr`, or two-byte SWAR, or a prebuilt table.
5. **`str_icmp_cstr` and `str_ieq_cstr`** run two `strlen`s and a table lookup per byte. The table is initialized lazily without synchronization. Hot in CSS selector matching. **survey**
6. **Hashing.** **survey**
   - SipHash-2-4 is the default for bytes, strings, pointers and integers (`lib/hashmap_helpers.h:67` and following).
   - On a miss, the name pool re-hashes every segment it probes and hashes the name twice more with FNV.
   - Radiant hashes whole image payloads byte by byte with FNV.
   - See §8-3.
7. **`lib/escape.c` offers only a per-character append callback.** Add a length-aware sink (`append_n`) so every escaper can bulk-copy clean runs (technique B). This enables §5.7-2.
8. **`stringbuf_append_char` (`lib/stringbuf.c:158`)** is why every per-character parser loop is slow. **survey**
   - `vappend_format` always runs `vsnprintf` twice.
   - Fix the callers with technique B rather than the function.

Already fast:
- `str_find_byte` equals `memchr` on arm64, and `str_find` beats macOS `memmem` 4.7× (measured).
- `str_to_lower` and `str_to_upper` are branch-free and vectorize.
- `StringBuf` growth doubles, and `stringbuf_append_str_n` and `append_char_n` are bulk operations.
- `stringbuf_append_long` avoids `snprintf`.
- HTML entity lookup is a binary search (survey).

### 5.4 Lambda runtime string builtins (survey unless marked)

1. **`fn_upper`/`fn_lower` pre-scan with an early exit** for a byte they would change. When nothing changes (DNA, digits), this is a full scalar pass for nothing. Fix: a block test (technique E).
2. **`trim`, `trim_start` and `trim_end` rescan the result for ASCII**, although the source's flag already proves it (technique F).
3. **`fn_substring` on non-ASCII text** counts the whole string, walks from byte 0 twice, and sets `is_ascii = 0` unconditionally. `slice` and `drop` add a fourth pass (techniques F and G).
4. **`split(s, "")` allocates a string per character**, although an interned one-character ASCII table exists (`get_ascii_char_string`). LambdaJS `split("")` in `hyphen.js` and `revcomp.js` lands here.
5. **`normalize()` has no ASCII shortcut**, although every normalization form leaves ASCII text unchanged.
6. **Regex `find`/`replace` do extra work on every call** (`lambda/runtime/re2_wrapper.cpp:905`).
   - They count every match with a full regex pass before the real pass, though only `{last: N}` needs the total.
   - `{ignore_case: true}` compiles and frees a new regex on every call.
   - regexredux2 makes nine such calls. They account for about 0.38 ms of its remaining 0.6 ms (measured on an older release binary).
7. **`string(int)` does two mallocs and two frees per call.** A stack buffer suffices.

Already fast:
- literal `split`, `replace` and `find` (T28-5, T28-5.1);
- O(1) `len`, indexing and slicing for ASCII strings, and interned one-character ASCII strings (§3).

### 5.5 LambdaJS (survey unless marked)

1. **Literal regexes match with `memcmp` at every position** (`lambda/js/js_runtime.cpp:16748`). **verified**, by code
   - These are regexes like `/,/g` and `/\./g`, used by `exec`, `test`, `match`, `replace`, `search` and `split`. Bulk replace runs the matcher once per match.
   - It is the loop T28-5.1 removed from Lambda.
   - **Fix:** the shared kernel (§5.3-1).
2. **`toUpperCase`/`toLowerCase` have no ASCII path.** They call utf8proc per character.
   - They also intern their results into the never-collected name pool, as `trim*` does.
   - Seen in `revcomp.js` and `knucleotide.js`.
3. **`+=` copies both sides every time.** See §5.2-11.
4. **UTF-16 indexing walks from byte 0:** `.length`, `s[i]`, `charAt`, `charCodeAt`, `slice`. See §5.2-10.
   - Also add `charAt` and two-argument `indexOf` to the ASCII fast path; `fast_diff.js` uses both.
5. **`indexOf`, `includes`, `startsWith` and `endsWith` re-encode both strings on every call** for non-ASCII text.
6. **Regex `split` calls `exec` at every position**, with no bulk path. Bulk `replace`/`match` refuse non-ASCII input.
7. **Smaller:**
   - `repeat`, `padStart` and `padEnd` copy two to four times; `prettier_ast.js` calls `" ".repeat(column)` for every line.
   - `localeCompare` and `normalize` lack an ASCII shortcut.
   - String `<`/`>` decode one code unit at a time instead of using `memcmp`.

Already fast:
- JS `split` with a string separator goes through `fn_split` (T28-5).
- One-argument `indexOf` and `charCodeAt` have allocation-free ASCII fast paths.
- JS strings share Lambda's `String` layout, so `is_ascii` applies.

### 5.6 Input parsers

**Shared by several parsers:**
- **`SourceTracker::advance`** (`lambda/input/source_tracker.cpp:57`) tracks line and column per byte (technique F). It is JSON's hot path; its upper bound is 1.4× (measured). TOML, INI/properties and the graph DSL parsers use it too (survey).
- **`ElementBuilder` interns the tag name twice per element,** and `attr(const char*)` re-measures and re-interns every attribute name (`lambda/io/mark_builder.cpp`). Every tree parser pays this. **survey**
- **`skip_whitespace` and block-comment `*/` scans** go one byte at a time. **survey**

**JSON: 191 MiB/s (Node 479)**
- **`parse_string`** (`lambda/input/input-json.cpp:40`) does per-byte checks, `stringbuf_append_char` and `tracker.advance(1)` for every character.
  - Bulk runs alone gave 1.07× (measured); with lazy tracking, up to 1.4×.
  - A string with no escapes can be created straight from its source slice.
- **Object keys are copied three times:** buffer, then arena string, then interned name. **survey**
- **Numbers are converted twice:** `strtod` finds the end (`input-json.cpp:97`), then `parse_scanned_decimal_number` converts again. **survey**

**CSV: 234 MiB/s**
- **`parse_csv_field` is 34% of parse time** (measured). **survey**, for the causes:
  - Quoted fields append one character at a time.
  - Unquoted fields use the per-byte set scanner (§5.3-4) and are copied twice.
  - Empty cells re-intern the header name.

**XML: 162 MiB/s**
- **Allocation is 45% of parse time** (measured): §5.2-6 plus `ElementBuilder`.
- **Comment and CDATA ends are found with `strncmp` at every offset** (`lambda/input/input-xml.cpp:196` and `:230`): technique A. **verified**
- **Text and attribute values append one character at a time;** `stringbuf_append_char` is 8% (measured). Names are copied three or four times (survey).

**YAML: 71 MiB/s**
- **The quadratic line-start walk** (§5.2-2).
- **Scalars are built one character at a time.** **survey**
  - Quoted and block scalars append per character.
  - Every scalar mallocs and frees its own buffer; the allocator is 29% of parse time (measured).
  - Keys are copied three times.
  - `make_scalar` repeats `strlen` and makes about 20 `strcmp`s.

**HTML: 68 MiB/s on English text, 23 MiB/s on Chinese**
- **The largest costs** are covered above: non-ASCII tokens (§5.2-5), scope `strcmp` (§5.2-7) and the allocator at 34% (§5.2-6).
- **The tree builder inserts text one character at a time** (`html5_insert_character`, `lambda/input/html5/html5_parser.cpp:732`; 7%, measured). It does so after copying the token text into an arena string it never uses again (survey).
- **Attributes and entities.** **survey**
  - Attributes are scanned one character at a time and built twice, with an O(attributes²) duplicate check.
  - Every element walks the open-element stack to find its SVG namespace.
  - A bare `&` scans about 106 legacy entity names linearly.
- **Already fast:** ASCII text in the DATA, RCDATA and RAWTEXT states is batched, line tracking is lazy, and entity lookup is a binary search (survey).

**Markdown: 30 MiB/s**
- **The arena walk (§5.2-3) is 58% of parse time, and `strpbrk` is 12%** (measured).
- **The inline loop's prefilter rarely helps.** Its stop set includes `\n`, `:` and `'`, so most multi-line paragraphs fall back to per-character handling. **survey**
- **Text is copied and scanned repeatedly.** Paragraph text is copied about five times, `splitLines` mallocs one buffer per line, and several block detectors re-scan each line. **survey**
- **Already fast:** code blocks append whole lines and code spans copy slices (survey).

**CSS tokenizer (survey)**
- Preprocessing checks every byte for NUL and 0xED; use `memchr`.
- Comments are scanned one byte at a time.
- Every token's text is duplicated with `pool_dup_n`, and numbers are duplicated just to add a NUL.
- Identifier characters are classified by a function call per character.
- **Already fast:** tokens are slices, `memchr('\\')` gates unescaping, and property lookup is hashed.

**PDF**
- **`parse_stream` finds `endstream` with `strncmp` at every byte** (`lambda/input/input-pdf.cpp:920`): technique A. Streams are most of a PDF's bytes. **verified**
- **`endobj` is found the same way.** **survey**

**TOML, Mark, RFC (EML/VCF/ICS), RTF, INI/properties, JSX (survey)**
- Strings are built one character at a time.
- TOML multi-line strings also `strncmp` the delimiter at every byte.

### 5.7 Output formatters

Measured shares (§4.3):
- **JSON:** floats 73%.
- **HTML:** escaping 27%, tag-name compares 22%, `format_contains_complex` 17%.
- **XML:** escaping 30%, `format_contains_complex` 18%, tag compares 11%.
- **Markdown:** escaping 51%, `strcmp` 13%.

1. **Floats.** See §5.2-4.
2. **Escaping** (technique B, using the sink from §5.3-7):
   - **JSON strings:** `escape_append_json_common` (`lib/escape.c:170`) does a linear rule search and an indirect `append_char` per byte. It also serves `JSON.stringify`, validator output, radiant `view_pool` and `serve` (survey).
   - **Generic and quoted escapes:** `escape_append_common` (`:549`) and `escape_append_quoted_to` (`:243`) serve YAML, INI/properties, JSX, graphs, TOML, Mark output and radiant SVG/paint (survey).
   - **HTML/XML text and attributes:** `format_markup_string_safe_ex` (`lambda/format/format-utils.cpp:162`) runs a `switch` and an append per byte, 27–30% of output (measured). XML also escapes every non-ASCII byte with its own `snprintf`, three per CJK character (survey).
   - **Markdown, RST and Wiki:** `format_text_with_escape` (`lambda/format/format-utils.cpp:43`) walks the whole escape set for each byte; 51% of Markdown output (measured).
3. **Integers:** each `int` costs two mallocs, two `vsnprintf`s and two frees, via `format_number_impl` → `print_int_value` into a temporary `StrBuf` (survey).
4. **Tag dispatch:** `strcmp`/`strncasecmp` chains run per element, 22% of HTML output (measured). A `MarkupNameId` enum already exists in `well_known_markup_names.h` (survey).
5. **`format_contains_complex`** (`lambda/format/format.cpp:14`) walks the whole tree before every `format()` call, 17–18% of HTML/XML output (measured). Fold it into the formatting walk (technique F). `lambda convert` calls the formatters directly and skips it (survey).
6. **Smaller (survey):**
   - `write_indent` appends two spaces per level, with a `strlen` each, in JSON and YAML output.
   - `vappend_format` is used for plain `"%.*s"` copies.
   - YAML quote detection makes 16 `strchr` passes plus `strtol`/`strtod` per string.
   - Markdown blockquotes are re-copied byte by byte at every nesting level.
   - The CSS formatter allocates a buffer per value.
   - Output files are written with `fprintf("%s")`.

Already fast (survey):
- Buffer growth doubles.
- Plain text, LaTeX text and code blocks are appended in bulk.
- HTML void, raw and boolean element lookups are binary searches.
- `stringbuf_vemit` flushes literal text in chunks.

---

## 6. Proposed phasing

- **P0 — correctness.** File each item in the Issue Ledger first.
  - §5.1-1 `str_rfind_byte`, with tests.
  - §5.1-2 symbol indexing.
  - Decide §5.1-3 and §5.1-4 (§8).
  - Verify, then fix, the survey-only items of §5.1-5.
- **P1 — few-line fixes with measured ratios.** Each ships with a before/after number.
  - §5.2-1 (`strnlen` for `\u`)
  - §5.2-2 (YAML line start)
  - §5.2-3 (`arena_owns`)
  - §5.2-4 (float probes)
  - §5.2-5 (HTML non-ASCII stop)
  - §5.5-1 (JS literal regexes onto the kernel)
- **P2 — shared kernels in `lib/`.**
  - §5.3-1 one search kernel, plus `str_rfind`.
  - §5.3-7 the escape sink, plus a "next byte needing escape" scanner.
  - §5.3-2 and §5.3-3 block loops.
  - §5.3-4 set scanners.
- **P3 — parser inner loops.**
  - Bulk runs in JSON, CSV, XML and YAML, and in HTML text insertion.
  - A lazy `SourceTracker`.
  - Names and keys created from source slices.
  - The PDF stream search.
- **P4 — formatter loops.**
  - Roll out escaping.
  - Integer formatting.
  - Tag dispatch by ID.
  - Fold in `format_contains_complex`.
- **P5 — structural.**
  - HTML tag IDs and scope bitsets.
  - Pool allocator O(1) bins and geometric commit growth.
  - A LambdaJS string builder or rope for `+=`.
  - A UTF-8 position cache for sequential indexing.
  - The hash choice (§8-3).

---

## 7. Verification protocol

- **Semantics unchanged:**
  - **Differential fuzz:** fuzz the changed builtin or parser against a control binary built from the same HEAD, and require byte-identical output. T28-5.1 used 4,000 random `replace`/`find`/`split` cases over a dense six-letter alphabet that included a two-byte UTF-8 letter, with every option kind.
  - **Golden cases:** add new cases to `test/lambda/*.ls` with their `.txt`.
  - **Test suites:** `make test-lambda-baseline` must pass 100%. For changes visible to LambdaJS, also run the test262 baseline (CLAUDE.md rule 18).
  - **Structural pins:** add one wherever a regression would be silent, like `LambdaOptStrings.LiteralSplitKernelAvoidsBytewiseComparisons`.
- **Speed:**
  - Compare release builds of the same HEAD with and without the change (CLAUDE.md rule 10), under `LAMBDA_TIER=jit`.
  - Take the median of at least 7 runs, interleaved when the difference is under about 5%.
  - Report peak RSS for any change that affects memory.
- **Inputs:**
  - the synthetic corpus of §4.1;
  - non-ASCII variants (Chinese HTML);
  - the pathological cases of §4.6;
  - the benchmark rows that touch the changed code.
- **Profiles:** see Appendix A.4.

---

## 8. Open questions for the user

1. **Is `string(float)`'s 6-digit spelling intended?** S4.8.1 says floats print as their shortest round-trip decimal and that the printer is injective. Yet `string(0.49345686049371)` returns `"0.493457"`. If `string()` falls under S4.8.1, this is a defect; if not, the difference should be ruled explicitly. This proposal changes no ruling.
2. **What should the HTML/XML formatter's 1 MiB text guard do?** (§5.1-3) It may be an intended cap, or a leftover from memory-safety debugging. Either way, silently dropping text looks wrong; the options are to fail loudly or to remove the cap.
3. **Which hash where?** (§5.3-6) SipHash may be deliberate, for hash-flooding resistance. One option: keep it for keys an attacker can control (maps built from input data) and use XXH64 or wyhash for internal tables such as the name pool and shapes.
4. **Allow hand-written SIMD?** (§2.4) Either allow a small NEON/SSE2 kernel in `lib/` behind `#if`, with a scalar fallback for the WASM and other builds, or keep to `memchr` and compiler vectorization only.
5. **What end state for float spelling?** (§2.3 H) Keep the three-probe loop (identical output, 2.8×), or move to a Ryu/Schubfach-class algorithm (roughly another order of magnitude). The latter would need byte-identical spelling validated on a large corpus before switching.

---

## Appendix A — Implementation notes

### A.1 The landed kernel (T28-5.1)

`literal_find` (`lambda/runtime/lambda-eval.cpp:6909`) is the case-sensitive core. `ignore_case` probes every position with ASCII folding instead, because `memchr` cannot fold case.

```cpp
const unsigned char first = (unsigned char)needle[0];
while (from <= last) {
    const char* hit = (const char*)memchr(chars + from, first, last - from + 1);
    if (!hit) return SIZE_MAX;
    size_t at = (size_t)(hit - chars);
    if (needle_len == 1 || memcmp(hit + 1, needle + 1, needle_len - 1) == 0) return at;
    from = at + 1;
}
```

`count_literal_matches` sends a one-byte, case-sensitive needle to `str_count_byte`. `replace()` of one byte by one byte at every match calls `translate_byte` (`:7599`). In the release binary, `fn_replace` contains this loop:

```text
cmeq.16b  v6, v2, v0      ; four 16-byte compares against the old byte
cmeq.16b  v7, v3, v0
cmeq.16b  v16, v4, v0
cmeq.16b  v17, v5, v0
bit.16b   v2, v1, v6      ; four 16-byte selects of the new byte
bit.16b   v3, v1, v7
bit.16b   v4, v1, v16
bit.16b   v5, v1, v17
```

### A.2 The experiment patches (scratch, reverted)

- **`arena_owns`:** before the chunk walk, return true when `ptr` lies in `arena->current`'s used range. The result is the same as the walk's, only found first.
- **Float probes:** in `lambda_finite_double_to_shortest`, loop over `prec` from `DBL_DIG` (15) to `DBL_DECIMAL_DIG` (17), and parse back with `strtod` instead of `sscanf`. The rest of the routine, including trailing-zero stripping, is unchanged.
- **Tracker upper bound:** `SourceTracker::advance(n)` only moves the pointer and offset. Line and column become wrong; this is for measuring only. The real fix computes them on demand from the offset and a lazily built line index.

### A.3 The exact SWAR zero-byte mask

With `x = word ^ broadcast(c)`, the high bit of each byte of `~(((x & 0x7F7F7F7F7F7F7F7F) + 0x7F7F7F7F7F7F7F7F) | x | 0x7F7F7F7F7F7F7F7F)` is set exactly when that byte of `word` equals `c`, with no borrow between bytes. Use it wherever more than the lowest flagged byte is read: backward searches, and counting via popcount.

### A.4 Profiling recipe

- **Build:** `make build-release-profile` builds `lambda-profile.exe` with local symbols kept. The normal release build strips them, which hides `static` functions.
- **Run:** loop the workload inside one process so it lasts a few seconds. Run it in the background and attach with `sample <pid> 3 -file out.txt`.
- **Read:** "Sort by top of stack" gives the leaf shares. Walk the call tree above a leaf to attribute it to its callers.
- **Caution:** the working-tree `lambda.exe` is often a debug build; never time it (CLAUDE.md rule 10). Archived release binaries live in `test/benchmark/exe/`.
