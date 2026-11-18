# Lambda Formal Spec Index

> **Generated** by `utils/formal_index.py` — do not edit. After editing either
> formal spec run `make formal-index`; `make check-formal-index` (part of
> `make lint`) fails while this file is stale. Not normative: the specs win.

One row per ruling (`S#`/`D#`), invariant (`SI#`/`DI#`) and open issue
(`SO#`/`DO#`), with the spec line where it starts. `*` marks a ruling that is
not or only partly implemented (see the spec's Appendix A). To read a ruling
in full — including lines too long for a viewer — run
`python3 utils/formal_index.py D9.4.6` (an ID or a section such as `S4.5`).

## Semantics — [`Lambda_Formal_Semantics.md`](Lambda_Formal_Semantics.md) v100.0.0 (2026-11-18)

| ID | Line | Title |
|---|---|---|
| **§S1** | 53 | *Core Principles* |
| S1.1 | 58 | Emptiness is not nothingness |
| S1.2 | 60 | Values versus containers |
| S1.3 | 62 | Exactness lives in the exact tier |
| S1.4v2* | 66 | Mutation is visible or it does not exist |
| S1.5 | 72 | Set-oriented: absence flows, failure is deliberate |
| S1.6 | 75 | Representation is invisible |
| S1.7 | 78 | One symbol, one concept |
| S1.8 | 81 | Strings are never code |
| S1.9 | 83 | Equality is the root relation |
| S1.10v2* | 86 | The effect doctrine |
| S1.11 | 99 | References, not authorities |
| SI1 | 111 | Boxing invisibility |
| SI2 | 113 | COW unobservability |
| SI3v2 | 117 | Inference evaluation-invariance |
| SI4 | 136 | Equality laws |
| SI5 | 140 | Order refinement |
| SI6 | 142 | Printer injectivity |
| SI7 | 145 | Int totality |
| SI8 | 149 | Poison symmetry |
| SI9v2 | 153 | Truthiness tag-decidability |
| SI10 | 156 | Literal strictness, data totality |
| SI11 | 160 | Total reads, checked writes |
| SI12 | 163 | The length law |
| SI13 | 166 | No aliasing |
| SI14v3 | 169 | A binding's static type is never a lie |
| SI15 | 181 | Schedule invisibility |
| **§S2** | 188 | *The Value Domain* |
| **§S2.1** | 190 | *Types* |
| S2.1.1v4 | 192 | Scalars: `null`, `bool`, `int`, `integer`, `i64`, `u64`, sized ints `i8 i16 i32 u8 u16 u32`, `f16 f32`, … |
| S2.1.2 | 208 | `number` is a declared union only; |
| S2.1.3v2* | 210 | An object type `type T { … }` declares a structure of exactly one structural kind |
| S2.1.4* | 223 | A nominal instance is sealed to its type and open in its fields |
| S2.1.5* | 235 | Instance type alteration is reconstruction |
| **§S2.2** | 242 | *Empty and solid values* |
| S2.2.1 | 244 | `""` is a genuine `string` with `len 0` |
| S2.2.2v2 | 246 | `symbol` and `binary` are **solid types**: every value has `len ≥ 1` |
| S2.2.3 | 253 | Element construction normalizes empty text: `<e "">` ≡ `<e>` |
| S2.2.4 | 256 | An empty file is a childless `<file>` element, not an empty binary: existence lives in the container, content … |
| **§S2.3** | 259 | *Ordered storage, unordered equality* |
| S2.3.1v2 | 261 | Maps store keys in source/insertion order while map **equality compares keys unordered** (§S5.4) |
| **§S2.4** | 273 | *Paths* |
| S2.4.1v2* | 275 | Path values use dotted steps |
| S2.4.2v5* | 284 | Every hierarchical reference is a typed root plus ordered operations; |
| S2.4.3v3* | 305 | Paths, names, symbols, and member expressions use this one reference scheme but retain distinct evaluation … |
| S2.4.4* | 325 | Each evaluation's immutable resolver deterministically maps logical `/` prefixes, namespaces, and provider … |
| S2.4.5v2* | 332 | Paths have three root forms: rooted `/.a.b`, relative `\.a.b`, and absolute `SchemeName...` |
| S2.4.6 | 343 | A wildcard step may sit anywhere in a path, and a force reads exactly the files that a spelled step enters |
| S2.4.7 | 368 | A query step on a path follows the wildcard rule: it never crosses the file boundary by itself |
| **§S2.5** | 391 | *Lists and blocks* |
| S2.5.1v2 | 393 | A list spreads; an array does not |
| S2.5.2v2 | 403 | A for-expression produces a list, whatever its clauses |
| S2.5.3v3* | 406 | A functional block `{…}` produces a list |
| S2.5.4v3 | 416 | Declarations produce no result, not a `null` result |
| S2.5.5v2 | 427 | A list has at least two items |
| S2.5.6 | 442 | A list is transient; where it lands decides |
| S2.5.7v4 | 453 | A list is built, never computed: functions and pipes return an array for any sequence input; operators keep … |
| S2.5.8 | 478 | Text is placed as one value and walked as a sequence |
| **§S2.6** | 489 | *Content* |
| S2.6.1v2 | 491 | Element content and script top level share one content model |
| S2.6.2 | 503 | Absent and empty items are dropped |
| S2.6.3 | 507 | Lists spread into content; arrays do not (S2.5.1v2) |
| S2.6.4 | 513 | Adjacent strings merge, adjacent binaries merge, nothing else does |
| S2.6.5 | 521 | Content stays normalized under mutation |
| S2.6.6v3* | 528 | An explicitly PDF-formatted file selects a byte-output destination |
| **§S3** | 560 | *Truthiness* |
| S3.1v2 | 562 | The falsy set is exactly **`null` · `false` · `""`** |
| S3.2v2 | 567 | Truth classification is decidable from the type tag alone |
| S3.3v3 | 572 | Consequences to teach: truthy-0 keeps `or` a safe coalescing operator (no `??` needed); |
| S3.4 | 581 | Logical evaluation preserves an encountered error |
| S3.5 | 588 | An `if` expression evaluates its condition once |
| **§S4** | 597 | *Numerics* |
| **§S4.1** | 612 | *The `int` type (v5: int53, total)* |
| S4.1.1 | 617 | The `int` domain is **{n ∈ ℤ : \|n\| ≤ 2⁵³−1} plus the three closure points `inf`/`-inf`/`nan`**, carried … |
| S4.1.2 | 623 | `int` arithmetic is **closed and total**: `+ - * div % neg abs`, bitwise `& \| ^`, shifts, and `**` never … |
| S4.1.3 | 628 | Aggregates use **mathematical-value semantics**: the result is the true sum if in band, `±inf` by the true … |
| S4.1.4 | 633 | `int ∥ i64`: poison has no `i64` home; |
| S4.1.5 | 637 | Machine ints are Go-aligned: runtime overflow wraps two's-complement; |
| S4.1.6 | 640 | A successful `int(x)` result is always an `int` |
| **§S4.2** | 651 | *Poison* |
| S4.2.1 | 653 | `int` and `float` share **one poison identity**, spelled bare `inf` / `-inf` / `nan` |
| S4.2.2 | 657 | A value shared across domains **types as its narrowest**: `type(nan)` → `int`; |
| S4.2.3 | 660 | Same-signed infinities are **one value** across all poison-bearing domains (`inf == decimal.inf`); |
| S4.2.4 | 664 | Classification flows up: widening admits poison with no check (`nan` flows into a `float` or `integer` … |
| S4.2.5* | 668 | Numerical domain outcomes stay numerical |
| **§S4.3** | 682 | *Literals and ingestion* |
| S4.3.1 | 684 | Literal type is **lexical, never value-selected**: a numeric literal is `int` iff it has no decimal point and … |
| S4.3.2 | 687 | An unsuffixed int-form literal outside ±(2⁵³−1) is a **compile error** |
| S4.3.3 | 689 | The suffix names the type: `n` = `integer` always; |
| S4.3.4 | 692 | Data cannot be rejected |
| **§S4.4** | 696 | *Promotion lattice* |
| S4.4.1 | 698 | One subsumption principle: `T1 ⊑ T2` iff every T1 value embeds **exactly** into T2 |
| S4.4.2 | 703 | Meets are **type-directed, never magnitude-directed**: int×int→int; |
| S4.4.3 | 706 | Every **bounded** integer type (`i8`…`u32`, `int`, `i64`, `u64`) meets `float` in **`float`**; |
| S4.4.4 | 710 | Sized×sized selects the smallest containing machine lane and stays there for `+ - *`, bitwise, shifts (`i8 + … |
| **§S4.5** | 715 | *Division and modulo* |
| S4.5.1 | 717 | A **literal** zero divisor (`x div 0`, `x % 0`) is a compile error; |
| S4.5.2 | 720 | `/` is true division, domain-selected: `int / int → float`; |
| S4.5.3 | 723 | `div` and `%` **stay in their operand domain**: `int div int → int`; |
| S4.5.4 | 728 | `div` truncates toward zero; |
| S4.5.5 | 731 | Vectorized integer division is per-lane: the result stays an int-family array with `inf`/`nan` at offending … |
| **§S4.6** | 735 | *Decimal* |
| S4.6.1 | 737 | `decimal` is one source-level type with invisible storage tiers (decimal128 when it fits, unbounded above); |
| S4.6.2 | 740 | `+ - *` are exact and may grow storage; |
| **§S4.7** | 745 | *Float ↔ decimal* |
| S4.7.1 | 747 | A float denotes its shortest round-trip decimal |
| **§S4.8** | 754 | *Printing* |
| S4.8.1 | 756 | Floats print as their shortest round-trip decimal; |
| S4.8.2 | 760 | Finite `int` values print as integers at every magnitude |
| **§S4.9** | 765 | *Guest boundary* |
| S4.9.1 | 767 | Numeric FFI is **type-directed, never value-directed** |
| **§S5** | 776 | *Equality* |
| **§S5.1** | 778 | *Error-preserving deep value equality* |
| S5.1.1v2 | 780 | `==` is deep, structural, value-based equality |
| S5.1.2v2 | 786 | NaN is unequal; errors propagate |
| S5.1.3 | 791 | Structural recursion is depth-limited; |
| S5.1.4v3* | 794 | `==` is the only **value** equality and compares content alone |
| S5.1.5* | 806 | Container equality recursively compares content and short-circuits |
| **§S5.2** | 817 | *Numbers* |
| S5.2.1v2 | 819 | Numeric equality is exact mathematical-value equality across ranks: `1 == 1.0 == 1n == 1.0m == 1.00m`; |
| **§S5.3** | 825 | *Sequences* |
| S5.3.1 | 827 | `range`, `list`, and `array` are one sequence family: `(1 to 3) == [1,2,3]`, element-wise in order |
| **§S5.4** | 831 | *Maps, objects, elements* |
| S5.4.1 | 833 | Map equality is key-unordered (JSON/XML, the document standards); |
| S5.4.2v3* | 835 | A nominal value equals only a value of the **same nominal type**, whatever its structural kind; |
| S5.4.3 | 845 | Element equality = tag + namespace, attributes as an unordered map (XML InfoSet), children ordered (document … |
| **§S5.5** | 848 | *Functions and types* |
| S5.5.1v2* | 850 | Function equality is **intensional**: same definition site + deep-equal captures |
| S5.5.2 | 857 | Type equality is **representational** |
| **§S5.6** | 861 | *Dedup, grouping, hashing* |
| S5.6.1v2* | 863 | `unique`, set semantics, and grouping use `==`: nulls group together and each `nan` stands alone |
| S5.6.2 | 868 | `a == b ⟹ hash(a) == hash(b)` wherever values are hashed: maps hash in canonically sorted key order; |
| **§S6** | 874 | *Ordering and Sort* |
| **§S6.1** | 876 | *Two relations, by design* |
| S6.1.1 | 880 | The comparison operators `< <= > >=` answer magnitude questions only |
| S6.1.2v2 | 886 | An error operand to `< <= > >=` propagates unchanged, including against null or nan |
| **§S6.2** | 891 | *The Lambda total order* |
| S6.2.1 | 893 | `sort` and `order by` use the separate total order:* > **null < false < true < number (by value) < datetime < … |
| S6.2.2v3 | 901 | The total order **totally refines `==`** |
| S6.2.3 | 909 | Sort is stable; |
| **§S7** | 914 | *Absence and Errors* |
| **§S7.1** | 918 | *Reads are total; writes are checked* |
| S7.1.1v3* | 920 | Every invalid member/index **read yields `null`** |
| S7.1.2v2 | 928 | Slices **clamp** to bounds and return an empty collection (or `""` for string results |
| S7.1.3v3* | 936 | Every invalid member/index **write fails with a language error**, subject to S7.4.1v2/S7.5 |
| **§S7.2** | 945 | *Indexing and `last`* |
| S7.2.1 | 947 | Indexing is 0-based; |
| S7.2.2 | 951 | Reaching from the end is the reserved word **`last`** = `len(container) − 1` of the innermost enclosing … |
| S7.2.3 | 956 | The two-homes rule |
| S7.2.4 | 960 | Functions never take the `last` keyword and never accept signed counts |
| **§S7.3** | 966 | *Aggregation* |
| S7.3.1 | 968 | A `null` input makes the aggregate `null` |
| S7.3.2 | 972 | Monoid identities: `sum([])` = 0, `prod([])` = 1; |
| **§S7.4** | 976 | *Ordinary errors and system faults* |
| S7.4.1v2 | 981 | Ordinary errors are values with uniform acknowledgement |
| S7.4.2v4 | 987 | `T^` is shorthand for `T \| error` in every type position, including function returns |
| S7.4.3 | 1000 | System faults |
| S7.4.4 | 1004 | An error value is a first-class value carrying `code`, `message`, and source location; |
| S7.4.5v4* | 1011 | System `fn` failures and effectful-reader failures obey the same ordinary-error discipline |
| S7.4.6v3* | 1023 | `raise v` completes the enclosing callable early with an error |
| **§S7.5** | 1031 | *Acknowledgment* |
| S7.5.1v6 | 1033 | Acknowledgement follows possible ordinary-error outcomes |
| S7.5.2v4 | 1045 | Binding acknowledgement follows type admission |
| S7.5.3v3 | 1065 | `a or b` defaults only `null`, `false`, and `""`; |
| S7.5.4 | 1072 | Acknowledgement composes through error-preserving operations |
| S7.5.5 | 1080 | An unacknowledged fallible result may not be silently overwritten in a procedural sequence |
| S7.5.6 | 1089 | Explicit error classification acknowledges the tested outcome |
| S7.5.7 | 1097 | No `empty` keyword or predefined error-or-null shorthand |
| **§S7.6** | 1105 | *Discharge: handlers and postfix `^`* |
| S7.6.1v9* | 1107 | `e ^ { … ^ … }` is the unfiltered postfix error handler and is origin-independent |
| S7.6.2v5 | 1137 | The unfiltered and filtered handlers are left-associative postfix-primary forms `primary ^ { body }` and … |
| S7.6.3v3* | 1150 | Postfix `e^` propagates |
| S7.6.4 | 1161 | `?` is not propagation |
| S7.6.5v4* | 1164 | Retired split bindings `let a^err = e` and prefix error tests `^err` / `if (^err)` remain unavailable; |
| S7.6.6v6* | 1172 | Division of labor: `or` defaults ordinary falsy values and preserves errors; |
| S7.6.7v5* | 1180 | Procedure handlers produce values under the same rules as function handlers |
| S7.6.8 | 1189 | `e or^ { h }` combines ordinary-error recovery and falsy defaulting |
| S7.6.9 | 1199 | `e ^ErrorType { h }` handles matching ordinary errors |
| **§S7.7** | 1214 | *Validation outcomes and boundary behavior* |
| S7.7.1v3* | 1219 | Binding validation is an expression-level operation |
| S7.7.2v2 | 1227 | A deferred initializer binds its validation outcome |
| S7.7.3* | 1237 | A declared parameter guards at the **call site**: on failure the function is not entered and **the call … |
| S7.7.4v2 | 1241 | Whole-variable reassignment uses the initializer admission rules unchanged |
| S7.7.5v3 | 1252 | Initializer or whole-variable reassignment failure inside a loop does not skip its body or abandon the loop |
| S7.7.6v2* | 1259 | Element/field stores keep their separate failure contract |
| S7.7.7v4 | 1268 | Validation errors can be handled as ordinary values |
| **§S7.8** | 1276 | *Containers and errors* |
| S7.8.1* | 1278 | Acceptance is read from the **destination contract**, never from syntactic position |
| S7.8.2 | 1288 | Both spellings are visible in source: `[ f(x) ^ { 0 } for x in xs ]` → `int[]`, lane preserved; |
| **§S7.9** | 1295 | *What an error participates in* |
| S7.9.1 | 1300 | Type family participates |
| S7.9.2v3 | 1302 | Logical evaluation and comparisons propagate |
| S7.9.3 | 1309 | Value family propagates |
| S7.9.4 | 1315 | Containment is not participation |
| **§S7.10** | 1320 | *The sys-func return contract* |
| S7.10.1v3 | 1325 | Result shape is fixed by the contract, never by cardinality, for array, list, and text inputs: zero-to-many → … |
| S7.10.2v2 | 1331 | Admission is an explicit per-function contract: an **admissive** case has a meaningful no-answer reading and … |
| S7.10.3 | 1340 | No in-band sentinels, ever |
| S7.10.4 | 1346 | Error operands are rejected at the call boundary (parameters are `any \ error`), keeping "error operand" … |
| S7.10.5v3* | 1348 | Vectorized sys funcs are sequence-in, array-out (S2.5.7v4): array or list in → array out, one lane never … |
| S7.10.6 | 1354 | A mutator family picks one public convention (updated owner, or unit) and holds it; |
| S7.10.7 | 1357 | Query system functions are total on non-error inputs |
| **§S7.11** | 1366 | *System faults and recovery* |
| S7.11.1v2 | 1368 | Fault reasons are a closed, typed native set: stack overflow, side-stack exhaustion, out-of-memory, and … |
| S7.11.2v2 | 1374 | Faults pass transparently through `fn` frames; |
| S7.11.3 | 1381 | Transaction barriers (module init, hosted-guest entry) take priority over inner handlers: no handler may … |
| S7.11.4 | 1386 | Production containment is fail-stop: recoverable faults use recovery frames; |
| **§S8** | 1394 | *Membership and Iteration* |
| **§S8.1** | 1396 | *The `in`/`at`/`of` axis* |
| S8.1.1v2 | 1398 | Three axes, one key class each |
| S8.1.2v3 | 1413 | On elements and objects, `in` ranges over attribute values then children; |
| S8.1.3v2 | 1419 | Axis and arity are independent |
| **§S8.2** | 1438 | *The key space* |
| S8.2.1v4* | 1440 | Every container has a fixed key domain |
| S8.2.2v6* | 1463 | One name space, keyed by namespace and normalized spelling |
| S8.2.3* | 1498 | Methods are members of the type, never of the value |
| S8.2.4v3* | 1509 | Two subscript worlds: a type key answers as XPath, a positional selection as NumPy |
| S8.2.5 | 1511 | The item step `.*` and its closure |
| S8.2.6 | 1528 | A run is stepped set-at-a-time by every key kind except positional |
| **§S8.3** | 1550 | *`len`* |
| S8.3.1v3 | 1552 | The law: **`len(x)` is the number of iterations `for (i in x)` performs.** Consequences, not separate rules: … |
| S8.3.2* | 1565 | Lazy sequences: a forceable stream's `len` forces and returns the actual size; |
| S8.3.3v3 | 1570 | Two lengths: `len` measures content; |
| **§S8.4** | 1581 | *Projections* |
| S8.4.1v2 | 1583 | `keys(c)` ≡ `[for (k, v in c) k]`; |
| **§S9** | 1592 | *Mutability: Mutable Value Semantics* |
| **§S9.1** | 1594 | *The model* |
| S9.1.1 | 1601 | `let` is final: nothing reachable through a `let` binding ever changes |
| S9.1.2 | 1606 | Binding, assignment, and construction copy, observably, for every container kind |
| S9.1.3v4* | 1609 | `var` parameters (`pn f(var a: T)`) are explicit inout borrows; |
| S9.1.4v2* | 1624 | Capture permissions follow the callable's colour |
| S9.1.5v2 | 1638 | No reference cells; |
| S9.1.6* | 1644 | Member/index assignment may update or add only a member admitted by the container's existing key domain; |
| S9.1.7v2* | 1653 | No module-level mutable binding |
| **§S9.2** | 1665 | *Covariance, borrows, and views* |
| S9.2.1v2 | 1667 | *Covariance where values copy, invariance where they're borrowed*: `int[] <: any[]` holds for `is`, reads, … |
| S9.2.2v2* | 1673 | Read views are first-class values with snapshot semantics (a zero-copy slice observably behaves as a copy … |
| S9.2.3* | 1681 | Iterating a `var` container walks the entry-time value: the loop share-marks at the head, and the first … |
| S9.2.4v2* | 1685 | A **view-state** `var` may not be passed as a `var` argument (no call-site check can see the callee's … |
| **§S9.3** | 1693 | *Construction captures values* |
| S9.3.1* | 1695 | Placing a value into a container captures it **by value** at every constructor and insertion point |
| S9.3.2 | 1701 | Construction preserves each element's type and precision |
| **§S10** | 1719 | *Operators* |
| **§S10.1** | 1721 | *Union, pipe, and filter* |
| S10.1.1v5* | 1723 | `\|`, `&` and `!` are the type operators |
| S10.1.2v4 | 1724 | The pipe is dual-mode on a parse-time syntactic test: a body with a **free `~`** is a mapping pipe (binds `~` … |
| S10.1.3 | 1737 | `~` is lexically scoped to the RHS of its pipe; |
| S10.1.4 | 1741 | File write/append syntax is deferred; |
| S10.1.5v3 | 1743 | `that` is the single-value proviso |
| S10.1.6 | 1759 | `\|:` is the filter stage of the pipe family |
| S10.1.7v4* | 1782 | A body about one subject may leave `~` implicit; the pipe family always spells it |
| **§S10.2** | 1818 | *Vectorization* |
| S10.2.1 | 1820 | Arithmetic `+ - * /` is vectorized (element-wise with broadcasting) |
| S10.2.2v2* | 1825 | Bare comparisons `< <= > >=` are **scalar-only**, never element-wise (a mask is a container, containers are … |
| S10.2.3 | 1833 | Mask consumption is explicit and non-magical: `sum(mask)` counts true lanes; |
| S10.2.4 | 1837 | Element-wise arithmetic promotes exactly as scalar arithmetic does: each result element has the type the … |
| **§S10.3** | 1852 | *Keyword operators* |
| S10.3.1v3 | 1854 | `and or not is in to div that at eq ne lt le gt ge` |
| **§S10.4** | 1863 | *Parent navigation* |
| S10.4.1* | 1865 | Postfix `.~~` is the parent-navigation step at the ordinary member/index precedence tier; |
| S10.4.2* | 1871 | Bare `~~` is exactly `~.~~`: it is valid exactly where `~` is bound, selects the innermost current-value … |
| S10.4.3v2* | 1877 | Contextual parent navigation is occurrence-based and carries lineage in the evaluation context as a … |
| **§S10.5** | 1884 | *Root navigation* |
| S10.5.1* | 1886 | `/` is the one root-selection operation |
| S10.5.2* | 1891 | For a path, `./` selects its logical or provider/authority anchor |
| S10.5.3v2* | 1896 | Dynamic root navigation is occurrence-based |
| **§S10.6** | 1901 | *Concatenation* |
| S10.6.1v2 | 1903 | `++` concatenates sequences, appends scalars, and merges maps |
| **§S11** | 1922 | *Types and Patterns* |
| **§S11.1** | 1924 | *Types compose like values* |
| S11.1.1v3* | 1926 | A bracket type is a structural pattern whose positions mix values, types, and occurrence runs freely: `[1, … |
| S11.1.2v4 | 1937 | String and symbol structural patterns share the delimiter `\( ... )`; quotes and named patterns determine one … |
| S11.1.3 | 1972 | A range type `X to Y` denotes inclusive membership in the consecutive values between its bounds |
| S11.1.4v2 | 1980 | The binary type relation is spelled `<:` |
| S11.1.5v4* | 1990 | Three function types: `fn`, `pn`, and their union `function` |
| S11.1.6v3 | 2008 | Two type families, split by concept: a run and an array |
| S11.1.7* | 2035 | `none` is the empty type |
| **§S11.2** | 2037 | *Match* |
| S11.2.1 | 2039 | `match` is a **type match**: arms are type expressions tried in order, first match wins, no fall-through; |
| S11.2.2 | 2046 | Poison is unequal, not untypeable: `case float:` catches nan, `case error:` catches errors; |
| S11.2.3* | 2051 | Exhaustiveness is compiler-checked: unions need every constituent, `bool` both arms, `T?` needs `T` and … |
| **§S11.3** | 2056 | *Structural `is`, nominal objects* |
| S11.3.1v2 | 2058 | `is` is structural for maps/arrays/elements (extra fields permitted; |
| **§S11.4** | 2067 | *Declared types are contracts* |
| S11.4.1v5* | 2071 | An annotation is a strict **admission contract**, not a hint |
| S11.4.2v3* | 2089 | Signatures spell both failure dimensions: plain successful `T` excludes null and error; |
| S11.4.3 | 2100 | `any` is the top type and includes error; |
| S11.4.4 | 2106 | *When the user is explicit, we check explicitly*: an explicitly declared error possibility cannot enter a … |
| S11.4.5* | 2111 | Deferred numeric admission is **value-aware**: an exactly-embedding value passes and is re-represented … |
| S11.4.6v4* | 2116 | User-defined types are enforced by the validator: deep, on first crossing, yielding an admitted value or a … |
| S11.4.7v3* | 2128 | Validation and discharge follow §S7.7–S7.8: deferred initializer and whole-variable reassignment checks … |
| S11.4.8v2 | 2132 | A function signature may establish a **type binder** with an explicit `T: type` parameter, a … |
| S11.4.9 | 2149 | A system-function registry row may declare a **type relation** from one call argument to its success result |
| S11.4.10* | 2162 | Verified at the crossing, valid while unchanged |
| S11.4.11 | 2174 | A `that` predicate is an `fn` body over the scope it is written in |
| S11.4.12v2* | 2187 | A type name binds for its whole scope and never reads as `any` |
| **§S11.5** | 2191 | *Physical binary schemas* |
| S11.5.1v2* | 2193 | `type A : binary & {…} { field: T & {…}, … }` declares a physical binary layout |
| S11.5.2* | 2204 | `binary_decode(bytes, A)` returns a lazy, read-only element backed by its byte source and schema |
| S11.5.3v3* | 2210 | A native schema names a host binding |
| S11.5.4 | 2217 | A native pointer field `T[n] & {pointer: 'native'}` exposes a lazy read-only array of contiguous fixed-width … |
| **§S12** | 2233 | *Functions, Effects, Resources* |
| **§S12.1** | 2235 | *Purity and effect bounds* |
| S12.1.1v5* | 2237 | `fn` is pure and deterministic under any schedule |
| S12.1.2v3* | 2251 | Local imperative control is admitted in `fn` and `pn` |
| S12.1.3v6* | 2270 | A `view` or `edit` template may have a `function` body |
| S12.1.4v7* | 2286 | A `function` combines its own generative effects with the effects of its polymorphic arguments |
| S12.1.5v2 | 2312 | Presentation authority by state tier |
| S12.1.6v2* | 2326 | A procedure can declare an upper bound on its effects |
| **§S12.2** | 2340 | *Assignment* |
| S12.2.1v3* | 2342 | `let` bindings and parameters are immutable; |
| S12.2.2 | 2351 | Element mutation is defined on both faces: `elem.attr = v` as map-field assignment; |
| **§S12.3** | 2355 | *The call contract* |
| S12.3.1 | 2357 | A function has at most **16 source-language argument slots** (a rest collector consumes one); |
| S12.3.2 | 2361 | Two dynamic-call restrictions are deliberate: a dynamic call with named arguments is rejected, and a dynamic … |
| S12.3.3v2* | 2364 | Member access is resolution, not membership |
| S12.3.4v2* | 2386 | `call(f, args)` is the dynamic-application form |
| S12.3.5v2 | 2399 | Spread splices into containers, never into an argument list; the spelling follows the container's shape |
| S12.3.6 | 2414 | No arity overloading for user definitions |
| S12.3.7* | 2423 | User definitions shadow system functions — user-first, module-lexical, warned |
| **§S12.4** | 2442 | *Resources* |
| S12.4.1v3* | 2447 | `open()` is resource acquisition and is `pn`-only |
| S12.4.2* | 2458 | A resource auto-closes at the end of its enclosing **block**; |
| S12.4.3* | 2465 | No `defer`, `with`, or `finally` keyword: auto-close is the only user-facing cleanup mechanism; |
| S12.4.4v4* | 2470 | An `open` block that scopes a transaction commits it on a clean, successful exit, including when a `return`, … |
| **§S13** | 2488 | *Concurrency* |
| **§S13.1** | 2493 | *Tasks and workers* |
| S13.1.1v3 | 2495 | `start` is a builtin `pn`, not a keyword |
| S13.1.2v2 | 2502 | Calls are **colorless**: `f(x)` synchronously yields the value and may suspend invisibly (`f(x)` ≡ … |
| S13.1.3v2* | 2507 | Two tiers, one handle vocabulary: tasks (`start(f, [x])`, shared context) and isolated workers (`start(f, … |
| S13.1.4 | 2513 | The capture rule |
| S13.1.5 | 2517 | Failures are values; faults are not |
| **§S13.2** | 2523 | *Messaging* |
| S13.2.1 | 2525 | Handle = address; |
| S13.2.2 | 2530 | `send(h, msg)` never blocks and returns `ok^`: a full mailbox is the error value `'mailbox_full'` |
| S13.2.3 | 2534 | Ordering: per-sender FIFO; |
| **§S13.3** | 2539 | *Scope and cancellation* |
| S13.3.1v2 | 2541 | A started handle is a scoped resource owned by the nearest lexical block: normal exit **joins**; |
| S13.3.2 | 2549 | Cancellation is an error value at park points (`'cancelled'`), unwinding by ordinary `^` propagation with … |
| **§S13.4** | 2555 | *Determinism* |
| S13.4.1* | 2557 | Builtin numeric reductions (`sum`, `avg`, `prod`, `variance`, dot, `min`/`max` join) are … |
| S13.4.2* | 2563 | In stream pipelines, `fn` stages auto-parallelize and are **ordered by default** (a re-sequencing buffer … |
| **§S14** | 2569 | *Data Processing* |
| **§S14.1** | 2571 | *For-clause grouping and joins* |
| S14.1.1v2 | 2573 | `group by KEY [as NAME], … into g` binds, for each group, **every key under its name** and **`g` to the … |
| S14.1.2v2 | 2580 | Key equality is `==` with numeric-tower coherence (`1` and `1.0` group together); |
| S14.1.3 | 2593 | Join `on` is restricted to conjunctions of equality tests (non-equi conditions are a compile error pointing … |
| **§S14.2** | 2599 | *Verbs and windows** |
| S14.2.1* | 2601 | The verb surface is generic over row-oriented data |
| S14.2.2* | 2605 | Column references in verb arguments are `~.field` (the pipe current-item reference extended into verb scope; |
| **§S14.3** | 2610 | *Streams and laziness** |
| S14.3.1v3* | 2612 | `input()` is eager, `stream()` lazy |
| S14.3.2* | 2626 | Two stream kinds: value-backed streams are true values (re-forcible, usable in `fn`); |
| S14.3.3* | 2630 | Handles are stream sources/sinks (`stream(h)`, `send_to(h)`); |
| **§S15** | 2637 | *Metaprogramming* |
| S15.1 | 2639 | Lambda is homoiconic through elements: the canonical AST is an element tree in the ambient **`lm.` … |
| S15.2 | 2644 | Element literals are inverted quasiquotation: expression children evaluate (splice); |
| S15.3* | 2648 | `input(f, 'lambda')` parses Lambda source into the `lm.` AST; |
| S15.4 | 2654 | `name(item)` is the shadow-proof accessor for intrinsic names (element tag, function name, type name); |
| **§S16** | 2661 | *Surface Syntax* |
| **§S16.1** | 2669 | *Whitespace and separation* |
| S16.1.1* | 2671 | Line breaks carry no meaning |
| S16.1.2v2* | 2676 | `;` is a **strict separator** between statements |
| S16.1.3v2* | 2682 | Adjacent statements need **no separator** when the second begins with a token that cannot continue the first, … |
| **§S16.2** | 2693 | *Line-start classification* |
| S16.2.1* | 2695 | An **incomplete** expression continues across a line break unconditionally: a trailing operator, an unclosed … |
| S16.2.2v3* | 2698 | After a **complete** expression, a line-start token that can only continue an expression does continue it: … |
| S16.2.3v3* | 2704 | After an **open-tail** statement (S16.1.3v2), a line-start **dual-role** token |
| S16.2.4v3* | 2718 | One carve-out: `.` followed by a step other than a digit |
| S16.2.5* | 2726 | `return` followed by a line break and a start token returns that value: `return` ⏎ `42` is `return 42` |
| S16.2.6v2* | 2731 | Handler bodies open on the same line as their introducer: `e ^ { h }`, `e ^ErrorType { h }`, `e or^ { h }` |
| **§S16.3** | 2738 | *Juxtaposition* |
| S16.3.1* | 2740 | Juxtaposition **sequences, never combines.** Adjacent expressions are separate statements or content items; |
| **§S16.4** | 2748 | *Braces* |
| S16.4.1v4* | 2750 | Interior decides, wherever braces are an expression |
| S16.4.2v2 | 2765 | Empty `{}` resolves by context, and only where a tie exists |
| S16.4.3v4* | 2776 | Declaration braces are structural and never read as maps |
| **§S16.5** | 2804 | *Element scope* |
| S16.5.1v2* | 2806 | In an exposed element attribute value or bare content expression, `< > <= >=` **are not operators**: `>` … |
| **§S16.6** | 2820 | *Control forms* |
| S16.6.1* | 2822 | `if`, `for`, and `while` each have **one node with two spellings**: parenthesized head with any-expression … |
| S16.6.2* | 2827 | `(` immediately after `if` or `while` **commits** to the parenthesized spelling |
| S16.6.3* | 2832 | `else` is **optional** in both spellings |
| S16.6.4* | 2835 | `match` keeps its single braced form: its braces delimit an arm list, not a body, so no parenthesized … |
| S16.6.5v2* | 2838 | Control capability and call effects are separate |
| S16.6.6* | 2846 | Control statements require braces |
| S16.6.7v3* | 2855 | A procedure's body is always the braced statement block, and the arrow is the one anonymous function |
| S16.6.8v3* | 2872 | An imperative control block is a statement, not a general expression |
| S16.6.9v2* | 2881 | Branch homogeneity |
| **§S16.7** | 2892 | *Script top level* |
| S16.7.1 | 2894 | A script's top level is element content, not a list |
| S16.7.2v2 | 2902 | Nulls and empty strings are dropped (S2.6.2) |
| S16.7.3v2 | 2907 | Lists spread and adjacent strings or binaries merge (S2.6.3, S2.6.4) |
| S16.7.4 | 2910 | A REPL entry echoes its value unless it is declarations only |
| S16.7.5 | 2917 | A session top level rebinds nothing |
| S16.7.6 | 2922 | Two session kinds |
| S16.7.7v2* | 2931 | A script can enter a generative context explicitly |
| **§S16.8** | 2942 | *Lexical forms* |
| S16.8.1 | 2944 | `not` is the one logical negation |
| S16.8.2 | 2948 | `not` binds loose |
| S16.8.3 | 2951 | Numeric spelling |
| S16.8.4* | 2955 | No implicit adjacent-literal concatenation |
| S16.8.5 | 2960 | Unary `+` is kept |
| S16.8.6v4 | 2963 | `*` is spread; `*` and `...` are two wildcard families, not one |
| S16.8.7 | 2975 | A single-quoted literal is a symbol, not a string |
| S16.8.8* | 2979 | The backtick syntax space is reserved and must not be spent otherwise |
| S16.8.9v2 | 2983 | Computed keys: `[expr]: val` |
| S16.8.10 | 2996 | Computed element tags: `<[expr] …>` |
| **§S16.9** | 3006 | *Declarations, elements, paths* |
| S16.9.1 | 3008 | `pub` is a uniform prefix modifier |
| S16.9.2* | 3011 | The **`apply;` fused token is retired**: bare `apply` is the keyword statement, disambiguated from … |
| S16.9.3 | 3014 | `;` has exactly one role language-wide: statement separation |
| S16.9.4 | 3025 | The relative path is spelled `\.a.b` |
| S16.9.5 | 3031 | `a?: T` marks an optional field |
| S16.9.6 | 3036 | `.` is the only import separator |
| S16.9.7 | 3040 | A rest parameter `...` closes a parameter list |
| S16.9.8 | 3045 | An import path's first token picks one of three roots |
| **§S16.10** | 3062 | *Keywords as names* |
| S16.10.1v3* | 3064 | Keywords never name bindings — where they could capture |
| S16.10.2* | 3096 | Data names admit keywords |
| S16.10.3* | 3113 | Member steps admit keywords |
| S16.10.4 | 3118 | A value name must resolve at compile time |
| **§S17** | 3130 | *System Library* |
| **§S17.1** | 3135 | *String splitting* |
| S17.1.1 | 3137 | `split` follows ECMAScript `String.prototype.split` |
| **§S17.2** | 3156 | *The system-function namespace* |
| S17.2.1 | 3158 | System functions live at `lambda.sys.*`, and the prelude imports them unqualified |
| S17.2.2 | 3164 | `lambda.sys.f` is the escape from a shadow |
| **§S17.3** | 3174 | *Process information* |
| S17.3.1 | 3176 | `sys.proc.self.argv` is the sole argument-vector path |
| **§S17.4** | 3182 | *String search positions* |
| S17.4.1 | 3184 | A text search result uses its source's code-point index domain |
| **§S17.5** | 3190 | *Sized integer conversions* |
| S17.5.1 | 3192 | Sized integer conversions use the type names as callable constructors |
| **§S17.6** | 3198 | *String replacement* |
| S17.6.1 | 3200 | `replace` steps through matches as ECMAScript `String.prototype.replaceAll` does, and inserts its replacement … |
| **§S17.7** | 3213 | *Letter case* |
| S17.7.1 | 3215 | Case-insensitive matching folds by Unicode simple case folding |
| S17.7.2 | 3227 | `lower` and `upper` map by Unicode full case mapping |
| **§S17.8** | 3235 | *Option maps* |
| S17.8.1* | 3237 | An option name a system function does not define is a compile-time error where the compiler can see it, and a … |
| SO1 | 3389 | Sized-lane `div`/`%`: [Number_Model §3.3.2](../vibe/Lambda_Semantics_Number_Model.md) says sized×sized `div` … |
| SO2 | 3390 | Int v5 §5 details: poison-algebra table ratification; |
| SO3 | 3391 | The `int?` fourth lane value (`INT_LANE_NULL`) is undocumented in the Int_Type sentinel table; |
| SO4 | 3392 | Bitwise semantics were ruled (S4.1.2), but the interaction with the retired sparse band in old goldens needs … |
| SO50 | 3395 | Error-preserving comparison follow-ups: canonical field traversal for key-unordered maps, selection among … |
| SO5 | 3401 | TE-17 transitivity: does discharging `(int \| error)[]` re-narrow in place, or only by copy? Copy is the safe … |
| SO6 | 3402 | Lazy/streaming `for` bodies vs typed-lane destinations (boxed-until-proven presumed, undecided); |
| SO7 | 3403 | TE-5 R5 sticky `any`; |
| SO8 | 3404 | Should `is` become value-aware? Deliberately undecided (S11.3.1v2 records the intentional asymmetry) |
| SO9 | 3405 | A surface spelling for `any \ error` (the `!` exclusion operator route is broken |
| SO10 | 3406 | A deep "does this data contain an error anywhere?" check (`valid(item)`-shaped) |
| SO13 | 3409 | COW granularity on large documents: node representation for spine-copying, refcount discipline for … |
| SO14 | 3410 | Nested-mutation ergonomics (`t.nodes[i].value`): path-shaped `var` borrows, `_modify`-style accessors, or … |
| SO15 | 3411 | Exclusivity granularity endpoint (whole-base vs blessed splitters vs dynamic bookkeeping) |
| SO16 | 3412 | Close-error routing (double fault): proposed |
| SO17 | 3413 | Resource-carrying-type containment rules (when a wrapping value is itself resource-typed) |
| SO18 | 3414 | Snapshot iteration (C4.2d) |
| SO19 | 3415 | Root and upward-parent navigation are resolved by S10.4.3v2, S10.5.3v2 / PTH10, PTH29: lineage lives in a … |
| SO39 | 3419 | Node identity (S5.1.4v2): which operations preserve an identity (a COW detach and an in-place `var` write are … |
| SO42 | 3426 | Instance type alteration (S2.1.5): the surface spelling, which declared fields must be satisfiable from the … |
| SO43 | 3430 | Type alteration |
| SO41 | 3433 | Cross-reference form for document graphs under S9.1.5v2: an identity or key stored as data and resolved … |
| SO20 | 3439 | O-D: cross-isolate lifetime for shared graph Items (promote-on-share recommended) |
| SO21 | 3440 | `select` surface syntax; |
| SO22 | 3441 | Deferred opt-ins: blocking send, true selective receive, `unordered` streams, CPU-bound cancellation … |
| SO23 | 3444 | PD4 join column-collision suffixes; |
| SO24 | 3445 | PD12 sub-items: `on error` resume semantics (abort vs skip-record), handler scoping over multiple forced … |
| SO25 | 3446 | Deferred group-by vocabulary: `having`-style filter, post-group `let`, extended aggregates, … |
| SO26 | 3449 | RF6 mutator convention: updated-owner vs unit; |
| SO52 | 3450 | Which additional operations map to each effect, and what cache scope and invalidation govern resource input? … |
| SO27 | 3451 | Whether debug logging inside `fn` is a permitted non-observable effect |
| SO44 | 3452 | Binder depth on function values: whether a binder (S11.4.8v2) over a function value selects its full … |
| SO29 | 3457 | File write/append syntax (C6a: `into`/`onto` candidates); |
| SO31 | 3458 | The `<file>` element shape (name/size/mime, content as child) |
| SO32 | 3459 | Match extensions: pipe-context shorthand, string-pattern capture binding in arms, range patterns |
| SO33 | 3460 | A10 residue: the aspirational generics text, `as` assertion semantics, and open-vs-closed map matching in … |
| SO34 | 3461 | `emit()` vs `send()` |
| SO47 | 3462 | Whether an empty literal needle matches at every code-point boundary |
| SO51 | 3464 | Local imperative control in comprehensions: mutation of a containing `fn` local across iterations, … |
| SO35 | 3471 | A dedicated formal syntax document: S16 parks the surface-syntax rulings here because syntax and semantics … |
| SO38 | 3472 | Whether `\|:` over a **map** should keep the surviving keys (yielding a map) rather than dropping them … |
| SO36 | 3478 | Whether a `pn` call may appear nested inside an expression (`(pn_func(), 123)`, `if (exists(path)) …`), or … |
| SO45 | 3487 | Whether a function type takes a suffix directly: `fn?`, `fn (x: int)?`, `fn ()[]` |
| SO46 | 3497 | How an element pattern spells *content must be empty* |
| SO48 | 3498 | PDF file content: remaining object kinds, decimal encoding, virtual source containers, direct array/scalar … |

## Design — [`Lambda_Formal_Design.md`](Lambda_Formal_Design.md) v69.0.0 (2026-11-18)

| ID | Line | Title |
|---|---|---|
| **§D1** | 57 | *Architecture Principles* |
| D1.1 | 62 | One host, many bundles |
| D1.2* | 68 | `Item` is the only value currency at a host boundary |
| D1.3* | 75 | Guests share the host substrate |
| D1.4* | 84 | A language failure returns through every frame |
| D1.5* | 97 | Precise GC everywhere, permanently |
| D1.6 | 104 | MIR Direct is the only compiler back end |
| D1.7 | 107 | Source is the distribution format |
| D1.8 | 111 | Carry decisions forward; never recover them |
| D1.9 | 115 | Fail closed |
| D1.10 | 120 | Enforcement is part of the design |
| DI1 | 132 | The Item contract |
| DI2 | 136 | Canonical encoding |
| DI3 | 140 | The tag budget has an owner |
| DI4 | 143 | Lane sentinels are private |
| DI5 | 146 | No sentinel laundering |
| DI6 | 150 | The GC sees only pointers |
| DI7 | 153 | No scalar cells in the GC heap |
| DI8 | 156 | ArrayNum is null-free |
| DI9 | 159 | Containers own their scalars |
| DI10 | 163 | The safepoint contract |
| DI11 | 167 | One context, no fixed addresses |
| DI12 | 170 | Hot paths have no locks |
| DI13 | 173 | Representation is carried |
| DI14 | 176 | Entry integrity |
| DI15 | 179 | No language failure skips a frame |
| DI16 | 184 | Initialization is a transaction |
| DI17 | 187 | One value currency |
| DI18 | 190 | The emission ratchet |
| DI19 | 192 | The COW bit |
| DI20 | 195 | Native lanes are error-free |
| **§D2** | 201 | *Value Representation* |
| **§D2.1** | 206 | *The Item model* |
| D2.1.1 | 208 | An Item is one word with three storage classes |
| D2.1.2 | 220 | One interface gives the semantic type |
| D2.1.3 | 225 | Extend containers by layers, not by tags |
| D2.1.4 | 230 | Construction is canonical |
| D2.1.5* | 234 | Design the lifecycle with the storage class |
| D2.1.6 | 238 | Some decisions are one-way doors |
| **§D2.2** | 244 | *Numbers* |
| D2.2.1 | 246 | Common doubles are inline |
| D2.2.2 | 250 | The int carrier has a private native lane |
| D2.2.3 | 256 | Wide scalars have no inline form |
| D2.2.4 | 261 | Decimal tiers are invisible |
| D2.2.5 | 267 | In MVP-Lmd, Lambda `int` is a private subtype of JS Number |
| **§D2.3** | 276 | *Representation discipline* |
| D2.3.1 | 280 | Boxing is representation only |
| D2.3.2 | 283 | A typed function has a native entry and a boxed entry |
| D2.3.3* | 289 | Never cast an Item to a container pointer blindly |
| D2.3.4* | 294 | Four facts have four owners |
| D2.3.5* | 303 | A carrier conversion is not a semantic coercion |
| **§D2.4** | 309 | *The nullable native lane* |
| D2.4.1* | 311 | An optional keeps a native carrier |
| D2.4.2 | 320 | Each lane has one null code |
| D2.4.3 | 328 | Representation follows the full inferred contract |
| **§D2.5** | 334 | *The error-free lane* |
| D2.5.1 | 339 | No native lane holds an error |
| D2.5.2* | 345 | The emitter keeps errors out of unboxed operators |
| D2.5.3* | 350 | Lane entry needs a static proof |
| **§D2.6** | 362 | *Scalar ownership* |
| D2.6.1 | 364 | No standalone scalar cell in the GC heap |
| D2.6.2 | 370 | A container owns its wide scalars |
| **§D3** | 376 | *Containers and Shapes* |
| **§D3.1** | 378 | *The container model* |
| D3.1.1* | 380 | One container hierarchy: map, array, element |
| D3.1.2* | 395 | Sealed types permit cached layout |
| D3.1.3* | 401 | Node identity is data, never an address |
| D3.1.4* | 405 | A bound method is a closure over its receiver |
| D3.1.5* | 411 | A JavaScript object is a nominal Lambda map |
| **§D3.2** | 419 | *Arrays* |
| D3.2.1 | 421 | There are three physical array forms |
| D3.2.2* | 428 | ArrayNum is non-null and invisible |
| **§D3.3** | 436 | *The append discipline* |
| D3.3.1 | 438 | The destination selects the append |
| **§D3.4** | 455 | *Shapes* |
| D3.4.1v2 | 457 | Shape layout is ABI |
| D3.4.2 | 464 | Shape identity is structural |
| D3.4.3 | 469 | Shapes share through a transition tree |
| D3.4.4* | 480 | Shape names follow the name identity rule |
| D3.4.5 | 489 | The exact shape always describes the stored bytes |
| D3.4.6 | 495 | Shapes carry the lane descriptor |
| D3.4.7 | 499 | JavaScript class metadata refines runtime shapes |
| **§D3.5** | 507 | *Virtual output lists* |
| D3.5.1* | 509 | A virtual output list carries its builder |
| **§D4** | 520 | *Types* |
| **§D4.1** | 525 | *Type values* |
| D4.1.1* | 527 | A type value is a `Type*` graph |
| D4.1.2 | 535 | Types compose as values and compare by representation |
| D4.1.3 | 540 | Reflection uses operators and functions |
| D4.1.4 | 544 | A type binder has a stable slot |
| **§D4.2** | 554 | *Subtyping and validation* |
| D4.2.1 | 556 | One subtype model, three operations |
| D4.2.2 | 563 | `<:` spells the subtype relation |
| D4.2.3* | 570 | The validator enforces user-defined types at run time |
| D4.2.4* | 578 | Declared and effective types are separate facts |
| D4.2.5* | 586 | A crossing into a named map contract reifies its layout |
| D4.2.6* | 602 | Verified once, valid while unchanged |
| **§D4.3** | 612 | *Inference* |
| D4.3.1 | 614 | Inference never changes the result of a correct program |
| D4.3.2 | 623 | Inference selects an implementation, never a contract |
| D4.3.3 | 629 | Inferred narrowing dies with its binding |
| D4.3.4 | 638 | System-function result relations are registry metadata |
| **§D4.4** | 645 | *Binary schemas* |
| D4.4.1* | 647 | Binary schemas compile to immutable descriptors |
| D4.4.2* | 655 | A native schema view can be made invalid |
| **§D5** | 664 | *Memory* |
| **§D5.1** | 666 | *Tiers and mechanisms* |
| D5.1.1 | 668 | There are four content tiers |
| D5.1.2 | 674 | Static Mark data is born shared and immortal |
| D5.1.3 | 679 | Input data is outside GC rooting |
| D5.1.4* | 682 | There are four allocation mechanisms |
| D5.1.5* | 695 | Select the mechanism by lifetime |
| **§D5.2** | 703 | *Memory contexts* |
| D5.2.1 | 705 | `MemContext` owns every allocator |
| D5.2.2 | 713 | Documents get attribution; some also get reclamation |
| D5.2.3* | 719 | A shared allocator has a reference count |
| D5.2.4* | 725 | Allocation tracking is a diagnostic |
| D5.2.5 | 732 | A pool can own an external resource through a cleanup |
| **§D5.3** | 739 | *Garbage collection* |
| D5.3.1 | 741 | The collector is a non-moving, two-zone mark-and-sweep |
| D5.3.2 | 748 | Closures and virtual maps are traced precisely |
| D5.3.3 | 752 | A null-typed shape lane is traced as a possible pointer |
| **§D5.4** | 759 | *Copy-on-write* |
| D5.4.1 | 761 | COW uses one shared bit |
| D5.4.2 | 767 | A COW copy is one level |
| D5.4.3* | 772 | Copy paths stay rooted |
| D5.4.4* | 775 | A place handle can borrow its place |
| D5.4.5 | 790 | A place copy is share-marked if its place can change |
| D5.4.6 | 802 | A `var` argument uses a caller-owned home |
| **§D5.5** | 810 | *The Radiant seam* |
| D5.5.1 | 812 | One substrate, two policies |
| D5.5.2* | 819 | Radiant never keeps a GC pointer |
| D5.5.3 | 825 | DOM construction copies; presentation retains |
| **§D5.6** | 834 | *Name identity* |
| D5.6.1 | 836 | A `NameId` is property identity |
| D5.6.2 | 845 | Evolve the NamePool; do not replace it |
| **§D5.7** | 853 | *Const pool and MarkPack* |
| D5.7.1* | 855 | One binary encoding has four users |
| D5.7.2* | 861 | Sealing appends; stream bytes never change |
| **§D6** | 868 | *Execution State* |
| **§D6.1** | 870 | *Stacks* |
| D6.1.1v3 | 872 | Three stack mechanisms have different owners |
| D6.1.2 | 889 | Frames are static |
| D6.1.3 | 894 | Only upward flows and outliving stores re-home a value |
| D6.1.4 | 898 | Cleanup runs before the watermarks are restored |
| **§D6.2** | 903 | *Scalar homes and wide returns* |
| D6.2.1* | 905 | Wide returns use a companion lane |
| D6.2.2 | 918 | A scalar home is a payload word owned by a destination |
| D6.2.3* | 926 | Watermark ownership selects the wide-return path |
| **§D6.3** | 937 | *GC rooting* |
| D6.3.1 | 939 | Generated code uses safepoint-current canonical slots |
| D6.3.2 | 944 | A collection starts only in a `MAY_GC` call |
| D6.3.3 | 948 | Native helpers root through slot-backed frames |
| D6.3.4 | 955 | The representation class decides rootability |
| D6.3.5 | 961 | Arguments use a fixed frame suffix |
| D6.3.6 | 966 | Recovery frames form one chain per activation |
| **§D6.4** | 974 | *Context and globals* |
| D6.4.1 | 976 | One `EvalContext` per isolate and one TLS root |
| D6.4.2 | 981 | Context state lives in opaque capsules |
| D6.4.3 | 986 | No context value is at a fixed address in code |
| D6.4.4 | 991 | Registries are frozen; hot paths stay lock-free |
| **§D7** | 999 | *Functions and Calls* |
| **§D7.1** | 1001 | *The effect color* |
| D7.1.1v4* | 1003 | Every callable boundary carries its effect contract |
| D7.1.2* | 1016 | The bit is load-bearing |
| D7.1.3* | 1030 | Other effects are separate facts that fail closed |
| **§D7.2** | 1036 | *Function values and closures* |
| D7.2.1 | 1038 | A function value has a definition-site identity |
| D7.2.2 | 1044 | Dynamic calls use per-callee entries |
| D7.2.3* | 1055 | Closures respect capture permissions |
| D7.2.4 | 1064 | A closure environment is traced precisely and stays boxed |
| **§D7.3** | 1069 | *Dual-function compiling* |
| D7.3.1* | 1071 | A boxed boundary plus a bounded set of raw variants |
| D7.3.2 | 1082 | The check lives in the callee |
| D7.3.3 | 1088 | Correctness is relative to the source |
| D7.3.4* | 1093 | Visibility decides which entries exist |
| D7.3.5 | 1103 | One numeric admission rule |
| **§D7.4** | 1108 | *Dispatch and the call ABI* |
| D7.4.1 | 1110 | No inline caches |
| D7.4.2* | 1119 | Direct calls pass ABI operands |
| D7.4.3* | 1123 | Every guest helper uses an explicit completion ABI |
| **§D7.5** | 1135 | *Concurrency* |
| D7.5.1 | 1137 | One scheduler per context |
| D7.5.2* | 1142 | Workers are share-nothing isolates |
| D7.5.3 | 1148 | A fault stays in one thread and one stack |
| **§D7.6** | 1154 | *System built-ins* |
| D7.6.1 | 1156 | Public sys funcs return boxed Items |
| D7.6.2 | 1162 | Raw helpers are only for adapters |
| D7.6.3* | 1168 | A native procedure can take `var` parameters |
| D7.6.4* | 1180 | A native procedure writes through its home in a fixed order |
| D7.6.5* | 1196 | A native function can declare that it only reads its arguments |
| **§D8** | 1208 | *Compilation and Execution Tiers* |
| **§D8.1** | 1210 | *Front ends* |
| D8.1.1 | 1212 | First-party C parsers are the production front ends |
| **§D8.2** | 1224 | *The unified AST* |
| D8.2.1* | 1226 | Language is a property of the compilation unit |
| D8.2.2* | 1231 | Keep syntax variance as fields; never rewrite the tree |
| D8.2.3* | 1238 | Extract the common core after convergence |
| D8.2.4* | 1241 | One indexed compilation unit |
| D8.2.5* | 1247 | One typed pass manager; facts live on the AST |
| D8.2.6* | 1256 | Lowering is driven by demand |
| **§D8.3** | 1265 | *Lambda execution tiers* |
| D8.3.1v3* | 1267 | The AST interpreter is the baseline tier |
| D8.3.2* | 1277 | Only hit counters trigger promotion; a handoff is one way |
| D8.3.3* | 1287 | Promoted code enters through the boxed wrapper |
| D8.3.4* | 1295 | Background compilation publishes only at safe points |
| D8.3.5 | 1304 | Static import prebuild is a shared keyed-future service |
| D8.3.6 | 1314 | The REPL is one persistent T0 session |
| **§D8.4** | 1323 | *LambdaJS execution tiers* |
| D8.4.1* | 1325 | One runtime, separate semantic walkers |
| D8.4.2* | 1333 | AUTO starts in the AST; MVP-Lmd is the MIR engine |
| D8.4.3 | 1342 | Dynamic source always runs in the AST interpreter |
| D8.4.4 | 1350 | All tiers share one JavaScript semantic contract |
| D8.4.5* | 1361 | JS promotion has closed admission and keeps callable identity |
| D8.4.6 | 1371 | JavaScript modules use the shared module registry |
| **§D8.5** | 1380 | *Caches* |
| D8.5.1* | 1382 | The L1 script cache is in-process |
| D8.5.2* | 1394 | A disk cache stores a code image with a relocation journal |
| D8.5.3* | 1400 | Every cache entry is born verified |
| **§D8.6** | 1409 | *Emission testing* |
| D8.6.1 | 1411 | The emission ratchet has 0% slack |
| D8.6.2 | 1417 | One artifact contract |
| D8.6.3 | 1421 | Dynamic oracles verify liveness |
| D8.6.4* | 1426 | The unified-AST consolidation has hard exit ratchets |
| **§D9** | 1437 | *Build, Modules and Packages* |
| **§D9.1** | 1443 | *Build layering* |
| D9.1.1v4* | 1445 | Input, Radiant and Lambda have separate owners |
| D9.1.2v2* | 1455 | The input service owns resource acquisition |
| D9.1.3v2* | 1461 | Provider ownership beats include ownership |
| D9.1.4v2* | 1465 | The input project owns the Mark API |
| D9.1.5* | 1469 | GNU Make and Premake define the project build pipeline |
| **§D9.2** | 1476 | *Build profiles* |
| D9.2.1v2* | 1478 | Headless has three profiles |
| D9.2.2 | 1486 | `lambda-cli` is the runtime-only host |
| D9.2.3 | 1492 | `lambda-wasm` is the browser evaluation profile |
| **§D9.3** | 1503 | *Script packages* |
| D9.3.1 | 1505 | A package is the compilation unit |
| D9.3.2 | 1511 | Imports bind boxed entries; initialization is a transaction |
| D9.3.3 | 1516 | Packages are cached in-process and distributed as source |
| D9.3.4 | 1519 | `lambda.*` is the one root for shipped code |
| D9.3.5 | 1534 | One import resolver serves every caller |
| **§D9.4** | 1542 | *Jube modules* |
| D9.4.1 | 1544 | The core is closed; everything else is a module |
| D9.4.2 | 1550 | Modules are verified, negotiated and transactional |
| D9.4.3 | 1557 | There is one strict host-API tier |
| D9.4.4 | 1563 | No module links another |
| D9.4.5 | 1567 | Every module kind names its conformance gate |
| D9.4.6* | 1571 | The host exports only an allowlist |
| **§D9.5** | 1579 | *Jube data and async contracts* |
| D9.5.1 | 1581 | Items cross; host structs use virtual carriers |
| D9.5.2 | 1589 | Modules are shielded from the async substrate |
| D9.5.3* | 1597 | Guest hosting crosses no core types |
| D9.5.4 | 1606 | Declared interfaces are the only host-object protocol |
| D9.5.5* | 1613 | Three virtual carriers mirror the container taxonomy |
| D9.5.6v2* | 1623 | Lambda and Radiant meet through explicit contracts |
| **§D10** | 1630 | *Documents and Shipped Packages* |
| **§D10.1** | 1632 | *Document formats* |
| D10.1.1 | 1634 | `.mark` is the one Mark file extension |
| D10.1.2 | 1640 | An explicit format overrides the filename |
| D10.1.3 | 1644 | A domain document in Mark syntax has its own format |
| D10.1.4 | 1651 | XHTML uses XML syntax and gives the HTML element tree |
| D10.1.5 | 1660 | SVG always uses HTML5 SVG parsing |
| D10.1.6 | 1666 | SVG export serializes the current DOM |
| D10.1.7* | 1671 | An animation snapshot uses the virtual-timer frame |
| **§D10.2** | 1678 | *Document packages* |
| D10.2.1* | 1680 | The DOM behavior package owns the editing policy |
| D10.2.2* | 1690 | LaTeX is read by TeX expansion, never by TeX typesetting |
| D10.2.3* | 1704 | Document math keeps the layout policy of its notation |
| D10.2.4 | 1711 | Bundled fonts are shared resources |
| D10.2.5* | 1715 | Graphical document editing keeps isolated errors read-only |
| **§D11** | 1724 | *Security and Capabilities* |
| **§D11.1** | 1729 | *Trust and IO* |
| D11.1.1* | 1731 | There are three trust tiers |
| D11.1.2* | 1739 | One central IO API is the only IO door |
| **§D11.2** | 1745 | *Apps and scripts* |
| D11.2.1* | 1747 | Apps and scripts run inside an admitted root |
| D11.2.2* | 1756 | Create freely; overwrite only with approval |
| D11.2.3* | 1764 | The trusted host owns the grant settings |
| D11.2.4* | 1772 | Network access is denied by default; updates are a host service |
| D11.2.5* | 1781 | Uninstall keeps private app data |
| **§D11.3** | 1787 | *Media capabilities* |
| D11.3.1* | 1789 | Audible output depends on the document origin |
| D11.3.2* | 1801 | The microphone needs explicit acquisition and separate grants |
| DO1 | 1891 | Nullable-lane residue: boxed fallback for `any`, `number` and unions; |
| DO2 | 1895 | Scalar-home audits: result-bearing dispatch helpers, resume-slot reads, exception-peek rematerialization, the … |
| DO3 | 1898 | Boxing: do union parameters stay boxed? Does the return type take part in unboxed-call selection? What is the … |
| DO4 | 1900 | `MirValue`: its size (measure first), and whether the planned occurrence source replaces the actual-rep … |
| DO25 | 1903 | Node identity (D3.1.3, S5.1.4v2): where the identity lives, the universal ID form, which operations copy it, … |
| DO27 | 1906 | Instance type alteration (S2.1.5): can reconstruction reuse the old buffer, and how are extra fields carried? … |
| DO28 | 1908 | Tier carrier divergence for annotated module bindings: one carrier for each declared contract at the shared … |
| DO31 | 1911 | Can an object declaration also name an element tag, and how do the tags of a hierarchy relate? [D3.1.1] |
| DO32 | 1913 | Virtual PDF output: wider pure-producer demand, retained-child representation (SO48), remaining object … |
| DO5 | 1918 | Numeric GC fallback: bring the per-boundary inventory to zero, then delete the scalar heap |
| DO6 | 1921 | Side-root slot metadata (boxed Item versus raw pointer), needed only for a moving collector; |
| DO7 | 1924 | Stack maps (kept in view): the only route to zero rooting cost in the mutator; |
| DO8 | 1927 | COW counts: a saturating count waits for live-corpus counters |
| DO9 | 1929 | Memory Context stage 2, including MIR code-page bytes; |
| DO10 | 1931 | Stack-frame checks: concat without tail-payload rebase; |
| DO24 | 1934 | Can a compiler-generated wide temporary be live across a loop back edge under the D6.2.3 reclaim? The emitter … |
| DO11 | 1939 | Dual functions: promotion-aware numeric inference, guard-failure counters, lazy generation of cold boxed … |
| DO12 | 1942 | MIR cache: GC-root registry reset under L1, thunk identity under lazy generation, a growth bound for the name … |
| DO13 | 1944 | Emission testing: guest-transpiler scope, the MT6 coupling spike, a clean zero-root-elision fixture, and the … |
| DO14 | 1947 | Unified AST: the "views" variance tier (adopt only when a shared pass needs it) |
| DO29 | 1949 | Typed `var` rebinding: arbitrary places, async rebinding, and a scalar-lane binding passed to an untyped … |
| DO33 | 1951 | Interpreter tier breadth (D8.3.2): satellite budgets under MT7, cross-context promotion cells, loops outside … |
| DO16 | 1957 | Name identity: the dynamic-intern growth bound, the pooled-flag audit, GOT rebuild on L1 hits, mapped-backing … |
| DO17 | 1960 | Const pool: decimal sharing, datetime packing, cross-module literal dedup, mapped-leaf page retention, the … |
| DO18 | 1963 | Sys-func registry: the public mutation convention, the public result of `splice`, and the confirmed ArrayNum … |
| DO19 | 1965 | Jube: the globals allowlist audit, the WPT harness, the data-pack design, `dynamic_lookup` retirement, the … |
| DO20 | 1969 | Cross-isolate lifetime for shared graph Items (promote-on-share is recommended) |
| DO21 | 1971 | Static modules: Class F scheduling, the profile-B headless audit, the runtime-native IO inventory and Windows … |
| DO22 | 1975 | First-class `Type*` representation needs its own design record: the kind-discriminated layout, the canonical … |
| DO23 | 1978 | Shapes: cross-`Input` type sharing and shape serialization |
