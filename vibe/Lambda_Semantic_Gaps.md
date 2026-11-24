# Lambda semantic gaps

**Status** — Audit findings and proposed clarifications, 2026-11-24. Decisions remain with the formal specifications.

**Scope** — Special values and the operations that consume them, reviewed against semantics 102.0.0 and design 70.0.0. The highest-priority decision is how a failed predicate affects the enclosing computation. The same three-row comprehension already returns different rows depending on the execution tier. [Formal semantics](../doc/Lambda_Formal_Semantics.md) · [Formal design](../doc/Lambda_Formal_Design.md) · [Reproduced issues](Lambda_Issue_Ledger.md#formal-semantics-edge-cases-20261124)

## Predicate failure changes the selected rows

S3.1v2 gives an error no Boolean truth value. S3.4–S3.5 say exactly what happens in a logical expression or an `if` condition. An encountered error becomes the expression result and unselected operands do not run. S11.4.11 separately says that an error answer fails a type constraint. A collection predicate needs a rule of its own. Consider this top-level binding.

```lambda
let e = error("guard")
let rows = [for (i in [1, 2, 3] where (if (i == 2) e else true)) i]
```

| Middle predicate | Interpreter | Eager JIT |
|---|---|---|
| `true` | `[1, 2, 3]` | `[1, 2, 3]` |
| `false` | `[1, 3]` | `[1, 3]` |
| `e` | `[1, 3]` | `[1, 2, 3]` |

The error row reproduced twice on each tier. Both tiers dropped the row in the corresponding `|:` filter. The `where` difference violates S1.6 regardless of which failure policy is chosen. The formal decision should identify who receives the error, whether later items run, and whether earlier rows or procedural effects survive. TE-22 flags collection predicates, quantifiers and procedural loop conditions as open. [Predicate decision record](Lambda_Design_Type_Enforcement.md#te-22--errors-have-no-truth-value-decided-2026-10-16-user)

## Conflicting rulings with observable consequences

### Deep equality and system faults

S5.1.3 and S7.4.3 classify structural equality depth exhaustion as a system fault. S7.11.1v2 explicitly classifies it as an ordinary error, and D1.4 requires ordinary failures to return through each frame. The difference determines whether an `fn` caller can inspect or handle the comparison result. SI13 additionally describes the depth limit as protection against imported cycles. Two independently constructed native arrays nested 270 levels yielded an ordinary error on both tiers. Align the earlier rulings with S7.11.1v2 and state whether the limit applies to native acyclic data. [Equality](../doc/Lambda_Formal_Semantics.md#s51-error-preserving-deep-value-equality) · [Fault recovery](../doc/Lambda_Formal_Semantics.md#s711-system-faults-and-recovery)

### Exact integer sum and sequential addition

Let `m = 9007199254740991`, the largest finite Lambda `int`. S4.1.3 gives `sum([m, 1, -1]) == m` because the final mathematical sum is in band. Under S4.1.2 a left fold first computes `m + 1 == inf`; subtracting one leaves infinity. S7.3.1 nevertheless calls the equivalence to a sequential addition fold literal, and S13.4.1 keeps user folds sequential. Both tiers returned `inf` for both expressions. The integer decision record explicitly chooses the exact final sum. Preserve that decision and state S7.3.1's null-propagation law independently of the fold identity. [Integer decision](Lambda_Semantics_Int_Type.md#52-poison-algebra--the-lane-must-branch-what-ieee-gave-free) · [Aggregation](../doc/Lambda_Formal_Semantics.md#s73-aggregation)

### Empty strings, symbols and binaries

S2.2.2v2 says that constructing a zero-length symbol or binary yields `null`. The predefined empty name, `symbol.empty`, is an exception. S2.5.8 and S10.1.6v2 say a text filter preserves its source kind and describe its empty result as `""`. Filtering out the sole member exposes the disagreement.

| Source | Predicate | Result on both tiers |
|---|---|---|
| `"a"` | Reject the character | `""` |
| `'a'` | Reject the character | Ordinary error |
| `b'\x61'` | Reject the byte | A value for which `is binary` is true and `len` is zero |

Keeping the sole symbol or byte preserved its kind. Specify the zero-survivor result separately for each text kind. Two related boundaries need the same precision. S8.2.4v3 describes positional windows as arrays, while S2.5.8, S7.1.2v2 and both tiers preserve text kinds on nonempty slices. S11.5.4 distinguishes a present zero-count native span from an absent optional span and promises an owned copy for `binary[n]`. What Lambda value represents that present zero-byte copy? [Solid values](../doc/Lambda_Formal_Semantics.md#s22-empty-and-solid-values) · [Physical binary schemas](../doc/Lambda_Formal_Semantics.md#s115-physical-binary-schemas)

### Element length and the last child

For `e = <row attr: 7, 10>`, both tiers returned `[2, 10, null]` for `[len(e), e[last], e[len(e) - 1]]`. S7.2.2 defines `last` by the third expression. S8.1.2v3 and S8.3.1v3 explain the difference. The value walk counts the attribute and child, while integer positions address children alone. Defining `last` using the size of the indexed axis preserves both the length law and the existing fixture. [Indexing ruling](../doc/Lambda_Formal_Semantics.md#s72-indexing-and-last) · [Existing element fixture](../test/lambda/mvp/mir_mvp_last_index.ls)

## Special values in numeric operations

| Rulings | Witness | Gap or proposed clarification |
|---|---|---|
| S1.9, SI6, S4.2.3, S4.8.1–S4.8.2, S5.2.1v2 | `inf == decimal.inf` is true, yet they must print differently. `-0.0 == 0.0` also holds. | The print/equality law names only NaN as an exception. Give value equality, canonical numeric text, and bit-preserving round trips separate guarantees. |
| S4.1.4, S4.4.1–S4.4.4, S4.5.2 | Both tiers reported `decimal` for `1n / 2.0` and `float` for `1i64 + 0.5`. | S4.4.2 gives the first pair a decimal result, while S4.5.2 says float-involved division yields float. State the priority of the bounded-integer/float exception over general promotion. Draw the finite-int-to-i64 admission rule separately from a subtype arrow, since i64 cannot hold poison. |
| S4.1.2, SI7, S10.1.1v5 | Both tiers gave float `0.5` for `2 ** (-1)` and an ordinary error for `shl(1, -1)`. | The unconditional int-closure claim covers both. Specify exponent and shift-count domains. Name the bitwise functions explicitly, since the value expression `1 & 2` constructs the type `none`. |
| S4.5.4, S6.1.2v2, SO2 | Both tiers gave NaN for `inf % inf` and null for both `null < nan` and `nan < null`. | The formula `x % ±inf = x` needs a finite-dividend condition. Give explicit precedence where the null and NaN comparison rules overlap. |
| S5.1.1v2–S5.1.2v2, S10.2.2v2 | Both tiers gave `true` for `nan ne nan` and `[false]` for `[nan] ne [nan]`. | S10.2.2v2 explicitly makes NaN mask lanes false. State whether scalar and one-lane `ne` are intended to differ. |

S13.4.1 also needs a reproducible floating reduction algorithm. For `[1.0e16, -1.0e16, 1.0]`, adding the first pair first yields 1, while adding the last pair first yields 0. Both trees can depend solely on input length. Specify the tree, including odd tails, to make bit-identical results independently testable. For decimal division, S4.6.2 refers to a documented context. The number-model record supplies the precision formula and half-even rounding and can be referenced directly from the ruling. [Pairwise reduction decision](Lambda_Design_Concurrency.md) · [Decimal context and mixed-number decisions](Lambda_Semantics_Number_Model.md)

## Other contracts that need clarification

| Subject | Rulings | Case to settle |
|---|---|---|
| Truth classification | S2.2.1, S3.1v2, S3.2v2, SI9v2 | `""` and `"a"` have the same string type and opposite truth values. The same holds for the two booleans. The stated falsy set is precise. The claim that the language type tag alone determines truth is too strong. |
| Error operands in equality | S1.9, SI4, S5.1.1v2–S5.1.5, S7.9.2v3 | SI4 says cross-family equality never yields an error. Comparing an error with null propagates the error. Even a container compared with itself can yield a visited child error. Restrict the Boolean equality laws to comparisons that return a Boolean. |
| Order within a sort band | S5.4.1, S6.1.1, S6.2.1–S6.2.3 | Specify how to sort a time-only value against a date, two functions, or two maps with the same keys and different values. Both tiers equated a year-only date with January 1 of that year. State the equality rule for reduced precision and how descending order preserves input order among ties. |
| Sequence literal length | SI12, S2.5.5v2, S8.3.3v3 | `[for (x in []) x, 1]` has one item. Summing `count(x or 1)` over evaluated operands predicts two. A stored error occupies one slot although `count(error)` propagates. Define the formula in terms of contributions during construction. |
| Deferred admission to `none` | S11.1.7, S7.7.1v3–S7.7.3, S11.4.1v5 | S11.1.7 says a failed declaration or argument stops. The boundary-specific rules say a deferred declaration binds the validation error and continues, and a rejected argument makes the call expression yield the error. State those outcomes separately from static rejection. |
| Total reads | SI11, S7.1.1v3, S11.5.2–S11.5.4, S14.3.1v3 | A missing field yields null. A present field can fail when its backing bytes are invalid, its owner is invalidated, or deferred archive data cannot be decoded. Qualify the total-read invariant by distinguishing lookup from materialization. |
| Query existence | S3.1v2, S8.2.4v3, S8.3.3v3 | A single matched `false` or `""` takes the false branch of `if (e?T)` even though the query succeeded. Use `count(e?T) > 0` for existence. S7.10.3 likewise calls a null magnitude comparison false, while S6.1.2v2 yields null, observable with `is null`. |
| Numeric subscripts | S4.3.3, S8.2.1v5 | The sequence-key rule names `int`, `float` and `decimal`, then gives `a[5.00n]` as a positive example. The suffix denotes `integer`, and its decimal-point spelling conflicts with the numeric literal rules. Give a valid example and specify integer and sized-lane admission and the index-domain bounds. |

## Settled cases and existing open issues

- **Absence and content.** S2.5.5v2 distinguishes an empty producer from a bound null. S2.6.1v2–S2.6.5 define which items content drops, spreads and merges.
- **Error visitation.** S3.4–S3.5 settle short-circuiting. S5.1.5 makes `[1,e] == [2,e]` false and `[e,1] == [e,2]` yield `e` when `e` is an error.
- **Empty aggregates and null keys.** S7.3.2 defines empty sum as zero, empty product as one and empty average, minimum and maximum as null. S14.1.2v2–S14.1.3 explicitly group null keys together and prevent null join keys from matching.
- **Already registered.** The 45 entries in Appendix B include SO50 for competing comparison errors and collection error boundaries, SO16 for competing cleanup failures, SO24 for streamed failures and SO47 for empty search needles. LR05-17 separately records string-pattern questions, including matching across line endings. [Open issues](../doc/Lambda_Formal_Semantics.md#appendix-b--open-design-issues) · [String-pattern ledger](Lambda_Issue_Ledger.md#lr05-17)
- **Stale portions of open items.** SO39 still asks whether identity-less `===` is a compile error, although S5.1.4v3 already gives false for non-error operands without identity. Which operations preserve identity remains open. SO1 asks for a sized-division supersession note that the number-model record already contains. [Identity ruling](../doc/Lambda_Formal_Semantics.md#s51-error-preserving-deep-value-equality) · [Number-model amendments](Lambda_Semantics_Number_Model.md)

## Verification scope

The audit used the existing Lambda-opus executable in a dirty checkout at `433a458f3`, with SHA-256 `26e6c872e5cab1e50d482fb95e096c3928b21447804d5c4754a4dd399242b97d`. Focused scripts ran with `--tier=interp` and `--tier=jit`. The top-level predicate example above matters for reproduction. A separate version that passed the error as a function argument returned an error on both tiers.

These observations describe that executable. The audit did not rebuild the checkout or run a full regression baseline. The repository's executable semantics model covers a functional subset. [Runtime issue record](Lambda_Issue_Ledger.md#formal-semantics-edge-cases-20261124) · [Model verification harness](../test/lambda/sem/semantics_verify.py)
