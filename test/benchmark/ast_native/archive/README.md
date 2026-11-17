# Retired ARM64 AST emitter

Removed from all build paths on 2026-11-18 at the user's request (D8.3.1v3).
The `.cpp.txt` and `.hpp.txt` files preserve the prototype without making them
compiler inputs. Fixtures and their goldens are historical. The focused runner
requires the old `temp/arm64_baseline/lambda-release` executable; current Lambda
builds have no `ast-native` selector, state, dispatch, or teardown hooks.

See [the measured comparison](../../ARM64_AST_Baseline_Comparison_2026-11-17.md).
