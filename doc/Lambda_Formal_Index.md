# Lambda Formal Spec Index

> **Generated** by `utils/formal_index.py` — do not edit. After editing either
> formal spec run `make formal-index`; `make check-formal-index` (part of
> `make lint`) fails while this file is stale. Not normative: the specs win.

One row per ruling (`S#`/`D#`), invariant (`SI#`/`DI#`) and open issue
(`SO#`/`DO#`), with the spec line where it starts. `*` marks a ruling that is
not or only partly implemented (see the spec's Appendix A). To read a ruling
in full — including lines too long for a viewer — run
`python3 utils/formal_index.py D9.4.6` (an ID or a section such as `S4.5`).

## Semantics — [`Lambda_Formal_Semantics.md`](Lambda_Formal_Semantics.md) v88.0.0 (2026-11-04)

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
| S1.10 | 86 | The effect doctrine |
| S1.11 | 98 | References, not authorities |
| SI1 | 110 | Boxing invisibility |
| SI2 | 112 | COW unobservability |
| SI3v2 | 116 | Inference evaluation-invariance |
| SI4 | 135 | Equality laws |
| SI5 | 139 | Order refinement |
| SI6 | 141 | Printer injectivity |
| SI7 | 144 | Int totality |
| SI8 | 148 | Poison symmetry |
| SI9v2 | 152 | Truthiness tag-decidability |
| SI10 | 155 | Literal strictness, data totality |
| SI11 | 159 | Total reads, checked writes |
| SI12 | 162 | The length law |
| SI13 | 165 | No aliasing |
| SI14v3 | 168 | A binding's static type is never a lie |
| SI15 | 180 | Schedule invisibility |
| **§S2** | 187 | *The Value Domain* |
| **§S2.1** | 189 | *Types* |
| S2.1.1v4 | 191 | Scalars: `null`, `bool`, `int`, `integer`, `i64`, `u64`, sized ints `i8 i16 i32 u8 u16 u32`, `f16 f32`, … |
| S2.1.2 | 207 | `number` is a declared union only; |
| S2.1.3v2* | 209 | An object type `type T { … }` declares a structure of exactly one structural kind |
| S2.1.4* | 222 | A nominal instance is sealed to its type and open in its fields |
| S2.1.5* | 234 | Instance type alteration is reconstruction |
| **§S2.2** | 241 | *Empty and solid values* |
| S2.2.1 | 243 | `""` is a genuine `string` with `len 0` |
| S2.2.2v2 | 245 | `symbol` and `binary` are **solid types**: every value has `len ≥ 1` |
| S2.2.3 | 252 | Element construction normalizes empty text: `<e "">` ≡ `<e>` |
| S2.2.4 | 255 | An empty file is a childless `<file>` element, not an empty binary: existence lives in the container, content … |
| **§S2.3** | 258 | *Ordered storage, unordered equality* |
| S2.3.1v2 | 260 | Maps store keys in source/insertion order while map **equality compares keys unordered** (§S5.4) |
| **§S2.4** | 272 | *Paths* |
| S2.4.1v2* | 274 | Path values use dotted steps |
| S2.4.2v5* | 283 | Every hierarchical reference is a typed root plus ordered operations; |
| S2.4.3v3* | 304 | Paths, names, symbols, and member expressions use this one reference scheme but retain distinct evaluation … |
| S2.4.4* | 324 | Each evaluation's immutable resolver deterministically maps logical `/` prefixes, namespaces, and provider … |
| S2.4.5v2* | 331 | Paths have three root forms: rooted `/.a.b`, relative `\.a.b`, and absolute `SchemeName...` |
| S2.4.6 | 342 | A wildcard step may sit anywhere in a path, and a force reads exactly the files that a spelled step enters |
| S2.4.7 | 367 | A query step on a path follows the wildcard rule: it never crosses the file boundary by itself |
| **§S2.5** | 390 | *Lists and blocks* |
| S2.5.1v2 | 392 | A list spreads; an array does not |
| S2.5.2v2 | 402 | A for-expression produces a list, whatever its clauses |
| S2.5.3v3* | 405 | A functional block `{…}` produces a list |
| S2.5.4v3 | 415 | Declarations produce no result, not a `null` result |
| S2.5.5v2 | 426 | A list has at least two items |
| S2.5.6 | 441 | A list is transient; where it lands decides |
| S2.5.7v4 | 452 | A list is built, never computed: functions and pipes return an array for any sequence input; operators keep … |
| S2.5.8 | 477 | Text is placed as one value and walked as a sequence |
| **§S2.6** | 488 | *Content* |
| S2.6.1v2 | 490 | Element content and script top level share one content model |
| S2.6.2 | 502 | Absent and empty items are dropped |
| S2.6.3 | 506 | Lists spread into content; arrays do not (S2.5.1v2) |
| S2.6.4 | 512 | Adjacent strings merge, adjacent binaries merge, nothing else does |
| S2.6.5 | 520 | Content stays normalized under mutation |
| S2.6.6v3* | 527 | An explicitly PDF-formatted file selects a byte-output destination |
| **§S3** | 559 | *Truthiness* |
| S3.1v2 | 561 | The falsy set is exactly **`null` · `false` · `""`** |
| S3.2v2 | 566 | Truth classification is decidable from the type tag alone |
| S3.3v3 | 571 | Consequences to teach: truthy-0 keeps `or` a safe coalescing operator (no `??` needed); |
| S3.4 | 580 | Logical evaluation preserves an encountered error |
| S3.5 | 587 | An `if` expression evaluates its condition once |
| **§S4** | 596 | *Numerics* |
| **§S4.1** | 611 | *The `int` type (v5: int53, total)* |
| S4.1.1 | 616 | The `int` domain is **{n ∈ ℤ : \|n\| ≤ 2⁵³−1} plus the three closure points `inf`/`-inf`/`nan`**, carried … |
| S4.1.2 | 622 | `int` arithmetic is **closed and total**: `+ - * div % neg abs`, bitwise `& \| ^`, shifts, and `**` never … |
| S4.1.3 | 627 | Aggregates use **mathematical-value semantics**: the result is the true sum if in band, `±inf` by the true … |
| S4.1.4 | 632 | `int ∥ i64`: poison has no `i64` home; |
| S4.1.5 | 636 | Machine ints are Go-aligned: runtime overflow wraps two's-complement; |
| S4.1.6 | 639 | A successful `int(x)` result is always an `int` |
| **§S4.2** | 650 | *Poison* |
| S4.2.1 | 652 | `int` and `float` share **one poison identity**, spelled bare `inf` / `-inf` / `nan` |
| S4.2.2 | 656 | A value shared across domains **types as its narrowest**: `type(nan)` → `int`; |
| S4.2.3 | 659 | Same-signed infinities are **one value** across all poison-bearing domains (`inf == decimal.inf`); |
| S4.2.4 | 663 | Classification flows up: widening admits poison with no check (`nan` flows into a `float` or `integer` … |
| **§S4.3** | 668 | *Literals and ingestion* |
| S4.3.1 | 670 | Literal type is **lexical, never value-selected**: a numeric literal is `int` iff it has no decimal point and … |
| S4.3.2 | 673 | An unsuffixed int-form literal outside ±(2⁵³−1) is a **compile error** |
| S4.3.3 | 675 | The suffix names the type: `n` = `integer` always; |
| S4.3.4 | 678 | Data cannot be rejected |
| **§S4.4** | 682 | *Promotion lattice* |
| S4.4.1 | 684 | One subsumption principle: `T1 ⊑ T2` iff every T1 value embeds **exactly** into T2 |
| S4.4.2 | 689 | Meets are **type-directed, never magnitude-directed**: int×int→int; |
| S4.4.3 | 692 | Every **bounded** integer type (`i8`…`u32`, `int`, `i64`, `u64`) meets `float` in **`float`**; |
| S4.4.4 | 696 | Sized×sized selects the smallest containing machine lane and stays there for `+ - *`, bitwise, shifts (`i8 + … |
| **§S4.5** | 701 | *Division and modulo* |
| S4.5.1 | 703 | A **literal** zero divisor (`x div 0`, `x % 0`) is a compile error; |
| S4.5.2 | 706 | `/` is true division, domain-selected: `int / int → float`; |
| S4.5.3 | 709 | `div` and `%` **stay in their operand domain**: `int div int → int`; |
| S4.5.4 | 714 | `div` truncates toward zero; |
| S4.5.5 | 717 | Vectorized integer division is per-lane: the result stays an int-family array with `inf`/`nan` at offending … |
| **§S4.6** | 721 | *Decimal* |
| S4.6.1 | 723 | `decimal` is one source-level type with invisible storage tiers (decimal128 when it fits, unbounded above); |
| S4.6.2 | 726 | `+ - *` are exact and may grow storage; |
| **§S4.7** | 731 | *Float ↔ decimal* |
| S4.7.1 | 733 | A float denotes its shortest round-trip decimal |
| **§S4.8** | 740 | *Printing* |
| S4.8.1 | 742 | Floats print as their shortest round-trip decimal; |
| S4.8.2 | 746 | Finite `int` values print as integers at every magnitude |
| **§S4.9** | 751 | *Guest boundary* |
| S4.9.1 | 753 | Numeric FFI is **type-directed, never value-directed** |
| **§S5** | 762 | *Equality* |
| **§S5.1** | 764 | *Error-preserving deep value equality* |
| S5.1.1v2 | 766 | `==` is deep, structural, value-based equality |
| S5.1.2v2 | 772 | NaN is unequal; errors propagate |
| S5.1.3 | 777 | Structural recursion is depth-limited; |
| S5.1.4v3* | 780 | `==` is the only **value** equality and compares content alone |
| S5.1.5* | 792 | Container equality recursively compares content and short-circuits |
| **§S5.2** | 803 | *Numbers* |
| S5.2.1v2 | 805 | Numeric equality is exact mathematical-value equality across ranks: `1 == 1.0 == 1n == 1.0m == 1.00m`; |
| **§S5.3** | 811 | *Sequences* |
| S5.3.1 | 813 | `range`, `list`, and `array` are one sequence family: `(1 to 3) == [1,2,3]`, element-wise in order |
| **§S5.4** | 817 | *Maps, objects, elements* |
| S5.4.1 | 819 | Map equality is key-unordered (JSON/XML, the document standards); |
| S5.4.2v3* | 821 | A nominal value equals only a value of the **same nominal type**, whatever its structural kind; |
| S5.4.3 | 831 | Element equality = tag + namespace, attributes as an unordered map (XML InfoSet), children ordered (document … |
| **§S5.5** | 834 | *Functions and types* |
| S5.5.1v2* | 836 | Function equality is **intensional**: same definition site + deep-equal captures |
| S5.5.2 | 843 | Type equality is **representational** |
| **§S5.6** | 847 | *Dedup, grouping, hashing* |
| S5.6.1v2* | 849 | `unique`, set semantics, and grouping use `==`: nulls group together and each `nan` stands alone |
| S5.6.2 | 854 | `a == b ⟹ hash(a) == hash(b)` wherever values are hashed: maps hash in canonically sorted key order; |
| **§S6** | 860 | *Ordering and Sort* |
| **§S6.1** | 862 | *Two relations, by design* |
| S6.1.1 | 866 | The comparison operators `< <= > >=` answer magnitude questions only |
| S6.1.2v2 | 872 | An error operand to `< <= > >=` propagates unchanged, including against null or nan |
| **§S6.2** | 877 | *The Lambda total order* |
| S6.2.1 | 879 | `sort` and `order by` use the separate total order:* > **null < false < true < number (by value) < datetime < … |
| S6.2.2v3 | 887 | The total order **totally refines `==`** |
| S6.2.3 | 895 | Sort is stable; |
| **§S7** | 900 | *Absence and Errors* |
| **§S7.1** | 904 | *Reads are total; writes are checked* |
| S7.1.1v3* | 906 | Every invalid member/index **read yields `null`** |
| S7.1.2 | 914 | Slices **clamp** to bounds and return an empty collection (or `""` for string results |
| S7.1.3v3* | 921 | Every invalid member/index **write fails with a language error**, subject to S7.4.1v2/S7.5 |
| **§S7.2** | 930 | *Indexing and `last`* |
| S7.2.1 | 932 | Indexing is 0-based; |
| S7.2.2 | 936 | Reaching from the end is the reserved word **`last`** = `len(container) − 1` of the innermost enclosing … |
| S7.2.3 | 941 | The two-homes rule |
| S7.2.4 | 945 | Functions never take the `last` keyword and never accept signed counts |
| **§S7.3** | 951 | *Aggregation* |
| S7.3.1 | 953 | A `null` input makes the aggregate `null` |
| S7.3.2 | 957 | Monoid identities: `sum([])` = 0, `prod([])` = 1; |
| **§S7.4** | 961 | *Ordinary errors and system faults* |
| S7.4.1v2 | 966 | Ordinary errors are values with uniform acknowledgement |
| S7.4.2v4 | 972 | `T^` is shorthand for `T \| error` in every type position, including function returns |
| S7.4.3 | 985 | System faults |
| S7.4.4 | 989 | An error value is a first-class value carrying `code`, `message`, and source location; |
| S7.4.5v2* | 996 | System `fn` failures and effectful-reader failures obey the same ordinary-error discipline |
| S7.4.6v3* | 1003 | `raise v` completes the enclosing callable early with an error |
| **§S7.5** | 1011 | *Acknowledgment* |
| S7.5.1v6 | 1013 | Acknowledgement follows possible ordinary-error outcomes |
| S7.5.2v4 | 1025 | Binding acknowledgement follows type admission |
| S7.5.3v3 | 1045 | `a or b` defaults only `null`, `false`, and `""`; |
| S7.5.4 | 1052 | Acknowledgement composes through error-preserving operations |
| S7.5.5 | 1060 | An unacknowledged fallible result may not be silently overwritten in a procedural sequence |
| S7.5.6 | 1069 | Explicit error classification acknowledges the tested outcome |
| S7.5.7 | 1077 | No `empty` keyword or predefined error-or-null shorthand |
| **§S7.6** | 1085 | *Discharge: handlers and postfix `^`* |
| S7.6.1v9* | 1087 | `e ^ { … ^ … }` is the unfiltered postfix error handler and is origin-independent |
| S7.6.2v5 | 1117 | The unfiltered and filtered handlers are left-associative postfix-primary forms `primary ^ { body }` and … |
| S7.6.3v3* | 1130 | Postfix `e^` propagates |
| S7.6.4 | 1141 | `?` is not propagation |
| S7.6.5v4* | 1144 | Retired split bindings `let a^err = e` and prefix error tests `^err` / `if (^err)` remain unavailable; |
| S7.6.6v6* | 1152 | Division of labor: `or` defaults ordinary falsy values and preserves errors; |
| S7.6.7v5* | 1160 | Procedure handlers produce values under the same rules as function handlers |
| S7.6.8 | 1169 | `e or^ { h }` combines ordinary-error recovery and falsy defaulting |
| S7.6.9 | 1179 | `e ^ErrorType { h }` handles matching ordinary errors |
| **§S7.7** | 1194 | *Validation outcomes and boundary behavior* |
| S7.7.1v3* | 1199 | Binding validation is an expression-level operation |
| S7.7.2v2 | 1207 | A deferred initializer binds its validation outcome |
| S7.7.3* | 1217 | A declared parameter guards at the **call site**: on failure the function is not entered and **the call … |
| S7.7.4v2 | 1221 | Whole-variable reassignment uses the initializer admission rules unchanged |
| S7.7.5v3 | 1232 | Initializer or whole-variable reassignment failure inside a loop does not skip its body or abandon the loop |
| S7.7.6v2* | 1239 | Element/field stores keep their separate failure contract |
| S7.7.7v4 | 1248 | Validation errors can be handled as ordinary values |
| **§S7.8** | 1256 | *Containers and errors* |
| S7.8.1* | 1258 | Acceptance is read from the **destination contract**, never from syntactic position |
| S7.8.2 | 1268 | Both spellings are visible in source: `[ f(x) ^ { 0 } for x in xs ]` → `int[]`, lane preserved; |
| **§S7.9** | 1275 | *What an error participates in* |
| S7.9.1 | 1280 | Type family participates |
| S7.9.2v3 | 1282 | Logical evaluation and comparisons propagate |
| S7.9.3 | 1289 | Value family propagates |
| S7.9.4 | 1295 | Containment is not participation |
| **§S7.10** | 1300 | *The sys-func return contract* |
| S7.10.1v3 | 1305 | Result shape is fixed by the contract, never by cardinality, for array, list, and text inputs: zero-to-many → … |
| S7.10.2 | 1311 | Admission is an explicit per-function contract: an **admissive** case has a meaningful no-answer reading and … |
| S7.10.3 | 1318 | No in-band sentinels, ever |
| S7.10.4 | 1324 | Error operands are rejected at the call boundary (parameters are `any \ error`), keeping "error operand" … |
| S7.10.5v3* | 1326 | Vectorized sys funcs are sequence-in, array-out (S2.5.7v4): array or list in → array out, one lane never … |
| S7.10.6 | 1332 | A mutator family picks one public convention (updated owner, or unit) and holds it; |
| **§S7.11** | 1336 | *System faults and recovery* |
| S7.11.1v2 | 1338 | Fault reasons are a closed, typed native set: stack overflow, side-stack exhaustion, out-of-memory, and … |
| S7.11.2v2 | 1344 | Faults pass transparently through `fn` frames; |
| S7.11.3 | 1351 | Transaction barriers (module init, hosted-guest entry) take priority over inner handlers: no handler may … |
| S7.11.4 | 1356 | Production containment is fail-stop: recoverable faults use recovery frames; |
| **§S8** | 1364 | *Membership and Iteration* |
| **§S8.1** | 1366 | *The `in`/`at`/`of` axis* |
| S8.1.1v2 | 1368 | Three axes, one key class each |
| S8.1.2v3 | 1383 | On elements and objects, `in` ranges over attribute values then children; |
| S8.1.3v2 | 1389 | Axis and arity are independent |
| **§S8.2** | 1408 | *The key space* |
| S8.2.1v4* | 1410 | Every container has a fixed key domain |
| S8.2.2v6* | 1433 | One name space, keyed by namespace and normalized spelling |
| S8.2.3* | 1468 | Methods are members of the type, never of the value |
| S8.2.4v3* | 1479 | Two subscript worlds: a type key answers as XPath, a positional selection as NumPy |
| S8.2.5 | 1481 | The item step `.*` and its closure |
| S8.2.6 | 1498 | A run is stepped set-at-a-time by every key kind except positional |
| **§S8.3** | 1520 | *`len`* |
| S8.3.1v3 | 1522 | The law: **`len(x)` is the number of iterations `for (i in x)` performs.** Consequences, not separate rules: … |
| S8.3.2* | 1535 | Lazy sequences: a forceable stream's `len` forces and returns the actual size; |
| S8.3.3v3 | 1540 | Two lengths: `len` measures content; |
| **§S8.4** | 1551 | *Projections* |
| S8.4.1v2 | 1553 | `keys(c)` ≡ `[for (k, v in c) k]`; |
| **§S9** | 1562 | *Mutability: Mutable Value Semantics* |
| **§S9.1** | 1564 | *The model* |
| S9.1.1 | 1571 | `let` is final: nothing reachable through a `let` binding ever changes |
| S9.1.2 | 1576 | Binding, assignment, and construction copy, observably, for every container kind |
| S9.1.3v3* | 1579 | `var` parameters (`pn f(var a: T)`) are explicit inout borrows; |
| S9.1.4v2* | 1590 | Capture permissions follow the callable's colour |
| S9.1.5v2 | 1604 | No reference cells; |
| S9.1.6* | 1610 | Member/index assignment may update or add only a member admitted by the container's existing key domain; |
| S9.1.7v2* | 1619 | No module-level mutable binding |
| **§S9.2** | 1631 | *Covariance, borrows, and views* |
| S9.2.1 | 1633 | *Covariance where values copy, invariance where they're borrowed*: `int[] <: any[]` holds for `is`, reads, … |
| S9.2.2v2* | 1637 | Read views are first-class values with snapshot semantics (a zero-copy slice observably behaves as a copy … |
| S9.2.3* | 1645 | Iterating a `var` container walks the entry-time value: the loop share-marks at the head, and the first … |
| S9.2.4v2* | 1649 | A **view-state** `var` may not be passed as a `var` argument (no call-site check can see the callee's … |
| **§S9.3** | 1657 | *Construction captures values* |
| S9.3.1* | 1659 | Placing a value into a container captures it **by value** at every constructor and insertion point |
| S9.3.2 | 1665 | Construction preserves each element's type and precision |
| **§S10** | 1683 | *Operators* |
| **§S10.1** | 1685 | *Union, pipe, and filter* |
| S10.1.1v5* | 1687 | `\|`, `&` and `!` are the type operators |
| S10.1.2v4 | 1688 | The pipe is dual-mode on a parse-time syntactic test: a body with a **free `~`** is a mapping pipe (binds `~` … |
| S10.1.3 | 1701 | `~` is lexically scoped to the RHS of its pipe; |
| S10.1.4 | 1705 | File write/append syntax is deferred; |
| S10.1.5v3 | 1707 | `that` is the single-value proviso |
| S10.1.6 | 1723 | `\|:` is the filter stage of the pipe family |
| S10.1.7v4* | 1746 | A body about one subject may leave `~` implicit; the pipe family always spells it |
| **§S10.2** | 1782 | *Vectorization* |
| S10.2.1 | 1784 | Arithmetic `+ - * /` is vectorized (element-wise with broadcasting) |
| S10.2.2v2* | 1789 | Bare comparisons `< <= > >=` are **scalar-only**, never element-wise (a mask is a container, containers are … |
| S10.2.3 | 1797 | Mask consumption is explicit and non-magical: `sum(mask)` counts true lanes; |
| S10.2.4 | 1801 | Element-wise arithmetic promotes exactly as scalar arithmetic does: each result element has the type the … |
| **§S10.3** | 1816 | *Keyword operators* |
| S10.3.1v3 | 1818 | `and or not is in to div that at eq ne lt le gt ge` |
| **§S10.4** | 1827 | *Parent navigation* |
| S10.4.1* | 1829 | Postfix `.~~` is the parent-navigation step at the ordinary member/index precedence tier; |
| S10.4.2* | 1835 | Bare `~~` is exactly `~.~~`: it is valid exactly where `~` is bound, selects the innermost current-value … |
| S10.4.3v2* | 1841 | Contextual parent navigation is occurrence-based and carries lineage in the evaluation context as a … |
| **§S10.5** | 1848 | *Root navigation* |
| S10.5.1* | 1850 | `/` is the one root-selection operation |
| S10.5.2* | 1855 | For a path, `./` selects its logical or provider/authority anchor |
| S10.5.3v2* | 1860 | Dynamic root navigation is occurrence-based |
| **§S10.6** | 1865 | *Concatenation* |
| S10.6.1 | 1867 | `++` concatenates sequences and appends scalars |
| **§S11** | 1883 | *Types and Patterns* |
| **§S11.1** | 1885 | *Types compose like values* |
| S11.1.1v3* | 1887 | A bracket type is a structural pattern whose positions mix values, types, and occurrence runs freely: `[1, … |
| S11.1.2v4 | 1898 | String and symbol structural patterns share the delimiter `\( ... )`; quotes and named patterns determine one … |
| S11.1.3 | 1933 | A range type `X to Y` denotes inclusive membership in the consecutive values between its bounds |
| S11.1.4v2 | 1941 | The binary type relation is spelled `<:` |
| S11.1.5v2* | 1951 | Three function types: `fn`, `pn`, and their union `function` |
| S11.1.6v3 | 1969 | Two type families, split by concept: a run and an array |
| S11.1.7* | 1996 | `none` is the empty type |
| **§S11.2** | 1998 | *Match* |
| S11.2.1 | 2000 | `match` is a **type match**: arms are type expressions tried in order, first match wins, no fall-through; |
| S11.2.2 | 2007 | Poison is unequal, not untypeable: `case float:` catches nan, `case error:` catches errors; |
| S11.2.3* | 2012 | Exhaustiveness is compiler-checked: unions need every constituent, `bool` both arms, `T?` needs `T` and … |
| **§S11.3** | 2017 | *Structural `is`, nominal objects* |
| S11.3.1v2 | 2019 | `is` is structural for maps/arrays/elements (extra fields permitted; |
| **§S11.4** | 2028 | *Declared types are contracts* |
| S11.4.1v5* | 2032 | An annotation is a strict **admission contract**, not a hint |
| S11.4.2v3* | 2050 | Signatures spell both failure dimensions: plain successful `T` excludes null and error; |
| S11.4.3 | 2061 | `any` is the top type and includes error; |
| S11.4.4 | 2067 | *When the user is explicit, we check explicitly*: an explicitly declared error possibility cannot enter a … |
| S11.4.5* | 2072 | Deferred numeric admission is **value-aware**: an exactly-embedding value passes and is re-represented … |
| S11.4.6v4* | 2077 | User-defined types are enforced by the validator: deep, on first crossing, yielding an admitted value or a … |
| S11.4.7v3* | 2089 | Validation and discharge follow §S7.7–S7.8: deferred initializer and whole-variable reassignment checks … |
| S11.4.8v2 | 2093 | A function signature may establish a **type binder** with an explicit `T: type` parameter, a … |
| S11.4.9 | 2110 | A system-function registry row may declare a **type relation** from one call argument to its success result |
| S11.4.10* | 2123 | Verified at the crossing, valid while unchanged |
| S11.4.11 | 2135 | A `that` predicate is an `fn` body over the scope it is written in |
| S11.4.12v2* | 2148 | A type name binds for its whole scope and never reads as `any` |
| **§S11.5** | 2152 | *Physical binary schemas* |
| S11.5.1v2* | 2154 | `type A : binary & {…} { field: T & {…}, … }` declares a physical binary layout |
| S11.5.2* | 2165 | `binary_decode(bytes, A)` returns a lazy, read-only element backed by its byte source and schema |
| S11.5.3v3* | 2171 | A native schema names a host binding |
| S11.5.4 | 2178 | A native pointer field `T[n] & {pointer: 'native'}` exposes a lazy read-only array of contiguous fixed-width … |
| **§S12** | 2194 | *Functions, Effects, Resources* |
| **§S12.1** | 2196 | *The one-bit effect system* |
| S12.1.1v3 | 2198 | `fn`/`pn` is a declared, compiler-checked, one-bit effect system: `fn` is pure and deterministic under any … |
| S12.1.2v3* | 2210 | Local imperative control is admitted in `fn` and `pn` |
| S12.1.3v2* | 2229 | Reactive templates are the doctrine applied: template body = pure `fn` transformation; |
| S12.1.4v4* | 2233 | Effect polymorphism: the `function` declaration |
| S12.1.5v2 | 2262 | Presentation authority by state tier |
| **§S12.2** | 2276 | *Assignment* |
| S12.2.1v3* | 2278 | `let` bindings and parameters are immutable; |
| S12.2.2 | 2287 | Element mutation is defined on both faces: `elem.attr = v` as map-field assignment; |
| **§S12.3** | 2291 | *The call contract* |
| S12.3.1 | 2293 | A function has at most **16 source-language argument slots** (a rest collector consumes one); |
| S12.3.2 | 2297 | Two dynamic-call restrictions are deliberate: a dynamic call with named arguments is rejected, and a dynamic … |
| S12.3.3v2* | 2300 | Member access is resolution, not membership |
| S12.3.4v2* | 2322 | `call(f, args)` is the dynamic-application form |
| S12.3.5v2 | 2335 | Spread splices into containers, never into an argument list; the spelling follows the container's shape |
| S12.3.6 | 2350 | No arity overloading for user definitions |
| S12.3.7* | 2359 | User definitions shadow system functions — user-first, module-lexical, warned |
| **§S12.4** | 2378 | *Resources* |
| S12.4.1v3* | 2383 | `open()` is resource acquisition and is `pn`-only |
| S12.4.2* | 2394 | A resource auto-closes at the end of its enclosing **block**; |
| S12.4.3* | 2401 | No `defer`, `with`, or `finally` keyword: auto-close is the only user-facing cleanup mechanism; |
| S12.4.4v4* | 2406 | An `open` block that scopes a transaction commits it on a clean, successful exit, including when a `return`, … |
| **§S13** | 2424 | *Concurrency* |
| **§S13.1** | 2429 | *Tasks and workers* |
| S13.1.1v3 | 2431 | `start` is a builtin `pn`, not a keyword |
| S13.1.2v2 | 2438 | Calls are **colorless**: `f(x)` synchronously yields the value and may suspend invisibly (`f(x)` ≡ … |
| S13.1.3v2* | 2443 | Two tiers, one handle vocabulary: tasks (`start(f, [x])`, shared context) and isolated workers (`start(f, … |
| S13.1.4 | 2449 | The capture rule |
| S13.1.5 | 2453 | Failures are values; faults are not |
| **§S13.2** | 2459 | *Messaging* |
| S13.2.1 | 2461 | Handle = address; |
| S13.2.2 | 2466 | `send(h, msg)` never blocks and returns `ok^`: a full mailbox is the error value `'mailbox_full'` |
| S13.2.3 | 2470 | Ordering: per-sender FIFO; |
| **§S13.3** | 2475 | *Scope and cancellation* |
| S13.3.1v2 | 2477 | A started handle is a scoped resource owned by the nearest lexical block: normal exit **joins**; |
| S13.3.2 | 2485 | Cancellation is an error value at park points (`'cancelled'`), unwinding by ordinary `^` propagation with … |
| **§S13.4** | 2491 | *Determinism* |
| S13.4.1* | 2493 | Builtin numeric reductions (`sum`, `avg`, `prod`, `variance`, dot, `min`/`max` join) are … |
| S13.4.2* | 2499 | In stream pipelines, `fn` stages auto-parallelize and are **ordered by default** (a re-sequencing buffer … |
| **§S14** | 2505 | *Data Processing* |
| **§S14.1** | 2507 | *For-clause grouping and joins* |
| S14.1.1v2 | 2509 | `group by KEY [as NAME], … into g` binds, for each group, **every key under its name** and **`g` to the … |
| S14.1.2v2 | 2516 | Key equality is `==` with numeric-tower coherence (`1` and `1.0` group together); |
| S14.1.3 | 2529 | Join `on` is restricted to conjunctions of equality tests (non-equi conditions are a compile error pointing … |
| **§S14.2** | 2535 | *Verbs and windows** |
| S14.2.1* | 2537 | The verb surface is generic over row-oriented data |
| S14.2.2* | 2541 | Column references in verb arguments are `~.field` (the pipe current-item reference extended into verb scope; |
| **§S14.3** | 2546 | *Streams and laziness** |
| S14.3.1v3* | 2548 | `input()` is eager, `stream()` lazy |
| S14.3.2* | 2562 | Two stream kinds: value-backed streams are true values (re-forcible, usable in `fn`); |
| S14.3.3* | 2566 | Handles are stream sources/sinks (`stream(h)`, `send_to(h)`); |
| **§S15** | 2573 | *Metaprogramming* |
| S15.1 | 2575 | Lambda is homoiconic through elements: the canonical AST is an element tree in the ambient **`lm.` … |
| S15.2 | 2580 | Element literals are inverted quasiquotation: expression children evaluate (splice); |
| S15.3* | 2584 | `input(f, 'lambda')` parses Lambda source into the `lm.` AST; |
| S15.4 | 2590 | `name(item)` is the shadow-proof accessor for intrinsic names (element tag, function name, type name); |
| **§S16** | 2597 | *Surface Syntax* |
| **§S16.1** | 2605 | *Whitespace and separation* |
| S16.1.1* | 2607 | Line breaks carry no meaning |
| S16.1.2v2* | 2612 | `;` is a **strict separator** between statements |
| S16.1.3v2* | 2618 | Adjacent statements need **no separator** when the second begins with a token that cannot continue the first, … |
| **§S16.2** | 2629 | *Line-start classification* |
| S16.2.1* | 2631 | An **incomplete** expression continues across a line break unconditionally: a trailing operator, an unclosed … |
| S16.2.2v3* | 2634 | After a **complete** expression, a line-start token that can only continue an expression does continue it: … |
| S16.2.3v3* | 2640 | After an **open-tail** statement (S16.1.3v2), a line-start **dual-role** token |
| S16.2.4v3* | 2654 | One carve-out: `.` followed by a step other than a digit |
| S16.2.5* | 2662 | `return` followed by a line break and a start token returns that value: `return` ⏎ `42` is `return 42` |
| S16.2.6v2* | 2667 | Handler bodies open on the same line as their introducer: `e ^ { h }`, `e ^ErrorType { h }`, `e or^ { h }` |
| **§S16.3** | 2674 | *Juxtaposition* |
| S16.3.1* | 2676 | Juxtaposition **sequences, never combines.** Adjacent expressions are separate statements or content items; |
| **§S16.4** | 2684 | *Braces* |
| S16.4.1v4* | 2686 | Interior decides, wherever braces are an expression |
| S16.4.2v2 | 2701 | Empty `{}` resolves by context, and only where a tie exists |
| S16.4.3v4* | 2712 | Declaration braces are structural and never read as maps |
| **§S16.5** | 2740 | *Element scope* |
| S16.5.1v2* | 2742 | In an exposed element attribute value or bare content expression, `< > <= >=` **are not operators**: `>` … |
| **§S16.6** | 2756 | *Control forms* |
| S16.6.1* | 2758 | `if`, `for`, and `while` each have **one node with two spellings**: parenthesized head with any-expression … |
| S16.6.2* | 2763 | `(` immediately after `if` or `while` **commits** to the parenthesized spelling |
| S16.6.3* | 2768 | `else` is **optional** in both spellings |
| S16.6.4* | 2771 | `match` keeps its single braced form: its braces delimit an arm list, not a body, so no parenthesized … |
| S16.6.5v2* | 2774 | Control capability and call effects are separate |
| S16.6.6* | 2782 | Control statements require braces |
| S16.6.7v3* | 2791 | A procedure's body is always the braced statement block, and the arrow is the one anonymous function |
| S16.6.8v3* | 2808 | An imperative control block is a statement, not a general expression |
| S16.6.9v2* | 2817 | Branch homogeneity |
| **§S16.7** | 2828 | *Script top level* |
| S16.7.1 | 2830 | A script's top level is element content, not a list |
| S16.7.2v2 | 2838 | Nulls and empty strings are dropped (S2.6.2) |
| S16.7.3v2 | 2843 | Lists spread and adjacent strings or binaries merge (S2.6.3, S2.6.4) |
| S16.7.4 | 2846 | A REPL entry echoes its value unless it is declarations only |
| S16.7.5 | 2853 | A session top level rebinds nothing |
| S16.7.6 | 2858 | Two session kinds |
| **§S16.8** | 2867 | *Lexical forms* |
| S16.8.1 | 2869 | `not` is the one logical negation |
| S16.8.2 | 2873 | `not` binds loose |
| S16.8.3 | 2876 | Numeric spelling |
| S16.8.4* | 2880 | No implicit adjacent-literal concatenation |
| S16.8.5 | 2885 | Unary `+` is kept |
| S16.8.6v4 | 2888 | `*` is spread; `*` and `...` are two wildcard families, not one |
| S16.8.7 | 2900 | A single-quoted literal is a symbol, not a string |
| S16.8.8* | 2904 | The backtick syntax space is reserved and must not be spent otherwise |
| S16.8.9v2 | 2908 | Computed keys: `[expr]: val` |
| **§S16.9** | 2921 | *Declarations, elements, paths* |
| S16.9.1 | 2923 | `pub` is a uniform prefix modifier |
| S16.9.2* | 2926 | The **`apply;` fused token is retired**: bare `apply` is the keyword statement, disambiguated from … |
| S16.9.3 | 2929 | `;` has exactly one role language-wide: statement separation |
| S16.9.4 | 2940 | The relative path is spelled `\.a.b` |
| S16.9.5 | 2946 | `a?: T` marks an optional field |
| S16.9.6 | 2951 | `.` is the only import separator |
| S16.9.7 | 2955 | A rest parameter `...` closes a parameter list |
| S16.9.8 | 2960 | An import path's first token picks one of three roots |
| **§S16.10** | 2977 | *Keywords as names* |
| S16.10.1v3* | 2979 | Keywords never name bindings — where they could capture |
| S16.10.2* | 3011 | Data names admit keywords |
| S16.10.3* | 3028 | Member steps admit keywords |
| S16.10.4 | 3033 | A value name must resolve at compile time |
| **§S17** | 3045 | *System Library* |
| **§S17.1** | 3050 | *String splitting* |
| S17.1.1 | 3052 | `split` follows ECMAScript `String.prototype.split` |
| **§S17.2** | 3071 | *The system-function namespace* |
| S17.2.1 | 3073 | System functions live at `lambda.sys.*`, and the prelude imports them unqualified |
| S17.2.2 | 3079 | `lambda.sys.f` is the escape from a shadow |
| **§S17.3** | 3089 | *Process information* |
| S17.3.1 | 3091 | `sys.proc.self.argv` is the sole argument-vector path |
| **§S17.4** | 3097 | *String search positions* |
| S17.4.1 | 3099 | A text search result uses its source's code-point index domain |
| **§S17.5** | 3105 | *Sized integer conversions* |
| S17.5.1 | 3107 | Sized integer conversions use the type names as callable constructors |
| **§S17.6** | 3113 | *String replacement* |
| S17.6.1 | 3115 | `replace` steps through matches as ECMAScript `String.prototype.replaceAll` does, and inserts its replacement … |
| **§S17.7** | 3128 | *Letter case* |
| S17.7.1 | 3130 | Case-insensitive matching folds by Unicode simple case folding |
| S17.7.2 | 3142 | `lower` and `upper` map by Unicode full case mapping |
| **§S17.8** | 3150 | *Option maps* |
| S17.8.1* | 3152 | An option name a system function does not define is a compile-time error where the compiler can see it, and a … |
| SO1 | 3299 | Sized-lane `div`/`%`: [Number_Model §3.3.2](../vibe/Lambda_Semantics_Number_Model.md) says sized×sized `div` … |
| SO2 | 3300 | Int v5 §5 details: poison-algebra table ratification; |
| SO3 | 3301 | The `int?` fourth lane value (`INT_LANE_NULL`) is undocumented in the Int_Type sentinel table; |
| SO4 | 3302 | Bitwise semantics were ruled (S4.1.2), but the interaction with the retired sparse band in old goldens needs … |
| SO50 | 3305 | Error-preserving comparison follow-ups: canonical field traversal for key-unordered maps, selection among … |
| SO5 | 3311 | TE-17 transitivity: does discharging `(int \| error)[]` re-narrow in place, or only by copy? Copy is the safe … |
| SO6 | 3312 | Lazy/streaming `for` bodies vs typed-lane destinations (boxed-until-proven presumed, undecided); |
| SO7 | 3313 | TE-5 R5 sticky `any`; |
| SO8 | 3314 | Should `is` become value-aware? Deliberately undecided (S11.3.1v2 records the intentional asymmetry) |
| SO9 | 3315 | A surface spelling for `any \ error` (the `!` exclusion operator route is broken |
| SO10 | 3316 | A deep "does this data contain an error anywhere?" check (`valid(item)`-shaped) |
| SO13 | 3319 | COW granularity on large documents: node representation for spine-copying, refcount discipline for … |
| SO14 | 3320 | Nested-mutation ergonomics (`t.nodes[i].value`): path-shaped `var` borrows, `_modify`-style accessors, or … |
| SO15 | 3321 | Exclusivity granularity endpoint (whole-base vs blessed splitters vs dynamic bookkeeping) |
| SO16 | 3322 | Close-error routing (double fault): proposed |
| SO17 | 3323 | Resource-carrying-type containment rules (when a wrapping value is itself resource-typed) |
| SO18 | 3324 | Snapshot iteration (C4.2d) |
| SO19 | 3325 | Root and upward-parent navigation are resolved by S10.4.3v2, S10.5.3v2 / PTH10, PTH29: lineage lives in a … |
| SO39 | 3329 | Node identity (S5.1.4v2): which operations preserve an identity (a COW detach and an in-place `var` write are … |
| SO42 | 3336 | Instance type alteration (S2.1.5): the surface spelling, which declared fields must be satisfiable from the … |
| SO43 | 3340 | Type alteration |
| SO41 | 3343 | Cross-reference form for document graphs under S9.1.5v2: an identity or key stored as data and resolved … |
| SO20 | 3349 | O-D: cross-isolate lifetime for shared graph Items (promote-on-share recommended) |
| SO21 | 3350 | `select` surface syntax; |
| SO22 | 3351 | Deferred opt-ins: blocking send, true selective receive, `unordered` streams, CPU-bound cancellation … |
| SO23 | 3354 | PD4 join column-collision suffixes; |
| SO24 | 3355 | PD12 sub-items: `on error` resume semantics (abort vs skip-record), handler scoping over multiple forced … |
| SO25 | 3356 | Deferred group-by vocabulary: `having`-style filter, post-group `let`, extended aggregates, … |
| SO26 | 3359 | RF6 mutator convention: updated-owner vs unit; |
| SO27 | 3360 | Whether debug logging inside `fn` is a permitted non-observable effect |
| SO44 | 3361 | Binder depth on function values: whether a binder (S11.4.8v2) over a function value selects its full … |
| SO29 | 3366 | File write/append syntax (C6a: `into`/`onto` candidates); |
| SO31 | 3367 | The `<file>` element shape (name/size/mime, content as child) |
| SO32 | 3368 | Match extensions: pipe-context shorthand, string-pattern capture binding in arms, range patterns |
| SO33 | 3369 | A10 residue: the aspirational generics text, `as` assertion semantics, and open-vs-closed map matching in … |
| SO34 | 3370 | `emit()` vs `send()` |
| SO47 | 3371 | Whether an empty literal needle matches at every code-point boundary |
| SO51 | 3373 | Local imperative control in comprehensions: mutation of a containing `fn` local across iterations, … |
| SO35 | 3380 | A dedicated formal syntax document: S16 parks the surface-syntax rulings here because syntax and semantics … |
| SO38 | 3381 | Whether `\|:` over a **map** should keep the surviving keys (yielding a map) rather than dropping them … |
| SO36 | 3387 | Whether a `pn` call may appear nested inside an expression (`(pn_func(), 123)`, `if (exists(path)) …`), or … |
| SO45 | 3396 | Whether a function type takes a suffix directly: `fn?`, `fn (x: int)?`, `fn ()[]` |
| SO46 | 3406 | How an element pattern spells *content must be empty* |
| SO48 | 3407 | PDF file content: remaining object kinds, decimal encoding, virtual source containers, direct array/scalar … |

## Design — [`Lambda_Formal_Design.md`](Lambda_Formal_Design.md) v65.1.0 (2026-11-10)

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
| D6.1.1 | 872 | Three stack mechanisms have different owners |
| D6.1.2 | 884 | Frames are static |
| D6.1.3 | 889 | Only upward flows and outliving stores re-home a value |
| D6.1.4 | 893 | Cleanup runs before the watermarks are restored |
| **§D6.2** | 898 | *Scalar homes and wide returns* |
| D6.2.1* | 900 | Wide returns use a companion lane |
| D6.2.2 | 913 | A scalar home is a payload word owned by a destination |
| D6.2.3* | 921 | Watermark ownership selects the wide-return path |
| **§D6.3** | 932 | *GC rooting* |
| D6.3.1 | 934 | Generated code uses safepoint-current canonical slots |
| D6.3.2 | 939 | A collection starts only in a `MAY_GC` call |
| D6.3.3 | 943 | Native helpers root through slot-backed frames |
| D6.3.4 | 950 | The representation class decides rootability |
| D6.3.5 | 956 | Arguments use a fixed frame suffix |
| D6.3.6 | 961 | Recovery frames form one chain per activation |
| **§D6.4** | 969 | *Context and globals* |
| D6.4.1 | 971 | One `EvalContext` per isolate and one TLS root |
| D6.4.2 | 976 | Context state lives in opaque capsules |
| D6.4.3 | 981 | No context value is at a fixed address in code |
| D6.4.4 | 986 | Registries are frozen; hot paths stay lock-free |
| **§D7** | 994 | *Functions and Calls* |
| **§D7.1** | 996 | *The effect color* |
| D7.1.1 | 998 | `fn`/`pn` is infrastructure |
| D7.1.2* | 1008 | The bit is load-bearing |
| D7.1.3* | 1022 | Other effects are separate facts that fail closed |
| **§D7.2** | 1028 | *Function values and closures* |
| D7.2.1 | 1030 | A function value has a definition-site identity |
| D7.2.2 | 1036 | Dynamic calls use per-callee entries |
| D7.2.3* | 1047 | Closures respect capture permissions |
| D7.2.4 | 1056 | A closure environment is traced precisely and stays boxed |
| **§D7.3** | 1061 | *Dual-function compiling* |
| D7.3.1* | 1063 | A boxed boundary plus a bounded set of raw variants |
| D7.3.2 | 1074 | The check lives in the callee |
| D7.3.3 | 1080 | Correctness is relative to the source |
| D7.3.4* | 1085 | Visibility decides which entries exist |
| D7.3.5 | 1095 | One numeric admission rule |
| **§D7.4** | 1100 | *Dispatch and the call ABI* |
| D7.4.1 | 1102 | No inline caches |
| D7.4.2* | 1111 | Direct calls pass ABI operands |
| D7.4.3* | 1115 | Every guest helper uses an explicit completion ABI |
| **§D7.5** | 1127 | *Concurrency* |
| D7.5.1 | 1129 | One scheduler per context |
| D7.5.2* | 1134 | Workers are share-nothing isolates |
| D7.5.3 | 1140 | A fault stays in one thread and one stack |
| **§D7.6** | 1146 | *System built-ins* |
| D7.6.1 | 1148 | Public sys funcs return boxed Items |
| D7.6.2 | 1154 | Raw helpers are only for adapters |
| **§D8** | 1161 | *Compilation and Execution Tiers* |
| **§D8.1** | 1163 | *Front ends* |
| D8.1.1 | 1165 | First-party C parsers are the production front ends |
| **§D8.2** | 1177 | *The unified AST* |
| D8.2.1* | 1179 | Language is a property of the compilation unit |
| D8.2.2* | 1184 | Keep syntax variance as fields; never rewrite the tree |
| D8.2.3* | 1191 | Extract the common core after convergence |
| D8.2.4* | 1194 | One indexed compilation unit |
| D8.2.5* | 1200 | One typed pass manager; facts live on the AST |
| D8.2.6* | 1209 | Lowering is driven by demand |
| **§D8.3** | 1218 | *Lambda execution tiers* |
| D8.3.1* | 1220 | The AST interpreter is the baseline tier |
| D8.3.2* | 1228 | Only hit counters trigger promotion; a handoff is one way |
| D8.3.3* | 1238 | Promoted code enters through the boxed wrapper |
| D8.3.4* | 1246 | Background compilation publishes only at safe points |
| D8.3.5 | 1255 | Static import prebuild is a shared keyed-future service |
| D8.3.6 | 1265 | The REPL is one persistent T0 session |
| **§D8.4** | 1274 | *LambdaJS execution tiers* |
| D8.4.1* | 1276 | One runtime, separate semantic walkers |
| D8.4.2* | 1284 | AUTO starts in the AST; MVP-Lmd is the MIR engine |
| D8.4.3 | 1293 | Dynamic source always runs in the AST interpreter |
| D8.4.4 | 1301 | All tiers share one JavaScript semantic contract |
| D8.4.5* | 1312 | JS promotion has closed admission and keeps callable identity |
| D8.4.6 | 1322 | JavaScript modules use the shared module registry |
| **§D8.5** | 1331 | *Caches* |
| D8.5.1* | 1333 | The L1 script cache is in-process |
| D8.5.2* | 1345 | A disk cache stores a code image with a relocation journal |
| D8.5.3* | 1351 | Every cache entry is born verified |
| **§D8.6** | 1360 | *Emission testing* |
| D8.6.1 | 1362 | The emission ratchet has 0% slack |
| D8.6.2 | 1368 | One artifact contract |
| D8.6.3 | 1372 | Dynamic oracles verify liveness |
| D8.6.4* | 1377 | The unified-AST consolidation has hard exit ratchets |
| **§D9** | 1388 | *Build, Modules and Packages* |
| **§D9.1** | 1394 | *Build layering* |
| D9.1.1v4* | 1396 | Input, Radiant and Lambda have separate owners |
| D9.1.2v2* | 1406 | The input service owns resource acquisition |
| D9.1.3v2* | 1412 | Provider ownership beats include ownership |
| D9.1.4v2* | 1416 | The input project owns the Mark API |
| D9.1.5* | 1420 | GNU Make and Premake define the project build pipeline |
| **§D9.2** | 1427 | *Build profiles* |
| D9.2.1v2* | 1429 | Headless has three profiles |
| D9.2.2 | 1437 | `lambda-cli` is the runtime-only host |
| D9.2.3 | 1443 | `lambda-wasm` is the browser evaluation profile |
| **§D9.3** | 1454 | *Script packages* |
| D9.3.1 | 1456 | A package is the compilation unit |
| D9.3.2 | 1462 | Imports bind boxed entries; initialization is a transaction |
| D9.3.3 | 1467 | Packages are cached in-process and distributed as source |
| D9.3.4 | 1470 | `lambda.*` is the one root for shipped code |
| D9.3.5 | 1485 | One import resolver serves every caller |
| **§D9.4** | 1493 | *Jube modules* |
| D9.4.1 | 1495 | The core is closed; everything else is a module |
| D9.4.2 | 1501 | Modules are verified, negotiated and transactional |
| D9.4.3 | 1508 | There is one strict host-API tier |
| D9.4.4 | 1514 | No module links another |
| D9.4.5 | 1518 | Every module kind names its conformance gate |
| D9.4.6* | 1522 | The host exports only an allowlist |
| **§D9.5** | 1530 | *Jube data and async contracts* |
| D9.5.1 | 1532 | Items cross; host structs use virtual carriers |
| D9.5.2 | 1540 | Modules are shielded from the async substrate |
| D9.5.3* | 1548 | Guest hosting crosses no core types |
| D9.5.4 | 1557 | Declared interfaces are the only host-object protocol |
| D9.5.5* | 1564 | Three virtual carriers mirror the container taxonomy |
| D9.5.6v2* | 1574 | Lambda and Radiant meet through explicit contracts |
| **§D10** | 1581 | *Documents and Shipped Packages* |
| **§D10.1** | 1583 | *Document formats* |
| D10.1.1 | 1585 | `.mark` is the one Mark file extension |
| D10.1.2 | 1591 | An explicit format overrides the filename |
| D10.1.3 | 1595 | A domain document in Mark syntax has its own format |
| D10.1.4 | 1602 | XHTML uses XML syntax and gives the HTML element tree |
| D10.1.5 | 1611 | SVG always uses HTML5 SVG parsing |
| D10.1.6 | 1617 | SVG export serializes the current DOM |
| D10.1.7* | 1622 | An animation snapshot uses the virtual-timer frame |
| **§D10.2** | 1629 | *Document packages* |
| D10.2.1* | 1631 | The DOM behavior package owns the editing policy |
| D10.2.2* | 1641 | LaTeX is read by TeX expansion, never by TeX typesetting |
| D10.2.3* | 1655 | Document math keeps the layout policy of its notation |
| D10.2.4 | 1662 | Bundled fonts are shared resources |
| **§D11** | 1667 | *Security and Capabilities* |
| **§D11.1** | 1672 | *Trust and IO* |
| D11.1.1* | 1674 | There are three trust tiers |
| D11.1.2* | 1682 | One central IO API is the only IO door |
| **§D11.2** | 1688 | *Apps and scripts* |
| D11.2.1* | 1690 | Apps and scripts run inside an admitted root |
| D11.2.2* | 1699 | Create freely; overwrite only with approval |
| D11.2.3* | 1707 | The trusted host owns the grant settings |
| D11.2.4* | 1715 | Network access is denied by default; updates are a host service |
| D11.2.5* | 1724 | Uninstall keeps private app data |
| **§D11.3** | 1730 | *Media capabilities* |
| D11.3.1* | 1732 | Audible output depends on the document origin |
| D11.3.2* | 1744 | The microphone needs explicit acquisition and separate grants |
| DO1 | 1830 | Nullable-lane residue: boxed fallback for `any`, `number` and unions; |
| DO2 | 1834 | Scalar-home audits: result-bearing dispatch helpers, resume-slot reads, exception-peek rematerialization, the … |
| DO3 | 1837 | Boxing: do union parameters stay boxed? Does the return type take part in unboxed-call selection? What is the … |
| DO4 | 1839 | `MirValue`: its size (measure first), and whether the planned occurrence source replaces the actual-rep … |
| DO25 | 1842 | Node identity (D3.1.3, S5.1.4v2): where the identity lives, the universal ID form, which operations copy it, … |
| DO27 | 1845 | Instance type alteration (S2.1.5): can reconstruction reuse the old buffer, and how are extra fields carried? … |
| DO28 | 1847 | Tier carrier divergence for annotated module bindings: one carrier for each declared contract at the shared … |
| DO31 | 1850 | Can an object declaration also name an element tag, and how do the tags of a hierarchy relate? [D3.1.1] |
| DO32 | 1852 | Virtual PDF output: wider pure-producer demand, retained-child representation (SO48), remaining object … |
| DO5 | 1857 | Numeric GC fallback: bring the per-boundary inventory to zero, then delete the scalar heap |
| DO6 | 1860 | Side-root slot metadata (boxed Item versus raw pointer), needed only for a moving collector; |
| DO7 | 1863 | Stack maps (kept in view): the only route to zero rooting cost in the mutator; |
| DO8 | 1866 | COW counts: a saturating count waits for live-corpus counters |
| DO9 | 1868 | Memory Context stage 2, including MIR code-page bytes; |
| DO10 | 1870 | Stack-frame checks: concat without tail-payload rebase; |
| DO24 | 1873 | Can a compiler-generated wide temporary be live across a loop back edge under the D6.2.3 reclaim? The emitter … |
| DO11 | 1878 | Dual functions: promotion-aware numeric inference, guard-failure counters, lazy generation of cold boxed … |
| DO12 | 1881 | MIR cache: GC-root registry reset under L1, thunk identity under lazy generation, a growth bound for the name … |
| DO13 | 1883 | Emission testing: guest-transpiler scope, the MT6 coupling spike, a clean zero-root-elision fixture, and the … |
| DO14 | 1886 | Unified AST: the "views" variance tier (adopt only when a shared pass needs it) |
| DO29 | 1888 | Typed `var` rebinding: arbitrary places, async rebinding, and a scalar-lane binding passed to an untyped … |
| DO33 | 1890 | Interpreter tier breadth (D8.3.2): satellite budgets under MT7, cross-context promotion cells, loops outside … |
| DO16 | 1896 | Name identity: the dynamic-intern growth bound, the pooled-flag audit, GOT rebuild on L1 hits, mapped-backing … |
| DO17 | 1899 | Const pool: decimal sharing, datetime packing, cross-module literal dedup, mapped-leaf page retention, the … |
| DO18 | 1902 | Sys-func registry: the public mutation convention, the public result of `splice`, and the confirmed ArrayNum … |
| DO19 | 1904 | Jube: the globals allowlist audit, the WPT harness, the data-pack design, `dynamic_lookup` retirement, the … |
| DO20 | 1908 | Cross-isolate lifetime for shared graph Items (promote-on-share is recommended) |
| DO21 | 1910 | Static modules: Class F scheduling, the profile-B headless audit, the runtime-native IO inventory and Windows … |
| DO22 | 1914 | First-class `Type*` representation needs its own design record: the kind-discriminated layout, the canonical … |
| DO23 | 1917 | Shapes: cross-`Input` type sharing and shape serialization |
