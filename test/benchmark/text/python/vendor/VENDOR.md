# Vendored purejq

`purejq/` is an unmodified copy of [purejq](https://github.com/adam2go/purejq)
0.3.1 (tag `v0.3.1`, commit `ca3f769`, MIT, `LICENSE-purejq`). It is a pure-
Python jq interpreter that compiles a filter to Python closures. It has no C
extension and no runtime dependencies; `orjson` is only an optional CLI
speed-up and is not used here.

It is the jq engine of the Python column of the `jq_*` text benchmark rows
(`vibe/impl/Lambda_Impl_Jq_Tests.md`), driven by `../jq_common.py`. There are
**no local patches**. The driver raises Python's recursion limit and runs on a
thread with a 512 MB stack, because purejq evaluates `until`/`recurse` by
recursion and `jq_bf` would otherwise hit `RecursionError`.

Re-sync: copy `src/purejq/*.py` and `py.typed` from the new tag, then re-run
the four `jq_*.py` scripts and check their checksums.
