# Lambda Formal Spec Index

> **Generated** by `utils/formal_index.py` — do not edit. After editing either
> formal spec run `make formal-index`; `make check-formal-index` (part of
> `make lint`) fails while this file is stale. Not normative: the specs win.

One row per ruling (`S#`/`D#`), invariant (`SI#`/`DI#`) and open issue
(`SO#`/`DO#`), with the spec line where it starts. `*` marks a ruling that is
not or only partly implemented (see the spec's Appendix A). To read a ruling
in full — including lines too long for a viewer — run
`python3 utils/formal_index.py D7.3.6` (an ID or a section such as `S4.5`).

## Semantics — [`Lambda_Formal_Semantics.md`](Lambda_Formal_Semantics.md) v69.0.1 (2026-10-20)

| ID | Line | Title |
|---|---|---|
| **§S1** | 52 | *Core Principles* |
| S1.1 | 57 | Emptiness is not nothingness |
| S1.2 | 59 | Values versus containers |
| S1.3 | 61 | Exactness lives in the exact tier |
| S1.4 | 65 | Mutation is visible or it does not exist |
| S1.5 | 70 | Set-oriented: absence flows, failure is deliberate |
| S1.6 | 73 | Representation is invisible |
| S1.7 | 76 | One symbol, one concept |
| S1.8 | 79 | Strings are never code |
| S1.9 | 81 | Equality is the root relation |
| S1.10 | 84 | The effect doctrine |
| S1.11 | 96 | References, not authorities |
| SI1 | 108 | Boxing invisibility |
| SI2 | 110 | COW unobservability |
| SI3v2 | 114 | Inference evaluation-invariance |
| SI4 | 133 | Equality laws |
| SI5 | 137 | Order refinement |
| SI6 | 139 | Printer injectivity |
| SI7 | 142 | Int totality |
| SI8 | 146 | Poison symmetry |
| SI9v2 | 150 | Truthiness tag-decidability |
| SI10 | 153 | Literal strictness, data totality |
| SI11 | 157 | Total reads, checked writes |
| SI12 | 160 | The length law |
| SI13 | 163 | No aliasing |
| SI14 | 166 | A binding's static type is never a lie |
| SI15 | 174 | Schedule invisibility |
| **§S2** | 181 | *The Value Domain* |
| **§S2.1** | 183 | *Types* |
| S2.1.1v4 | 185 | Scalars: `null`, `bool`, `int`, `integer`, `i64`, `u64`, sized ints `i8 i16 i32 u8 u16 u32`, `f16 f32`, … |
| S2.1.2 | 201 | `number` is a declared union only; |
| S2.1.3v2* | 203 | An object type `type T { … }` declares a structure of exactly one structural kind |
| S2.1.4* | 216 | A nominal instance is sealed to its type and open in its fields |
| S2.1.5* | 228 | Instance type alteration is reconstruction |
| **§S2.2** | 235 | *Empty and solid values* |
| S2.2.1 | 237 | `""` is a genuine `string` with `len 0` |
| S2.2.2v2 | 239 | `symbol` and `binary` are **solid types**: every value has `len ≥ 1` |
| S2.2.3 | 246 | Element construction normalizes empty text: `<e "">` ≡ `<e>` |
| S2.2.4 | 249 | An empty file is a childless `<file>` element, not an empty binary: existence lives in the container, content … |
| **§S2.3** | 252 | *Ordered storage, unordered equality* |
| S2.3.1 | 254 | Maps store keys in source/insertion order |
| **§S2.4** | 259 | *Paths* |
| S2.4.1v2* | 261 | Path values use dotted steps |
| S2.4.2v5* | 270 | Every hierarchical reference is a typed root plus ordered operations; |
| S2.4.3v3* | 291 | Paths, names, symbols, and member expressions use this one reference scheme but retain distinct evaluation … |
| S2.4.4* | 311 | Each evaluation's immutable resolver deterministically maps logical `/` prefixes, namespaces, and provider … |
| S2.4.5v2* | 318 | Paths have three root forms: rooted `/.a.b`, relative `\.a.b`, and absolute `SchemeName...` |
| S2.4.6 | 329 | A wildcard step may sit anywhere in a path, and a force reads exactly the files that a spelled step enters |
| **§S2.5** | 355 | *Lists and blocks* |
| S2.5.1v2 | 357 | A list spreads; an array does not |
| S2.5.2v2 | 367 | A for-expression produces a list, whatever its clauses |
| S2.5.3 | 370 | A block `{…}` produces a list |
| S2.5.4v2 | 374 | Declarations produce no item |
| S2.5.5v2 | 382 | A list has at least two items |
| S2.5.6 | 397 | A list is transient; where it lands decides |
| S2.5.7v4 | 408 | A list is built, never computed: functions and pipes return an array for any sequence input; operators keep … |
| S2.5.8 | 433 | Text is placed as one value and walked as a sequence |
| **§S2.6** | 444 | *Content* |
| S2.6.1v2 | 446 | Element content and script top level share one content model |
| S2.6.2 | 458 | Absent and empty items are dropped |
| S2.6.3 | 462 | Lists spread into content; arrays do not (S2.5.1v2) |
| S2.6.4 | 468 | Adjacent strings merge, adjacent binaries merge, nothing else does |
| S2.6.5 | 476 | Content stays normalized under mutation |
| S2.6.6v3* | 483 | An explicitly PDF-formatted file selects a byte-output destination |
| **§S3** | 515 | *Truthiness* |
| S3.1v2 | 517 | The falsy set is exactly **`null` · `false` · `""`** |
| S3.2v2 | 522 | Truth classification is decidable from the type tag alone |
| S3.3v3 | 527 | Consequences to teach: truthy-0 keeps `or` a safe coalescing operator (no `??` needed); |
| S3.4 | 536 | Logical evaluation preserves an encountered error |
| S3.5 | 543 | An `if` expression evaluates its condition once |
| **§S4** | 552 | *Numerics* |
| **§S4.1** | 566 | *The `int` type (v5: int53, total)* |
| S4.1.1 | 571 | The `int` domain is **{n ∈ ℤ : \|n\| ≤ 2⁵³−1} plus the three closure points `inf`/`-inf`/`nan`**, carried … |
| S4.1.2 | 577 | `int` arithmetic is **closed and total**: `+ - * div % neg abs`, bitwise `& \| ^`, shifts, and `**` never … |
| S4.1.3 | 582 | Aggregates use **mathematical-value semantics**: the result is the true sum if in band, `±inf` by the true … |
| S4.1.4 | 587 | `int ∥ i64`: poison has no `i64` home; |
| S4.1.5 | 591 | Machine ints are Go-aligned: runtime overflow wraps two's-complement; |
| **§S4.2** | 595 | *Poison* |
| S4.2.1 | 597 | `int` and `float` share **one poison identity**, spelled bare `inf` / `-inf` / `nan` |
| S4.2.2 | 601 | A value shared across domains **types as its narrowest**: `type(nan)` → `int`; |
| S4.2.3 | 604 | Same-signed infinities are **one value** across all poison-bearing domains (`inf == decimal.inf`); |
| S4.2.4 | 608 | Classification flows up: widening admits poison with no check (`nan` flows into a `float` or `integer` … |
| **§S4.3** | 613 | *Literals and ingestion* |
| S4.3.1 | 615 | Literal type is **lexical, never value-selected**: a numeric literal is `int` iff it has no decimal point and … |
| S4.3.2 | 618 | An unsuffixed int-form literal outside ±(2⁵³−1) is a **compile error** |
| S4.3.3 | 620 | The suffix names the type: `n` = `integer` always; |
| S4.3.4 | 623 | Data cannot be rejected |
| **§S4.4** | 627 | *Promotion lattice* |
| S4.4.1 | 629 | One subsumption principle: `T1 ⊑ T2` iff every T1 value embeds **exactly** into T2 |
| S4.4.2 | 634 | Meets are **type-directed, never magnitude-directed**: int×int→int; |
| S4.4.3 | 637 | Every **bounded** integer type (`i8`…`u32`, `int`, `i64`, `u64`) meets `float` in **`float`**; |
| S4.4.4 | 641 | Sized×sized selects the smallest containing machine lane and stays there for `+ - *`, bitwise, shifts (`i8 + … |
| **§S4.5** | 646 | *Division and modulo* |
| S4.5.1 | 648 | A **literal** zero divisor (`x div 0`, `x % 0`) is a compile error; |
| S4.5.2 | 651 | `/` is true division, domain-selected: `int / int → float`; |
| S4.5.3 | 654 | `div` and `%` **stay in their operand domain**: `int div int → int`; |
| S4.5.4 | 659 | `div` truncates toward zero; |
| S4.5.5 | 662 | Vectorized integer division is per-lane: the result stays an int-family array with `inf`/`nan` at offending … |
| **§S4.6** | 666 | *Decimal* |
| S4.6.1 | 668 | `decimal` is one source-level type with invisible storage tiers (decimal128 when it fits, unbounded above); |
| S4.6.2 | 671 | `+ - *` are exact and may grow storage; |
| **§S4.7** | 676 | *Float ↔ decimal* |
| S4.7.1 | 678 | A float denotes its shortest round-trip decimal |
| **§S4.8** | 685 | *Printing* |
| S4.8.1 | 687 | Floats print as their shortest round-trip decimal; |
| S4.8.2 | 691 | Finite `int` values print as integers at every magnitude |
| **§S4.9** | 696 | *Guest boundary* |
| S4.9.1 | 698 | Numeric FFI is **type-directed, never value-directed** |
| **§S5** | 707 | *Equality* |
| **§S5.1** | 709 | *Error-preserving deep value equality* |
| S5.1.1v2 | 711 | `==` is deep, structural, value-based equality |
| S5.1.2v2 | 717 | NaN is unequal; errors propagate |
| S5.1.3 | 722 | Structural recursion is depth-limited; |
| S5.1.4v3* | 725 | `==` is the only **value** equality and compares content alone |
| S5.1.5* | 737 | Container equality recursively compares content and short-circuits |
| **§S5.2** | 748 | *Numbers* |
| S5.2.1v2 | 750 | Numeric equality is exact mathematical-value equality across ranks: `1 == 1.0 == 1n == 1.0m == 1.00m`; |
| **§S5.3** | 756 | *Sequences* |
| S5.3.1 | 758 | `range`, `list`, and `array` are one sequence family: `(1 to 3) == [1,2,3]`, element-wise in order |
| **§S5.4** | 762 | *Maps, objects, elements* |
| S5.4.1 | 764 | Map equality is key-unordered (JSON/XML, the document standards); |
| S5.4.2v3* | 766 | A nominal value equals only a value of the **same nominal type**, whatever its structural kind; |
| S5.4.3 | 776 | Element equality = tag + namespace, attributes as an unordered map (XML InfoSet), children ordered (document … |
| **§S5.5** | 779 | *Functions and types* |
| S5.5.1v2* | 781 | Function equality is **intensional**: same definition site + deep-equal captures |
| S5.5.2 | 788 | Type equality is **representational** |
| **§S5.6** | 792 | *Dedup, grouping, hashing* |
| S5.6.1v2* | 794 | `unique`, set semantics, and grouping use `==`: nulls group together and each `nan` stands alone |
| S5.6.2 | 799 | `a == b ⟹ hash(a) == hash(b)` wherever values are hashed: maps hash in canonically sorted key order; |
| **§S6** | 805 | *Ordering and Sort* |
| **§S6.1** | 807 | *Two relations, by design* |
| S6.1.1 | 811 | The comparison operators `< <= > >=` answer magnitude questions only |
| S6.1.2v2 | 817 | An error operand to `< <= > >=` propagates unchanged, including against null or nan |
| **§S6.2** | 822 | *The Lambda total order* |
| S6.2.1 | 824 | `sort` and `order by` use the separate total order:* |
| S6.2.2v3 | 832 | The total order **totally refines `==`** |
| S6.2.3 | 840 | Sort is stable; |
| **§S7** | 845 | *Absence and Errors* |
| **§S7.1** | 849 | *Reads are total; writes are checked* |
| S7.1.1v3* | 851 | Every invalid member/index **read yields `null`** |
| S7.1.2 | 859 | Slices **clamp** to bounds and return an empty collection (or `""` for string results |
| S7.1.3v2* | 866 | Every invalid member/index **write raises a hard language error** through the `T^` channel of S7.4.2 |
| **§S7.2** | 873 | *Indexing and `last`* |
| S7.2.1 | 875 | Indexing is 0-based; |
| S7.2.2 | 879 | Reaching from the end is the reserved word **`last`** = `len(container) − 1` of the innermost enclosing … |
| S7.2.3 | 884 | The two-homes rule |
| S7.2.4 | 888 | Functions never take the `last` keyword and never accept signed counts |
| **§S7.3** | 894 | *Aggregation* |
| S7.3.1 | 896 | A `null` input makes the aggregate `null` |
| S7.3.2 | 900 | Monoid identities: `sum([])` = 0, `prod([])` = 1; |
| **§S7.4** | 903 | *The three failure channels* |
| S7.4.1 | 909 | Value errors `T \| error` |
| S7.4.2 | 912 | Raised errors `T^` / `T^E` |
| S7.4.3 | 919 | System faults |
| S7.4.4 | 923 | An error value is a first-class value carrying `code`, `message`, and source location; |
| S7.4.5 | 930 | System `fn` failures are values, never `T^E`: *absence in / no answer → `null`* (or `""` for string results); |
| S7.4.6 | 938 | `raise v` with a non-error `v` is shorthand for `raise error(v)`: the constructor (S7.4.4) decides the code … |
| **§S7.5** | 943 | *Acknowledgment* |
| S7.5.1v2 | 945 | A call carrying a `^` channel must engage the error at the **immediate expression**, through exactly one of: … |
| S7.5.2 | 952 | `any` never acknowledges (it admits error but engages nothing); |
| S7.5.3v2 | 955 | `a or b` defaults only `null`, `false`, and `""`; |
| **§S7.6** | 962 | *Discharge: the handler and postfix `^`* |
| S7.6.1v4* | 964 | `e ^ { … ^ … }` handles the error locally and is channel-agnostic |
| S7.6.2v3 | 995 | The handler is the left-associative postfix-primary form `primary ^ { error_body }`, optionally followed … |
| S7.6.3v2 | 1009 | Postfix `e^` propagates |
| S7.6.4 | 1020 | `?` is not propagation |
| S7.6.5v2 | 1023 | The retired forms `let a^err = e` and prefix `^err` / `if (^err)` do not exist (why: the TE-13/TE-16 record); |
| S7.6.6v3 | 1029 | Division of labor: `or` defaults ordinary falsy values and preserves errors; |
| S7.6.7v4 | 1035 | A statement-position procedure handler `pn_call() ^ { error_body }` may protect a possibly-suspending call |
| **§S7.7** | 1045 | *Containment: the declaration-boundary skip* |
| S7.7.1* | 1050 | Skip is a **declaration-boundary mechanism only** |
| S7.7.2* | 1055 | A failed deferred check at a declaration skips to the end of the block that **declares** the binding; |
| S7.7.3* | 1060 | A declared parameter guards at the **call site**: on failure the function is not entered and **the call … |
| S7.7.4* | 1064 | Reassignment to a declared `var` carries a diagnostic obligation, in three tiers: compile error where the RHS … |
| S7.7.5* | 1070 | In a `for`, the skip target is the **iteration body**, not the loop: per-item skip keeps the batch alive; |
| S7.7.6* | 1076 | Edge sites: element/field stores report via S7.7.4's tiers (containers are not scoped away |
| S7.7.7v2 | 1081 | Recovery belongs in the initializer: `let a: T = e ^ { … }` |
| **§S7.8** | 1085 | *Containers and errors* |
| S7.8.1* | 1087 | Acceptance is read from the **destination contract**, never from syntactic position |
| S7.8.2 | 1097 | Both spellings are visible in source: `[ f(x) ^ { 0 } for x in xs ]` → `int[]`, lane preserved; |
| **§S7.9** | 1104 | *What an error participates in* |
| S7.9.1 | 1109 | Type family participates |
| S7.9.2v3 | 1111 | Logical evaluation and comparisons propagate |
| S7.9.3 | 1118 | Value family propagates |
| S7.9.4 | 1124 | Containment is not participation |
| **§S7.10** | 1129 | *The sys-func return contract* |
| S7.10.1v3 | 1134 | Result shape is fixed by the contract, never by cardinality, for array, list, and text inputs: zero-to-many → … |
| S7.10.2 | 1140 | Admission is an explicit per-function contract: an **admissive** case has a meaningful no-answer reading and … |
| S7.10.3 | 1147 | No in-band sentinels, ever |
| S7.10.4 | 1153 | Error operands are rejected at the call boundary (parameters are `any \ error`), keeping "error operand" … |
| S7.10.5v3* | 1155 | Vectorized sys funcs are sequence-in, array-out (S2.5.7v4): array or list in → array out, one lane never … |
| S7.10.6 | 1161 | A mutator family picks one public convention (updated owner, or unit) and holds it; |
| **§S7.11** | 1165 | *System faults and recovery* |
| S7.11.1v2 | 1167 | Fault reasons are a closed, typed native set: stack overflow, side-stack exhaustion, out-of-memory, and … |
| S7.11.2v2 | 1173 | Faults pass transparently through `fn` frames; |
| S7.11.3 | 1180 | Transaction barriers (module init, hosted-guest entry) take priority over inner handlers: no handler may … |
| S7.11.4 | 1185 | Production containment is fail-stop: recoverable faults use recovery frames; |
| **§S8** | 1193 | *Membership and Iteration* |
| **§S8.1** | 1195 | *The `in`/`at`/`of` axis* |
| S8.1.1v2 | 1197 | Three axes, one key class each |
| S8.1.2v3 | 1212 | On elements and objects, `in` ranges over attribute values then children; |
| S8.1.3v2 | 1218 | Axis and arity are independent |
| **§S8.2** | 1237 | *The key space* |
| S8.2.1v4* | 1239 | Every container has a fixed key domain |
| S8.2.2v6* | 1262 | One name space, keyed by namespace and normalized spelling |
| S8.2.3* | 1297 | Methods are members of the type, never of the value |
| S8.2.4v3* | 1308 | Two subscript worlds: a type key answers as XPath, a positional selection as NumPy |
| S8.2.5 | 1310 | The item step `.*` and its closure |
| S8.2.6 | 1327 | A run is stepped set-at-a-time by every key kind except positional |
| **§S8.3** | 1349 | *`len`* |
| S8.3.1v3 | 1351 | The law: **`len(x)` is the number of iterations `for (i in x)` performs.** Consequences, not separate rules: … |
| S8.3.2* | 1364 | Lazy sequences: a forceable stream's `len` forces and returns the actual size; |
| S8.3.3v3 | 1369 | Two lengths: `len` measures content; |
| **§S8.4** | 1380 | *Projections* |
| S8.4.1v2 | 1382 | `keys(c)` ≡ `[for (k, v in c) k]`; |
| **§S9** | 1391 | *Mutability: Mutable Value Semantics* |
| **§S9.1** | 1393 | *The model* |
| S9.1.1 | 1398 | `let` is final: nothing reachable through a `let` binding ever changes |
| S9.1.2 | 1403 | Binding, assignment, and construction copy, observably, for every container kind |
| S9.1.3* | 1406 | `var` parameters (`pn f(var a: T)`) are the sole sharing construct |
| S9.1.4* | 1412 | Closures are immutable values: captures snapshot at creation; |
| S9.1.5v2 | 1421 | No reference cells; |
| S9.1.6* | 1427 | Member/index assignment may update or add only a member admitted by the container's existing key domain; |
| S9.1.7 | 1436 | No global mutable state |
| **§S9.2** | 1445 | *Covariance, borrows, and views* |
| S9.2.1 | 1447 | *Covariance where values copy, invariance where they're borrowed*: `int[] <: any[]` holds for `is`, reads, … |
| S9.2.2* | 1451 | Read views are first-class values with snapshot semantics (a zero-copy slice observably behaves as a copy … |
| S9.2.3* | 1456 | Iterating a `var` container walks the entry-time value: the loop share-marks at the head, and the first … |
| S9.2.4v2* | 1460 | A **view-state** `var` may not be passed as a `var` argument (no call-site check can see the callee's … |
| **§S9.3** | 1468 | *Construction captures values* |
| S9.3.1* | 1470 | Placing a value into a container captures it **by value** at every constructor and insertion point |
| **§S10** | 1485 | *Operators* |
| **§S10.1** | 1487 | *Union, pipe, and filter* |
| S10.1.1v4* | 1489 | `\|`, `&` and `!` are the type operators |
| S10.1.2v4 | 1490 | The pipe is dual-mode on a parse-time syntactic test: a body with a **free `~`** is a mapping pipe (binds `~` … |
| S10.1.3 | 1503 | `~` is lexically scoped to the RHS of its pipe; |
| S10.1.4 | 1507 | File write/append syntax is deferred; |
| S10.1.5v3 | 1509 | `that` is the single-value proviso |
| S10.1.6 | 1525 | `\|:` is the filter stage of the pipe family |
| S10.1.7v2 | 1548 | A body about one subject may leave `~` implicit; the pipe family always spells it |
| **§S10.2** | 1581 | *Vectorization* |
| S10.2.1 | 1583 | Arithmetic `+ - * /` is vectorized (element-wise with broadcasting) |
| S10.2.2v2* | 1588 | Bare comparisons `< <= > >=` are **scalar-only**, never element-wise (a mask is a container, containers are … |
| S10.2.3* | 1596 | Mask consumption is explicit and non-magical: `sum(mask)` counts true lanes; |
| **§S10.3** | 1601 | *Keyword operators* |
| S10.3.1v3 | 1603 | `and or not is in to div that at eq ne lt le gt ge` |
| **§S10.4** | 1612 | *Parent navigation* |
| S10.4.1* | 1614 | Postfix `.~~` is the parent-navigation step at the ordinary member/index precedence tier; |
| S10.4.2* | 1620 | Bare `~~` is exactly `~.~~`: it is valid exactly where `~` is bound, selects the innermost current-value … |
| S10.4.3v2* | 1626 | Contextual parent navigation is occurrence-based and carries lineage in the evaluation context as a … |
| **§S10.5** | 1633 | *Root navigation* |
| S10.5.1* | 1635 | `/` is the one root-selection operation |
| S10.5.2* | 1640 | For a path, `./` selects its logical or provider/authority anchor |
| S10.5.3v2* | 1645 | Dynamic root navigation is occurrence-based |
| **§S10.6** | 1650 | *Concatenation* |
| S10.6.1 | 1652 | `++` concatenates sequences and appends scalars |
| **§S11** | 1668 | *Types and Patterns* |
| **§S11.1** | 1670 | *Types compose like values* |
| S11.1.1v3* | 1672 | A bracket type is a structural pattern whose positions mix values, types, and occurrence runs freely: `[1, … |
| S11.1.2v4 | 1683 | String and symbol structural patterns share the delimiter `\( ... )`; quotes and named patterns determine one … |
| S11.1.3 | 1718 | A range type `X to Y` denotes inclusive membership in the consecutive values between its bounds |
| S11.1.4v2 | 1726 | The binary type relation is spelled `<:` |
| S11.1.5v2* | 1736 | Three function types: `fn`, `pn`, and their union `function` |
| S11.1.6v3 | 1754 | Two type families, split by concept: a run and an array |
| S11.1.7* | 1781 | `none` is the empty type |
| **§S11.2** | 1783 | *Match* |
| S11.2.1 | 1785 | `match` is a **type match**: arms are type expressions tried in order, first match wins, no fall-through; |
| S11.2.2 | 1792 | Poison is unequal, not untypeable: `case float:` catches nan, `case error:` catches errors; |
| S11.2.3* | 1797 | Exhaustiveness is compiler-checked: unions need every constituent, `bool` both arms, `T?` needs `T` and … |
| **§S11.3** | 1802 | *Structural `is`, nominal objects* |
| S11.3.1v2 | 1804 | `is` is structural for maps/arrays/elements (extra fields permitted; |
| **§S11.4** | 1813 | *Declared types are contracts* |
| S11.4.1v3 | 1817 | An annotation is a contract on the binding, not a hint |
| S11.4.2 | 1831 | Signatures spell both failure dimensions: plain `T` excludes null *and* error; |
| S11.4.3 | 1839 | `any` is the top type and includes error; |
| S11.4.4 | 1845 | *When the user is explicit, we check explicitly*: an explicitly declared error possibility cannot enter a … |
| S11.4.5* | 1850 | Deferred numeric admission is **value-aware**: an exactly-embedding value passes and is re-represented … |
| S11.4.6* | 1855 | User-defined types are enforced by the validator: deep, on first crossing, or a rich error with a validator … |
| S11.4.7 | 1859 | Containment and discharge follow §S7.7–S7.8: skip at declaration boundaries, destination-contract container … |
| S11.4.8v2 | 1862 | A function signature may establish a **type binder** with an explicit `T: type` parameter, a … |
| S11.4.9 | 1879 | A system-function registry row may declare a **type relation** from one call argument to its success result |
| S11.4.10* | 1892 | Verified at the crossing, valid while unchanged |
| S11.4.11 | 1904 | A `that` predicate is an `fn` body over the scope it is written in |
| **§S11.5** | 1920 | *Physical binary schemas* |
| S11.5.1v2* | 1922 | `type A : binary & {…} { field: T & {…}, … }` declares a physical binary layout |
| S11.5.2* | 1933 | `binary_decode(bytes, A)` returns a lazy, read-only element backed by its byte source and schema |
| S11.5.3v3* | 1939 | A native schema names a host binding |
| S11.5.4 | 1946 | A native pointer field `T[n] & {pointer: 'native'}` exposes a lazy read-only array of contiguous fixed-width … |
| **§S12** | 1962 | *Functions, Effects, Resources* |
| **§S12.1** | 1964 | *The one-bit effect system* |
| S12.1.1v3 | 1966 | `fn`/`pn` is a declared, compiler-checked, one-bit effect system: `fn` is pure and deterministic under any … |
| S12.1.2 | 1978 | `break`, `continue`, `return`, `while`, and `var` declarations are `pn`-only; |
| S12.1.3 | 1982 | Reactive templates are the doctrine applied: template body = pure `fn` transformation; |
| S12.1.4v3* | 1985 | Effect polymorphism: the `function` declaration |
| S12.1.5v2 | 2014 | Presentation authority by state tier |
| **§S12.2** | 2028 | *Assignment* |
| S12.2.1 | 2030 | `let` bindings and parameters are immutable; |
| S12.2.2 | 2036 | Element mutation is defined on both faces: `elem.attr = v` as map-field assignment; |
| **§S12.3** | 2040 | *The call contract* |
| S12.3.1 | 2042 | A function has at most **16 source-language argument slots** (a rest collector consumes one); |
| S12.3.2 | 2046 | Two dynamic-call restrictions are deliberate: a dynamic call with named arguments is rejected, and a dynamic … |
| S12.3.3v2* | 2049 | Member access is resolution, not membership |
| S12.3.4* | 2071 | `call(f, args)` is the dynamic-application form |
| S12.3.5v2 | 2085 | Spread splices into containers, never into an argument list; the spelling follows the container's shape |
| S12.3.6 | 2100 | No arity overloading for user definitions |
| S12.3.7* | 2109 | User definitions shadow system functions — user-first, module-lexical, warned |
| **§S12.4** | 2128 | *Resources* |
| S12.4.1v3* | 2133 | `open()` is resource acquisition and is `pn`-only |
| S12.4.2* | 2144 | A resource auto-closes at the end of its enclosing **block**; |
| S12.4.3* | 2151 | No `defer`, `with`, or `finally` keyword: auto-close is the only user-facing cleanup mechanism; |
| **§S13** | 2159 | *Concurrency* |
| **§S13.1** | 2164 | *Tasks and workers* |
| S13.1.1v2 | 2166 | `start` is a builtin `pn`, not a keyword |
| S13.1.2v2 | 2173 | Calls are **colorless**: `f(x)` synchronously yields the value and may suspend invisibly (`f(x)` ≡ … |
| S13.1.3v2* | 2178 | Two tiers, one handle vocabulary: tasks (`start(f, [x])`, shared context) and isolated workers (`start(f, … |
| S13.1.4 | 2184 | The capture rule |
| S13.1.5 | 2188 | Failures are values; faults are not |
| **§S13.2** | 2194 | *Messaging* |
| S13.2.1 | 2196 | Handle = address; |
| S13.2.2 | 2201 | `send(h, msg)` never blocks and returns `ok^E`: a full mailbox is the error value `'mailbox_full'` |
| S13.2.3 | 2205 | Ordering: per-sender FIFO; |
| **§S13.3** | 2210 | *Scope and cancellation* |
| S13.3.1 | 2212 | A started handle is a scoped resource owned by the nearest lexical block: normal exit **joins**; |
| S13.3.2 | 2216 | Cancellation is an error value at park points (`'cancelled'`), unwinding by ordinary `^` propagation with … |
| **§S13.4** | 2222 | *Determinism* |
| S13.4.1* | 2224 | Builtin numeric reductions (`sum`, `avg`, `prod`, `variance`, dot, `min`/`max` join) are … |
| S13.4.2* | 2230 | In stream pipelines, `fn` stages auto-parallelize and are **ordered by default** (a re-sequencing buffer … |
| **§S14** | 2236 | *Data Processing* |
| **§S14.1** | 2238 | *For-clause grouping and joins* |
| S14.1.1 | 2240 | `group by KEY [as ALIAS], … into g` |
| S14.1.2 | 2244 | Key equality is `==` with numeric-tower coherence (`1` and `1.0` group together); |
| S14.1.3 | 2251 | Join `on` is restricted to conjunctions of equality tests (non-equi conditions are a compile error pointing … |
| **§S14.2** | 2257 | *Verbs and windows** |
| S14.2.1* | 2259 | The verb surface is generic over row-oriented data |
| S14.2.2* | 2263 | Column references in verb arguments are `~.field` (the pipe current-item reference extended into verb scope; |
| **§S14.3** | 2268 | *Streams and laziness** |
| S14.3.1v3* | 2270 | `input()` is eager, `stream()` lazy |
| S14.3.2* | 2284 | Two stream kinds: value-backed streams are true values (re-forcible, usable in `fn`); |
| S14.3.3* | 2288 | Handles are stream sources/sinks (`stream(h)`, `send_to(h)`); |
| **§S15** | 2295 | *Metaprogramming* |
| S15.1 | 2297 | Lambda is homoiconic through elements: the canonical AST is an element tree in the ambient **`lm.` … |
| S15.2 | 2302 | Element literals are inverted quasiquotation: expression children evaluate (splice); |
| S15.3* | 2306 | `input(f, 'lambda')` parses Lambda source into the `lm.` AST; |
| S15.4 | 2312 | `name(item)` is the shadow-proof accessor for intrinsic names (element tag, function name, type name); |
| **§S16** | 2319 | *Surface Syntax* |
| **§S16.1** | 2327 | *Whitespace and separation* |
| S16.1.1* | 2329 | Line breaks carry no meaning |
| S16.1.2v2* | 2334 | `;` is a **strict separator** between statements |
| S16.1.3v2* | 2340 | Adjacent statements need **no separator** when the second begins with a token that cannot continue the first, … |
| **§S16.2** | 2351 | *Line-start classification* |
| S16.2.1* | 2353 | An **incomplete** expression continues across a line break unconditionally: a trailing operator, an unclosed … |
| S16.2.2v2* | 2356 | After a **complete** expression, a line-start token that can only continue an expression does continue it: … |
| S16.2.3v3* | 2362 | After an **open-tail** statement (S16.1.3v2), a line-start **dual-role** token |
| S16.2.4v3* | 2376 | One carve-out: `.` followed by a step other than a digit |
| S16.2.5* | 2384 | `return` followed by a line break and a start token returns that value: `return` ⏎ `42` is `return 42` |
| S16.2.6* | 2389 | A handler's brace opens **on the same line as its `^`** (`expr ^ {  |
| **§S16.3** | 2393 | *Juxtaposition* |
| S16.3.1* | 2395 | Juxtaposition **sequences, never combines.** Adjacent expressions are separate statements or content items; |
| **§S16.4** | 2403 | *Braces* |
| S16.4.1v4* | 2405 | Interior decides, wherever braces are an expression |
| S16.4.2v2 | 2420 | Empty `{}` resolves by context, and only where a tie exists |
| S16.4.3 | 2431 | Declaration braces are structural and never read as maps |
| **§S16.5** | 2458 | *Element scope* |
| S16.5.1v2* | 2460 | In an exposed element attribute value or bare content expression, `< > <= >=` **are not operators**: `>` … |
| **§S16.6** | 2474 | *Control forms* |
| S16.6.1* | 2476 | `if`, `for`, and `while` each have **one node with two spellings**: parenthesized head with any-expression … |
| S16.6.2* | 2481 | `(` immediately after `if` or `while` **commits** to the parenthesized spelling |
| S16.6.3* | 2486 | `else` is **optional** in both spellings |
| S16.6.4* | 2489 | `match` keeps its single braced form: its braces delimit an arm list, not a body, so no parenthesized … |
| S16.6.5* | 2492 | The expression/statement distinction is enforced by semantic analysis on the S12.1 effect boundary, not by … |
| S16.6.6* | 2497 | Control statements require braces |
| S16.6.7v2 | 2506 | A procedure's body is always the braced statement block, and the arrow is the one anonymous function |
| S16.6.8v2* | 2521 | A **procedural block is a statement, never an expression** |
| S16.6.9* | 2534 | Branch homogeneity |
| **§S16.7** | 2545 | *Script top level* |
| S16.7.1 | 2547 | A script's top level is element content, not a list |
| S16.7.2v2 | 2555 | Nulls and empty strings are dropped (S2.6.2) |
| S16.7.3v2 | 2560 | Lists spread and adjacent strings or binaries merge (S2.6.3, S2.6.4) |
| S16.7.4 | 2563 | A REPL entry echoes its value unless it is declarations only |
| S16.7.5 | 2570 | A session top level rebinds nothing |
| S16.7.6 | 2575 | Two session kinds |
| **§S16.8** | 2584 | *Lexical forms* |
| S16.8.1 | 2586 | `not` is the one logical negation |
| S16.8.2 | 2590 | `not` binds loose |
| S16.8.3 | 2593 | Numeric spelling |
| S16.8.4* | 2597 | No implicit adjacent-literal concatenation |
| S16.8.5 | 2602 | Unary `+` is kept |
| S16.8.6v4 | 2605 | `*` is spread; `*` and `...` are two wildcard families, not one |
| S16.8.7 | 2617 | A single-quoted literal is a symbol, not a string |
| S16.8.8* | 2621 | The backtick syntax space is reserved and must not be spent otherwise |
| S16.8.9* | 2625 | Computed keys: `[expr]: val` |
| **§S16.9** | 2637 | *Declarations, elements, paths* |
| S16.9.1 | 2639 | `pub` is a uniform prefix modifier |
| S16.9.2* | 2642 | The **`apply;` fused token is retired**: bare `apply` is the keyword statement, disambiguated from … |
| S16.9.3 | 2645 | `;` has exactly one role language-wide: statement separation |
| S16.9.4 | 2656 | The relative path is spelled `\.a.b` |
| S16.9.5 | 2662 | `a?: T` marks an optional field |
| S16.9.6 | 2667 | `.` is the only import separator |
| S16.9.7 | 2671 | A rest parameter `...` closes a parameter list |
| S16.9.8 | 2676 | An import path's first token picks one of three roots |
| **§S16.10** | 2693 | *Keywords as names* |
| S16.10.1v3* | 2695 | Keywords never name bindings — where they could capture |
| S16.10.2* | 2727 | Data names admit keywords |
| S16.10.3* | 2744 | Member steps admit keywords |
| **§S17** | 2752 | *System Library* |
| **§S17.1** | 2757 | *String splitting* |
| S17.1.1 | 2759 | `split` follows ECMAScript `String.prototype.split` |
| **§S17.2** | 2778 | *The system-function namespace* |
| S17.2.1 | 2780 | System functions live at `lambda.sys.*`, and the prelude imports them unqualified |
| S17.2.2 | 2786 | `lambda.sys.f` is the escape from a shadow |
| **§S17.3** | 2796 | *Process information* |
| S17.3.1 | 2798 | `sys.proc.self.argv` is the sole argument-vector path |
| **§S17.4** | 2804 | *String search positions* |
| S17.4.1 | 2806 | A text search result uses its source's code-point index domain |
| **§S17.5** | 2812 | *Sized integer conversions* |
| S17.5.1 | 2814 | Sized integer conversions use the type names as callable constructors |
| **§S17.6** | 2820 | *String replacement* |
| S17.6.1 | 2822 | `replace` steps through matches as ECMAScript `String.prototype.replaceAll` does, and inserts its replacement … |
| **§S17.7** | 2835 | *Letter case* |
| S17.7.1 | 2837 | Case-insensitive matching folds by Unicode simple case folding |
| S17.7.2 | 2849 | `lower` and `upper` map by Unicode full case mapping |
| **§S17.8** | 2857 | *Option maps* |
| S17.8.1* | 2859 | An option name a system function does not define is a compile-time error where the compiler can see it, and a … |
| SO1 | 2992 | Sized-lane `div`/`%`: [Number_Model §3.3.2](../vibe/Lambda_Semantics_Number_Model.md) says sized×sized `div` … |
| SO2 | 2993 | Int v5 §5 details: poison-algebra table ratification; |
| SO3 | 2994 | The `int?` fourth lane value (`INT_LANE_NULL`) is undocumented in the Int_Type sentinel table; |
| SO4 | 2995 | Bitwise semantics were ruled (S4.1.2), but the interaction with the retired sparse band in old goldens needs … |
| SO50 | 2998 | Error-preserving comparison follow-ups: canonical field traversal for key-unordered maps, selection among … |
| SO5 | 3004 | TE-17 transitivity: does discharging `(int \| error)[]` re-narrow in place, or only by copy? Copy is the safe … |
| SO6 | 3005 | Lazy/streaming `for` bodies vs typed-lane destinations (boxed-until-proven presumed, undecided); |
| SO7 | 3006 | TE-5 R5 sticky `any`; |
| SO8 | 3007 | Should `is` become value-aware? Deliberately undecided (S11.3.1v2 records the intentional asymmetry) |
| SO9 | 3008 | A surface spelling for `any \ error` (the `!` exclusion operator route is broken |
| SO10 | 3009 | A deep "does this data contain an error anywhere?" check (`valid(item)`-shaped) |
| SO13 | 3012 | COW granularity on large documents: node representation for spine-copying, refcount discipline for … |
| SO14 | 3013 | Nested-mutation ergonomics (`t.nodes[i].value`): path-shaped `var` borrows, `_modify`-style accessors, or … |
| SO15 | 3014 | Exclusivity granularity endpoint (whole-base vs blessed splitters vs dynamic bookkeeping) |
| SO16 | 3015 | Close-error routing (double fault): proposed |
| SO17 | 3016 | Resource-carrying-type containment rules (when a wrapping value is itself resource-typed) |
| SO18 | 3017 | Snapshot iteration (C4.2d) |
| SO19 | 3018 | Root and upward-parent navigation are resolved by S10.4.3v2, S10.5.3v2 / PTH10, PTH29: lineage lives in a … |
| SO39 | 3022 | Node identity (S5.1.4v2): which operations preserve an identity (a COW detach and an in-place `var` write are … |
| SO42 | 3029 | Instance type alteration (S2.1.5): the surface spelling, which declared fields must be satisfiable from the … |
| SO43 | 3033 | Type alteration |
| SO41 | 3036 | Cross-reference form for document graphs under S9.1.5v2: an identity or key stored as data and resolved … |
| SO20 | 3042 | O-D: cross-isolate lifetime for shared graph Items (promote-on-share recommended) |
| SO21 | 3043 | `select` surface syntax; |
| SO22 | 3044 | Deferred opt-ins: blocking send, true selective receive, `unordered` streams, CPU-bound cancellation … |
| SO23 | 3047 | PD4 join column-collision suffixes; |
| SO24 | 3048 | PD12 sub-items: `on error` resume semantics (abort vs skip-record), handler scoping over multiple forced … |
| SO25 | 3049 | Deferred group-by vocabulary: `having`-style filter, post-group `let`, extended aggregates, … |
| SO26 | 3052 | RF6 mutator convention: updated-owner vs unit; |
| SO27 | 3053 | Whether debug logging inside `fn` is a permitted non-observable effect |
| SO44 | 3054 | Binder depth on function values: whether a binder (S11.4.8v2) over a function value selects its full … |
| SO29 | 3059 | File write/append syntax (C6a: `into`/`onto` candidates); |
| SO31 | 3060 | The `<file>` element shape (name/size/mime, content as child) |
| SO32 | 3061 | Match extensions: pipe-context shorthand, string-pattern capture binding in arms, range patterns |
| SO33 | 3062 | A10 residue: the aspirational generics text, `as` assertion semantics, and open-vs-closed map matching in … |
| SO34 | 3063 | `emit()` vs `send()` |
| SO47 | 3064 | Whether an empty literal needle matches at every code-point boundary |
| SO35 | 3067 | A dedicated formal syntax document: S16 parks the surface-syntax rulings here because syntax and semantics … |
| SO38 | 3068 | Whether `\|:` over a **map** should keep the surviving keys (yielding a map) rather than dropping them … |
| SO36 | 3074 | Whether a `pn` call may appear nested inside an expression (`(pn_func(), 123)`, `if (exists(path)) …`), or … |
| SO45 | 3082 | Whether a function type takes a suffix directly: `fn?`, `fn (x: int)?`, `fn ()[]` |
| SO46 | 3093 | How an element pattern spells *content must be empty* |
| SO48 | 3094 | PDF file content: remaining object kinds, decimal encoding, virtual source containers, direct array/scalar … |

## Design — [`Lambda_Formal_Design.md`](Lambda_Formal_Design.md) v38.2.0 (2026-10-19)

| ID | Line | Title |
|---|---|---|
| **§D1** | 27 | *Architecture Principles* |
| D1.1 | 29 | One host, many bundles |
| D1.2v2* | 35 | Explicit guest ABI boundary |
| D1.3v3* | 42 | Shared host substrate, private guest cores |
| D1.4v4* | 52 | Language failures return through every frame |
| D1.5v2* | 67 | Precise GC everywhere, forever |
| D1.6 | 73 | The legacy paths are frozen |
| D1.7 | 78 | Source is the spec level |
| D1.8 | 82 | Decisions are carried, never recovered |
| D1.9 | 86 | Fail closed |
| D1.10 | 90 | Enforcement is part of the design |
| DI1 | 102 | The Item contract |
| DI2 | 106 | Canonical encoding |
| DI3 | 109 | The tag budget is governed |
| DI4 | 112 | Lane sentinels are private |
| DI5 | 115 | No sentinel laundering |
| DI6 | 118 | GC sees only pointers |
| DI7 | 120 | No scalar cells in the GC heap |
| DI8 | 123 | ArrayNum is null-free |
| DI9 | 126 | Container-owned scalars |
| DI10 | 130 | The safepoint contract |
| DI11 | 133 | One context, no baked addresses |
| DI12 | 136 | Hot paths stay lock-free |
| DI13 | 138 | Representation is carried |
| DI14 | 141 | Entry integrity |
| DI15v2 | 144 | No language failure skips a frame |
| DI16 | 150 | Transactional initialization |
| DI17 | 153 | One value currency |
| DI18 | 156 | The emission ratchet |
| DI19 | 158 | The COW bit |
| DI20 | 161 | Native lanes are error-free |
| **§D2** | 169 | *Data Representation* |
| **§D2.1** | 171 | *The Item model* |
| D2.1.1 | 173 | An `Item` is one 64-bit word, passed by value, in exactly three storage classes: **inline tagged immediate** … |
| D2.1.2 | 180 | `get_type_id(Item)` is the **only** sanctioned semantic-type interface (tag ≠ 0 → tag; |
| D2.1.3 | 185 | Measured ruling: String/Symbol/Binary stay **tagged leaf pointers** |
| D2.1.4 | 190 | Container extensibility is **layered, not tag-based**: storage class → `Container.type_id` family → … |
| D2.1.5 | 196 | Canonical construction: one encoding per value per build; |
| D2.1.6* | 200 | *A storage class is not designed until its lifecycle is* |
| D2.1.7 | 204 | One-way doors, deliberately accepted and to be re-acknowledged before GC/sandboxing/port work: non-moving … |
| **§D2.2** | 209 | *Number representation* |
| D2.2.1 | 211 | Common doubles are **self-tagged inline** (the double's own bit pattern is the Item; |
| D2.2.2 | 216 | The v5 `int` carrier: finite values pack into the 56-bit payload (unbox = shift-left-8, … |
| D2.2.3 | 226 | Wide scalars (`i64`, `u64`, cold doubles) have **no inline form at any magnitude** (no value-dependent … |
| D2.2.4 | 231 | `LMD_TYPE_DECIMAL` carries a `Decimal` wrapper with an explicit `DecimalKind storage_kind` (`FIXED`, … |
| D2.2.5 | 241 | MVP-Lmd admits Lambda `int` as an internal runtime subtype of JS Number |
| **§D2.3** | 253 | *Boxing and unboxing* |
| D2.3.1 | 255 | A typed function compiles to a **native entry plus a boxed entry** per the dual-func plan (§D8.3); |
| D2.3.2* | 260 | Blind `(void*)` casts of Items to container pointers are a defect |
| D2.3.3 | 265 | Boxing is representation only (semantics S1.6); |
| **§D2.4** | 269 | *Value representation discipline* |
| D2.4.1* | 273 | Four facts, four authorities: semantic contract → AST `Type*`; |
| D2.4.2* | 280 | `transpile_expr()` returns a `MirValue` carrying the full `Type*` contract; |
| D2.4.3* | 285 | Representation conversion ≠ semantic coercion |
| **§D2.5** | 291 | *The nullable native lane* |
| D2.5.1* | 293 | `N(T?) = N(T) \| NULL_LANE(T)`: optionals keep a native carrier everywhere a representation is chosen |
| D2.5.2v3 | 300 | Sentinels: `INT_LANE_NULL` (numerically the `ItemNull` word |
| D2.5.3 | 317 | `a[i]` with an unproven index infers `T?` |
| **§D2.6** | 322 | *Containers and array storage* |
| D2.6.1v3 | 324 | Three physical array forms: boxed Array (Items), native Array (uniform descriptor-selected native slot per … |
| D2.6.2 | 333 | ArrayNum is strictly non-null |
| D2.6.3 | 340 | `ELEM_INT` element storage is the i64 lane (finite values or poison sentinels), mapped to IEEE at print/box … |
| D2.6.4v3 | 343 | A container that retains a raw wide-scalar Item points **only into that container's own buffer** (tail … |
| D2.6.5v4 | 349 | The append discipline follows the destination: content, sequence, or slot |
| D2.6.6v3* | 378 | One container hierarchy — map → array → element — and nominal is a descriptor property, not a kind |
| D2.6.7* | 405 | A bound method is an ordinary D6.2.1 function value whose closure environment is the receiver, captured by … |
| D2.6.8* | 411 | Node identity is data the container carries, never its address |
| D2.6.9v3* | 418 | A JavaScript object is a nominal Lambda `Map` — and therefore a Lambda object |
| D2.6.10* | 438 | Sealing at the representation level |
| D2.6.11* | 445 | The `is_nominal` bit packs into the existing header |
| D2.6.12v3* | 457 | Virtual output lists carry their builder |
| **§D2.7** | 476 | *The scalar-GC invariant* |
| D2.7.1 | 478 | No standalone scalar cell in the GC heap |
| D2.7.2v2* | 484 | Public entry wrappers speak the **companion-lane pair** (D5.2.1v3); |
| **§D2.8** | 494 | *The error-free lane invariant* |
| D2.8.1 | 499 | No native lane slot ever holds an error |
| D2.8.2* | 514 | No unboxed operator ever receives an error operand |
| D2.8.3* | 520 | Lane entry is gated on static proof, never on runtime rescue |
| **§D2.9** | 534 | *Mark file conventions* |
| D2.9.1 | 536 | `.mark` is the sole canonical file extension for Mark Notation |
| D2.9.2 | 545 | An explicit `mark` format overrides the filename |
| D2.9.3 | 551 | A domain document written in Mark takes its own format name and extension; it is not a Mark alias |
| **§D2.10** | 560 | *XHTML input* |
| D2.10.1 | 562 | `xhtml` uses XML syntax and emits the HTML element tree |
| **§D2.11** | 581 | *SVG admission, DOM export and frame snapshots* |
| D2.11.1 | 583 | SVG embedded in `text/html` follows browser-standard HTML parsing |
| D2.11.2 | 589 | External SVG reuses that same HTML5 SVG parsing path and result model |
| D2.11.3 | 598 | SVG source export serializes the current SVG DOM, including edits |
| D2.11.4* | 603 | Animation snapshots use the frame selected by the virtual timer |
| **§D3** | 611 | *Type and Shape* |
| **§D3.1** | 617 | *First-class type values* |
| D3.1.1v5* | 619 | A type value is a `Type*` graph node under one compact TypeId (`LMD_TYPE_TYPE`), discriminated by `Type.kind` … |
| D3.1.2 | 632 | Types compose as values (`\|` union, `?`, `[]`, constraints) and compare **representationally** |
| D3.1.3 | 637 | Reflection is by operators and functions, not properties: `type(x)` yields the type value, `name(T)` its name; |
| D3.1.4v2 | 641 | A type-binder site stores its written bound, name, and stable signature-local slot; |
| **§D3.2** | 654 | *The subtype foundation* |
| D3.2.1 | 656 | One subtype model, three distinct operations |
| D3.2.2* | 662 | The **validator is the runtime enforcer** for user-defined types: deep, on first crossing, producing a rich … |
| D3.2.3 | 668 | Declared and effective types are **separate recorded facts** (declaration on the binding node, inference … |
| D3.2.4v4* | 672 | A crossing into a named map contract is a reification, not merely a check |
| D3.2.5v2 | 703 | The surface spelling for the shared subtype relation is the binary `<:` operator |
| D3.2.6* | 712 | Verified once, valid while unchanged; a required field is a required field |
| **§D3.3** | 728 | *Inference* |
| D3.3.1v2 | 730 | A type-error-free script's evaluation result is never affected by inference |
| D3.3.2v2 | 750 | Entry-shape inference and body/result inference are **separate products**: float arithmetic on a parameter … |
| D3.3.3v3 | 760 | Inferred container element-type **narrowing dies with its binding**: it is a property of one binding's scope, … |
| D3.3.4 | 774 | Representation always follows the **full inferred contract** (D2.4, D2.5): an unproven indexed read infers … |
| D3.3.5 | 777 | System-function result relations are declarative registry metadata |
| **§D3.4** | 788 | *Shapes* |
| D3.4.1 | 790 | A map/element shape is a `ShapeEntry` chain on `TypeMap`/`TypeElmt`; |
| D3.4.2v2 | 796 | Shape identity is **structural, not nominal**: the ordered (field-name, TypeId) sequence, which is the path a … |
| D3.4.3v5 | 797 | Maps and elements share types through a per-`Input` transition tree, grown one field at a time |
| D3.4.4v4* | 798 | ShapeEntry name identity follows S8.2.2v4: a name is identified by its resolved namespace and its normalized … |
| D3.4.5 | 823 | Shape transitions obey the **map-layout invariant**: the exact runtime shape always describes the stored bytes |
| D3.4.6 | 831 | Shapes carry the immutable `LaneStorageDesc` derived from the full `Type*` via the one shared descriptor … |
| D3.4.7 | 835 | A runtime JavaScript `TypeMap` may carry one immutable `const JsClassMeta*` refinement selected before … |
| D3.4.8 | 844 | A JavaScript accessor descriptor is a virtual, object-local `ShapeEntry`: it owns a `JsAccessorCell*`, has … |
| **§D3.5** | 852 | *Physical binary schemas* |
| D3.5.1v2* | 854 | Binary schemas compile to immutable, module-owned field descriptors; |
| D3.5.2v2* | 863 | Native schema views retain an invalidatable lifetime control; |
| **§D4** | 875 | *Memory Management* |
| **§D4.1** | 877 | *Allocator tiers* |
| D4.1.1v2 | 879 | Four **content tiers**: **GC heap** (runtime values), **Input arena** (parsed documents), **AST/const pool**, … |
| D4.1.2 | 887 | Static Mark data is **born shared and immortal**: `MarkBuilder` sets the shared COW state at construction; |
| D4.1.3 | 893 | Input/format allocations are pool/arena-owned and **outside GC rooting entirely**; |
| D4.1.4v5* | 896 | Heap bytes are kept by exactly **four mechanisms** |
| D4.1.5* | 927 | Two mechanism-selection corollaries of D4.1.4 |
| **§D4.2** | 940 | *Memory Context* |
| D4.2.1v3 | 942 | `MemContext` is a **factory that owns every allocator** |
| D4.2.2v2 | 953 | Per-document sub-contexts key allocators to a document URL, with the **attribution/reclamation split**: … |
| D4.2.3* | 962 | Every arena and pool is **bound at creation to a context** |
| D4.2.4* | 968 | A shared arena/pool's lifetime is governed by an **allocator-level atomic `ref_count`**: created at 1 held by … |
| D4.2.5v3* | 975 | Allocation tracking is a **diagnostic, never the mechanism**: release builds default `memtrack` OFF; |
| D4.2.6 | 992 | A pool may **own a resource that is not one of its blocks** through a **cleanup**: `pool_add_cleanup` … |
| **§D4.3** | 1006 | *Garbage collection* |
| D4.3.1 | 1008 | Non-moving mark-and-sweep, dual-zone |
| D4.3.2v2 | 1013 | Object zone = size-class segregated free lists backed by VM-owned extents; |
| D4.3.3 | 1019 | Precise closure-env tracing via `closure_field_count`; |
| D4.3.4 | 1022 | A shaped field whose declared lane is `null` is traced **conservatively**: the collector marks the eight-byte … |
| **§D4.4** | 1033 | *COW implementation* |
| D4.4.1 | 1035 | v1 ships the **1-bit shared flag** (`cow_state`, monotonic, idempotent; |
| D4.4.2 | 1041 | A COW copy is exactly **one level** |
| D4.4.3 | 1047 | All copy paths precisely root source/replacement/owner chain and reload after possible GC; |
| D4.4.4v4* | 1050 | A place handle |
| D4.4.6 | 1084 | A **place copy** (`let`/`var h = root.path`, root mutable) is share-marked at its bind iff **the place may be … |
| D4.4.5 | 1097 | A **move-out** place copy |
| **§D4.5** | 1108 | *The Radiant seam* |
| D4.5.1v4 | 1110 | One system-allocation substrate (`memtrack` + VM regions, owned through `MemContext`), **two policies**: … |
| D4.5.2* | 1121 | Radiant never retains a GC pointer |
| D4.5.3v2 | 1132 | UI/view-mode DOM construction deep-copies ordinary Elements and their content through MarkBuilder |
| **§D4.6** | 1145 | *Name identity* |
| D4.6.1v4 | 1147 | One semantic **property** identity is a `NameId`, never a `String*` address |
| D4.6.2v2 | 1164 | Evolve NamePool, don't replace it (first definer wins and parent-first lookup) |
| **§D4.7** | 1175 | *Const pool / MarkPack* |
| D4.7.1* | 1177 | One binary encoding for Lambda data, four consumers |
| D4.7.2* | 1183 | Two stream variants over one encoding (bare stream; |
| **§D5** | 1191 | *Execution State: Stacks and Rooting* |
| **§D5.1** | 1193 | *The stack model* |
| D5.1.1v3 | 1195 | Three stack-like mechanisms with distinct owners: the native C stack (invisible to GC); |
| D5.1.2 | 1211 | Static per-function frame sizing, no `MIR_ALLOCA`, two saved watermarks per frame, virtual reservation + … |
| D5.1.3 | 1216 | Downward flows never re-home; only upward flows and outliving stores do. Suspension is a re-homing barrier |
| D5.1.4 | 1220 | Cleanup order is load-bearing: all `MAY_GC` cleanup runs **before** root/number watermark restoration; |
| **§D5.2** | 1225 | *Scalar homes* |
| D5.2.1v3 | 1227 | Function wide-scalar returns use the **companion-lane convention**: a may-be-wide boxed return is the pair … |
| D5.2.2v3 | 1243 | Scalar homes are raw payload words, never GC roots, colored in a separate slot space from root slots; |
| D5.2.3* | 1260 | Watermark ownership selects the wide-return transport |
| **§D5.3** | 1283 | *GC rooting* |
| D5.3.1 | 1285 | Generated code uses **safepoint-current canonical slots**: a stable home per rootable binding plus colored … |
| D5.3.2 | 1290 | GC begins only inside a `MAY_GC` call |
| D5.3.3 | 1294 | C/C++ helpers use slot-backed `RootFrame` / `Rooted<T>` / handles / `PersistentRooted` on the same side-root … |
| D5.3.4 | 1301 | Rootability is driven by the four representation classes (`BOXED_ITEM`, `RAW_GC_POINTER`, `NON_GC_SCALAR`, … |
| D5.3.5 | 1307 | Argument passing reserves each function's **maximum lexically-overlapping argument arity as a fixed suffix of … |
| D5.3.6v2 | 1313 | Recovery frames form **one LIFO chain per activation** (the thread's base stack is one), a frame per boundary … |
| **§D5.4** | 1323 | *Runtime globals and EvalContext* |
| D5.4.1 | 1325 | One canonical long-lived `EvalContext` per isolate; exactly one sanctioned TLS root |
| D5.4.2 | 1330 | Context state lives in lazily-allocated opaque capsules; |
| D5.4.3 | 1335 | No context-dependent value at a code-baked address |
| D5.4.4 | 1340 | Frozen registries stay global (immutable after publication); |
| **§D6** | 1347 | *Function* |
| **§D6.1** | 1349 | *The effect coloring* |
| D6.1.1v2 | 1351 | `fn`/`pn` is a one-bit, declared, compiler-checked effect system (semantics §S12.1), realized as … |
| D6.1.2 | 1362 | The bit is load-bearing across the runtime: it is the const-folder's soundness gate, the stream-fusion and … |
| D6.1.3v2* | 1369 | Effect analyses beyond the bit are separate compiler facts: `may_await` (the call may park, so it may collect … |
| **§D6.2** | 1377 | *Function values and closures* |
| D6.2.1 | 1379 | A function value is a GC object carrying its entry pointers, its attached type/signature, and its closure … |
| D6.2.2v2 | 1385 | Dynamic calls dispatch through per-callee executable entries |
| D6.2.3v2 | 1399 | Lambda-language closures are **immutable values**: captures snapshot at creation and are stored as `Item`s in … |
| D6.2.4 | 1407 | The closure env is precisely traced (`closure_field_count`); |
| **§D6.3** | 1412 | *Concurrency runtime* |
| D6.3.1 | 1414 | Tasks share the context heap under **one scheduler per context**; |
| D6.3.2* | 1420 | Workers are **share-nothing isolates** (thread default, process optional); |
| D6.3.3v2 | 1426 | Faults are scoped to one thread and one stack |
| **§D6.4** | 1433 | *System built-ins* |
| D6.4.1 | 1435 | Public sys funcs return **boxed `Item`** at the runtime/JIT boundary; |
| D6.4.2 | 1441 | Raw `-1` helpers are **adapter-only**; |
| **§D7** | 1448 | *Modules and Packaging* |
| **§D7.1** | 1454 | *Build packaging and layering* |
| D7.1.1 | 1456 | Build packaging is **static libraries** (dynamic is reserved for Jube modules): four archives + exe |
| D7.1.2v2* | 1463 | All resource IO lives in `lambda-io` |
| D7.1.3 | 1472 | Provider ownership beats include ownership |
| D7.1.4v2 | 1478 | Headless is **three profiles**: A (`lambda-cli`, D7.1.6) is a link-time omission of `radiant.a`; |
| D7.1.5 | 1488 | Mark API ownership: `MarkReader` is core; |
| D7.1.6 | 1492 | `lambda-cli` is the runtime-only host |
| D7.1.7v6 | 1500 | `lambda-wasm` is the browser evaluation profile |
| **§D7.2** | 1534 | *Script packages* |
| D7.2.1 | 1536 | A Lambda script module (package) is the compilation unit: its language profile is a unit property (D8.2.1), … |
| D7.2.2 | 1542 | Imports resolve to the **boxed `_b` symbol** of exported functions (export ⇒ boxed entry mandatory, D8.3.4v4); |
| D7.2.3 | 1547 | Imported packages are cached in-process (L1, D8.5.1) and distribute as **source** (D1.7); |
| D7.2.4v3 | 1550 | `lambda.*` is the one root for everything Lambda ships |
| D7.2.5* | 1565 | Full user-agent editing policy belongs to the shipped Lambda DOM behavior package, never to Radiant's native … |
| D7.2.6 | 1580 | Import resolution follows the S16.9.8 roots, one resolver for every caller |
| D7.2.7v2* | 1592 | LaTeX is read by TeX expansion, never by TeX typesetting |
| D7.2.8* | 1594 | Document math preserves its source notation's layout policy |
| **§D7.3** | 1606 | *Jube modules: the module system* |
| D7.3.1 | 1608 | Core = ECMA-262 + engine substrate + `./lib` + a **closed globals allowlist** (console, timers, … |
| D7.3.2 | 1613 | Modules are DSOs + `module.json`: hash-verified, ABI and hosted-API negotiated before any module code runs, … |
| D7.3.3 | 1619 | One strict host-API tier for everyone |
| D7.3.4 | 1625 | Core never links modules; |
| D7.3.5 | 1629 | Every module kind names its conformance gate, wired into CI |
| D7.3.6* | 1633 | The host exports an allowlist, nothing more |
| **§D7.4** | 1645 | *Jube modules: data and async contracts* |
| D7.4.1v2 | 1647 | `Item` is the only value currency; |
| D7.4.2v2 | 1654 | Modules are **fully shielded from the async substrate** |
| D7.4.3 | 1664 | Guest hosting crosses **no core types**: no `Runtime*`, `EvalContext*`, `Input*`, `MIR_context_t`, or … |
| D7.4.4 | 1674 | Declared interfaces + record-owned hooks are the ONLY host-object protocol — one way to be a host object, no … |
| D7.4.5v2* | 1686 | Three virtual carriers mirror the container taxonomy and occupy their structural TypeId bands: `vmap`, … |
| D7.4.6 | 1708 | A host module states each function's colour in its signature |
| **§D7.5** | 1717 | *Jube modules: trust and IO* |
| D7.5.1 | 1719 | Three trust tiers: **T1 system** (in-process DSO |
| D7.5.2* | 1728 | One central IO API is the sole IO door |
| D7.5.3v2 | 1734 | Lambda↔Radiant is two explicit contracts: Radiant embeds Lambda through a versioned, Jube-aligned embed … |
| D7.5.4* | 1742 | Audible output is a capability gated by document origin |
| **§D8** | 1761 | *Compilation Pipeline* |
| **§D8.1** | 1763 | *Structure* |
| D8.1.1v18* | 1765 | First-party Lambda C lexer + hybrid recursive-descent/Pratt parser → the shared typed AST → **tiered … |
| D8.1.2v3 | 1920 | The shipped Lambda executable uses the first-party C parser as the final production implementation for normal … |
| D8.1.3v26* | 1931 | LambdaJS uses its first-party C lexer and hybrid recursive-descent/Pratt parser for normal JavaScript and … |
| **§D8.2** | 2107 | *Unified AST* |
| D8.2.1* | 2109 | One leveled core-node catalog serves host and guests; |
| D8.2.2* | 2114 | Variance tiers in strict preference order: fields/flags → clause chains → language-range node kinds; |
| D8.2.3v2* | 2121 | Migration is extract-after-convergence: the common core is extracted only after two working clients exist; |
| D8.2.4v2* | 2126 | The unified compiler has one **indexed compilation unit**: dense stable IDs name … |
| D8.2.5v3* | 2139 | One typed pass manager owns build → bind → validate → index → collect → capture/effect → environment layout → … |
| D8.2.6v2* | 2155 | Core expression lowering is demand-driven and returns the full `MirValue` of D2.4.2 |
| **§D8.3** | 2167 | *Dual-function compiling* |
| D8.3.1v2* | 2169 | Plan-dependent entries comprise the boxed `<name>_b` boundary and a **bounded set of immutable raw variants** |
| D8.3.2 | 2182 | The check lives in the callee |
| D8.3.3 | 2188 | Source-relative correctness is the invariant |
| D8.3.4v4 | 2193 | Visibility decides which versions exist |
| D8.3.5 | 2213 | Numeric admission at implicit boundaries is one rule (static whole-domain embedding admits + normalizes; |
| **§D8.4** | 2218 | *Dispatch policy* |
| D8.4.1v2 | 2220 | No inline caches anywhere in the Lambda lane or LambdaJS |
| D8.4.2v2* | 2235 | Core direct calls pass individual ABI operands (`Context*`, args); |
| D8.4.3v2* | 2239 | Every hosted-language helper uses an explicit completion ABI |
| **§D8.5** | 2258 | *MIR module cache* |
| D8.5.1v7* | 2260 | L1 |
| D8.5.2* | 2297 | The disk-cache direction is **code image + relocation journal** (Route B); |
| D8.5.3* | 2304 | The **differential write verifier is mandatory and fail-closed**: compile twice at different addresses, diff; |
| **§D8.6** | 2314 | *Emission testing* |
| D8.6.1 | 2316 | The MT7 ratchet: 0% slack |
| D8.6.2 | 2322 | One artifact contract: the MIR dump path honored in release; |
| D8.6.3 | 2326 | Liveness is verified by **dynamic oracles** |
| D8.6.4v2* | 2331 | Unified-AST consolidation has three **hard, fail-closed exit ratchets**: at least 2,000 net physical C/C++ … |
| DO1 | 2708 | Nullable-lane residue: boxed fallback for `any`/`number`/unions (confirm); |
| DO25 | 2712 | Node-identity carrier and operations (S5.1.4v2, D2.6.8): where the identity lives (a header word costs every … |
| DO2 | 2719 | Scalar-home audits: result-bearing dispatch-helper enumeration; |
| DO3 | 2722 | Box/unbox: union params stay boxed (revisit?); |
| DO4 | 2724 | ValueRep: `MirValue` size (measure first); |
| DO5 | 2729 | Numeric GC-fallback end state: per-boundary inventory to zero, then delete `scalar_heap`; |
| DO6 | 2733 | Side-root slot metadata (boxed-Item vs raw-pointer tag) |
| DO7 | 2736 | Stack maps (KIV) |
| DO8 | 2739 | CW4 shared-flag → saturating count/GC-refresh: blocked on CW5 counters from a live corpus; |
| DO9 | 2742 | Memory Context Stage 2 opens, incl |
| DO10 | 2745 | SF verification items: concat memcpy without tail-payload rebase (latent dangling wide scalars); |
| DO11 | 2750 | Dual-func: O1 promotion-aware numeric inference and lowering (Stage-1 boundary only today); |
| DO12 | 2754 | MIR cache: GC-root registry reset under L1 (verify before relying); |
| DO13 | 2760 | Emission testing: guest-transpiler scope (no guest is hosted since `lang-python` was removed, 2026-10-07); |
| DO14 | 2765 | Unified AST: the "views" fourth variance tier (adopt only when a real shared pass asks); |
| DO15 | 2767 | *(resolved 2026-08-07)* Online exception-poll doc status conflict: verified landed in `52c0f3c02` (2026-07-24) |
| DO25 | 2773 | Interpreter tier (D8.1.1v14) remains open for breadth/performance: satellite-module treatment under the MT7 … |
| DO16 | 2785 | Name identity: temporal canonical accepted; |
| DO17 | 2791 | Const pool: decimal sharing scope; |
| DO18 | 2795 | Sys-func registry: the public mutation convention (owner- returning vs unit) and `splice`'s public result; |
| DO19 | 2798 | Jube: `js_globals.cpp` allowlist audit; |
| DO20 | 2808 | Cross-isolate lifetime for shared graph Items (promote-on-share recommended) |
| DO21 | 2811 | Static modules: Class F scheduling (the browser-style engine/bindings split is the eventual shape); |
| DO22 | 2817 | First-class `Type*` representation needs its own design record: today the kind-discriminated layout is … |
| DO23 | 2822 | Shape system opens: cross-`Input` type sharing |
| DO24 | 2823 | Unnamed wide temporaries crossing a loop back edge, under D5.2.3's reclaim |
| DO26 | 2835 | The D2.6.6v2 migration, **updated after phase 1 landed and phase 2 began, 2026-09-03** |
| DO27 | 2857 | Representation of instance type alteration (S2.1.5): whether reconstruction reuses the old buffer when the … |
| DO28 | 2861 | Tier carrier divergence for annotated module bindings: the eager module init keeps a trusted `let T: string[] … |
| DO29 | 2871 | Typed `var` parameter rebind on the eager tier |
| DO30 | 2900 | *(closed 2026-09-07 |
| DO31 | 2912 | Whether an object declaration may name an element tag |
| DO32 | 2914 | Virtual PDF output follow-up: extend pure-producer demand coverage, retained-child representation (SO48), … |
