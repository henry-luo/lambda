# Lambda Syntax Design Decisions

This document records design discussions and rationale for Lambda's syntax choices.

---

## Concatenation Operator: `++` vs `.+`

**Date**: 2025-02-10  
**Decision**: Keep `++`

### Context

Considered changing the concatenation operator from `++` to `.+`.

### Arguments Against `.+`

1. **Parsing ambiguity with `.`**: Lambda heavily uses `.` for member access (`person.name`) and path literals (`.src.main.ls`). An expression like `a.+b` becomes ambiguous — is it `a .+ b` (concat) or `a.` `+b` (member access + unary plus)?

2. **Misleading semantics**: In MATLAB and Julia, `.+` means *element-wise addition*, not concatenation. Developers coming from those languages would expect `[1,2] .+ [3,4]` to yield `[4,6]`, not `[1,2,3,4]`.

3. **Established convention**: `++` is the standard concatenation operator in Haskell, Erlang, and Elixir — languages that share Lambda's functional DNA. It's immediately recognizable to that audience.

### Arguments For `.+`

- Could be read as "collection-level plus" — adding collections together.
- One fewer character to type (marginal).

### Conclusion

**Keep `++`**. The parsing ambiguity with `.` is a real technical risk given how central dot notation is to Lambda (paths, member access), and the semantic mismatch with MATLAB/Julia's `.+` convention would confuse more people than it helps. `++` is well-understood, unambiguous, and idiomatic for a functional language.
