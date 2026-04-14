# Octane Benchmark Results — LambdaJS vs Node.js

Tested: 2026-04-14 (Debug build)

## Summary

| Benchmark     | Node.js | LambdaJS | Status         |
|---------------|---------|----------|----------------|
| earley-boyer  | PASS    | PASS     | 100% match     |
| regexp        | PASS    | FAIL     | Wrong checksum |
| box2d         | PASS    | FAIL     | TypeError      |
| code-load     | PASS    | —        | Skipped (requires `eval()`) |
| pdfjs         | PASS    | —        | Skipped (requires TypedArrays + canvas) |
| typescript    | PASS    | —        | Skipped (large compiler, not tested) |

## earley-boyer — PASS

Output is identical between Node.js and LambdaJS:

```
=== Earley Benchmark ===
132
Earley result: undefined
Earley: PASS
=== Boyer Benchmark (n=0) ===
95024 rewrites
Boyer result: undefined
Boyer: PASS
=== Boyer Benchmark (n=1) ===
591777 rewrites
Boyer(1) result: undefined
Boyer(1): PASS
=== Boyer Benchmark (n=2) ===
1813975 rewrites
Boyer(2) result: undefined
Boyer(2): PASS
```

## regexp — FAIL: Wrong checksum

The Octane harness replaces `Math.random` with a deterministic PRNG so the regexp benchmark produces a stable checksum of `1666109`. With this seeded RNG, Node.js passes but LambdaJS fails.

**Root cause:** LambdaJS rejects regex backreference `\4`:

```
js regex compile error: /^(\[) *@?([\w-]+) *([!*$^~=]*) *('?"?)(.*?)\4 *\]/: invalid escape sequence: \4
```

`\4` is a valid backreference to capture group 4. The LambdaJS regex engine (RE2-based) does not support backreferences, causing some regex matches to return wrong results and shifting the checksum.

## box2d — FAIL: TypeError

```
TypeError: Cannot read properties of undefined (reading 'y')
```

**Root cause:** Property chain resolution fails on `b2Mat22` matrix objects. Trace shows:

```
.R  obj_t=18 → res_t=18    (correct: object returns object)
.col1  obj_t=7 → res_t=26  (BUG: float returns undefined)
```

A `b2Mat22` has `.col1` / `.col2` sub-objects (vectors). Somewhere the `.R` lookup resolves to a float (type 7) instead of the expected `b2Mat22` object, so `.col1` returns `undefined` and `.y` on that throws TypeError.

This is a property access or prototype chain bug in the JS engine affecting deeply nested object structures.
