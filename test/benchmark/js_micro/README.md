# LambdaJS property/element micro-benchmarks

One hot loop each, `Date.now()` around the loop only. Used to isolate the
per-operation costs analysed in
[`vibe/jube/JS_Tune10_Fast_Paths.md`](../../../vibe/jube/JS_Tune10_Fast_Paths.md)
§2.3 and to A/B its fixes (§8.3).

```bash
for f in test/benchmark/js_micro/*.js; do ./lambda.exe js "$f"; done
```

Run against a **release** build, and A/B against an archived binary in
`test/benchmark/exe/` rather than a rebuild.

| Script | Isolates |
|---|---|
| `index.js` | `a[i]` read+write on a plain array (T10-1 numeric key lane) |
| `ctor.js` | `new P(x,y)` — two property *adds* per call (T10-3) |
| `lit.js` | `{x, y}` object literal allocation (T10-2 item 1, still open) |
| `named.js` | `p.x` reads and existing-slot `p.x =` writes (T10-2 item 3) |
| `args_fp.js` / `args_ctl.js` | a body that reads `o.arguments` vs `o.args` — these two must time the same; a gap means the `arguments` observation walk is reading key positions as references again (T10-0) |
