# Vendored RE2

This is a vendored copy of [RE2](https://github.com/google/re2), the regular
expression engine behind Lambda string patterns (`lambda/runtime/re2_wrapper.cpp`),
the LambdaJS RegExp fast path, the hosted Python and Ruby `re` modules, and the
grep library (`lib/grep`). It moved in-tree from a setup-script clone in
`build_temp/re2-noabsl` so that local patches have an auditable home (GRP15 in
`vibe/Lambda_Lib_Grep.md`).

| | |
|---|---|
| Upstream | https://github.com/google/re2 |
| Tag | `2023-03-01` |
| Commit | `3a8436ac436124a57a4e22d5c8713a2d42b381d7` (2023-02-20) |
| Local patches | `patches/re2-simd-prefix-accel.patch` |

The tag is deliberate: it is the last release line before RE2 began requiring
Abseil (GRP16). Upgrading means vendoring and building Abseil on every platform;
backport an individual upstream fix as a patch instead.

**The patches under `patches/` are already applied to the source here.** They
are kept as the record of our delta versus upstream, so a future re-sync can
replay them onto a newer RE2. They are *not* applied at build time — that would
rewrite tracked files on every build.

`make verify-re2-patches` checks the invariant: it clones pristine upstream at
the commit above, applies every `patches/re2-*.patch`, and diffs the result
against this directory. Run it after editing anything here by hand.

## What is vendored

Only what `libre2.a` needs, plus licence and build files — about 0.8 MB:

- `re2/*.cc`, `re2/*.h` — the whole library, including `filtered_re2`, `set` and
  the Unicode tables
- `util/` — only the headers the library includes (`logging.h`, `mix.h`,
  `mutex.h`, `strutil.h`, `utf.h`, `util.h`) and the two sources it compiles
  (`rune.cc`, `strutil.cc`)
- `CMakeLists.txt`, `re2.pc.in`, `re2Config.cmake.in` — the build is still
  RE2's own CMake; the two `.in` files are read at configure time
- `LICENSE`, `README`, `AUTHORS`, `CONTRIBUTORS`

Dropped from upstream: `re2/testing`, `re2/fuzzing`, the Unicode table
generators (`make_*.py`, `make_perl_groups.pl`, `unicode.py`), `util/test.*`,
`util/benchmark.*`, `util/pcre.*`, `util/fuzz.cc`, `util/flags.h`,
`util/malloc_counter.h`, `doc/`, the Bazel and Python packaging, and
`libre2.symbols*` (shared-library export lists; Lambda links statically).
CMake is configured with `-DRE2_BUILD_TESTING=OFF`, so none of the dropped
sources is referenced.

Internal headers such as `re2/regexp.h` are vendored as part of the library and
may be included by Lambda code that needs them (GRP7: only
`lib/grep/grep_literal.cpp`, never a public header). A shared `libre2` would not
export their symbols; this is one reason RE2 stays a static library.

## Building

Driven from the top-level Makefile, out of tree, so the vendored directory stays
clean:

```
make build                 # builds build_temp/re2_build/libre2.a on demand
make verify-re2-patches    # lib/re2 == upstream + patches/re2-*.patch
```

The include root is `lib/re2` (`#include <re2/re2.h>`).

## Re-syncing to a newer upstream

1. Clone upstream at the new commit (it must not require Abseil, or Abseil has
   to be vendored first).
2. Apply each `patches/re2-*.patch`; fix up any that no longer apply.
3. Copy the vendored subset above over this directory.
4. Update the commit in this file and `RE2_UPSTREAM_COMMIT` in the Makefile.
5. `make verify-re2-patches && make build && make test-lambda-baseline`.

## Local patches

### `re2-simd-prefix-accel.patch` — SSE2 and NEON prefix acceleration

`Prog::PrefixAccel_FrontAndBack` skips ahead to places where a pattern's
literal prefix can start by testing the prefix's first and last byte together,
32 bytes at a time — but only under `__AVX2__`, which Lambda never builds with,
so every Lambda target fell back to `memchr` on the first byte alone and
stopped at every occurrence of it. The patch adds the same test 16 bytes at a
time with SSE2 (always available on x86-64, so no runtime detection) and NEON
(ARM64; a narrowing shift stands in for the missing movemask). AVX2 stays first
for any build that enables it. Touches only `re2/prog.cc` (GRP9v2,
`vibe/Lambda_Lib_Grep.md` §6.1).

Reach: every RE2 search whose pattern starts with a case-sensitive literal of
two or more bytes — Lambda string patterns in `find`/`replace`/`split`, the
LambdaJS RegExp fast path, the hosted Python and Ruby `re` modules. A
case-insensitive prefix uses RE2's shift-DFA kernel and is unaffected.

Measured on Apple M-series (64 MB haystack, no match, release flags): `hello\w+`
0.55 → 12–13.6 GB/s, `the zebra` 0.97 → 5.8–6.2 GB/s, `hz[0-9]` 0.61 → 40–46
GB/s; a prefix whose first byte never occurs (`qq\d`) 62 → 43–46 GB/s, where
plain `memchr` was already at memory speed. Correctness:
`Re2PrefixAccel.MatchesNaiveSearchAtEveryAlignment` in
`test/lib/test_grep_gtest.cpp` (NEON); the SSE2 path was checked with the same
harness built for x86_64 and run under Rosetta (120,000 checks).
