# Vendored jqjs

`vendor/jqjs.js` is a vendored copy of [jqjs](https://github.com/mwh/jqjs), a
pure-JavaScript jq interpreter by Michael Homer (MIT, `vendor/LICENSE-jqjs.txt`).
It is the jq engine of the Node.js and LambdaJS columns of the `jq_*` rows in
the text benchmark suite (`vibe/impl/Lambda_Impl_Jq_Tests.md`).

| | |
|---|---|
| Upstream | https://github.com/mwh/jqjs |
| Commit | `f2894f6` ("Fix update() to auto-instantiating missing nested objects") |
| Local patches | `patches/jqjs-lexical-scope-cow-add.patch` |

**Why git master, not npm.** The npm release (`@michaelhomer/jqjs` 1.6.0) has
no `try … catch`, and fails `.a += 1` and `//=` on missing keys. Master fixes
all three.

**The patch is already applied to `vendor/jqjs.js`.** It is the record of our
delta versus upstream, approved by the user on 2026-10-05 (CLAUDE.md rule 16).
It makes three changes:

1. **Lexical scopes.** Function arguments and variables were dynamically
   scoped. That cost O(call depth) per argument lookup and gave wrong results
   for recursive functions such as `ack($m; $n)`.
2. **Copy-on-write assignment.** `|=` and `=` used to deep-copy the whole
   input on every assignment. They now copy only the containers on each
   updated path.
3. **Linear `add`.** `add` no longer re-copies the running sum per element.

With jq 1.7.1's `tests/jq.test` and jqjs's `run-test.js`, the pass count went
from 339 to 341 with no newly failing case. On `jq_bf` the patch makes jqjs
12.6× faster, and on `jq_mix` 2×, with identical checksums.

The benchmark README labels the Node column **jqjs + patches**.

## Verifying

```bash
git clone https://github.com/mwh/jqjs temp/jqjs-verify
git -C temp/jqjs-verify checkout f2894f6
git -C temp/jqjs-verify apply "$PWD/patches/jqjs-lexical-scope-cow-add.patch"
diff temp/jqjs-verify/jq.js test/benchmark/text/jq/vendor/jqjs.js && echo VERIFIED
```

Run this after any hand edit of `vendor/jqjs.js`. Edits belong in the patch:
regenerate it from a jqjs checkout (`git diff f2894f6 HEAD`) and keep the
header that describes it.

## Re-syncing to a newer upstream

1. Clone upstream at the new commit.
2. Apply the patch and fix it up if it no longer applies.
3. Re-run `run-test.js` against jq's `tests/jq.test`, before and after the
   patch, and compare the failing-case lists.
4. Copy `jq.js` to `vendor/jqjs.js` and update the commit above.
