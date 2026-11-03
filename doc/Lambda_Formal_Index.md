# Lambda Formal Spec Index

> **Generated** by `utils/formal_index.py` — do not edit. After editing either
> formal spec run `make formal-index`; `make check-formal-index` (part of
> `make lint`) fails while this file is stale. Not normative: the specs win.

One row per ruling (`S#`/`D#`), invariant (`SI#`/`DI#`) and open issue
(`SO#`/`DO#`), with the spec line where it starts. `*` marks a ruling that is
not or only partly implemented (see the spec's Appendix A). To read a ruling
in full — including lines too long for a viewer — run
`python3 utils/formal_index.py D7.3.6` (an ID or a section such as `S4.5`).

## Semantics — [`Lambda_Formal_Semantics.md`](Lambda_Formal_Semantics.md) v81.0.1 (2026-11-03)

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
| S2.3.1 | 260 | Maps store keys in source/insertion order |
| **§S2.4** | 265 | *Paths* |
| S2.4.1v2* | 267 | Path values use dotted steps |
| S2.4.2v5* | 276 | Every hierarchical reference is a typed root plus ordered operations; |
| S2.4.3v3* | 297 | Paths, names, symbols, and member expressions use this one reference scheme but retain distinct evaluation … |
| S2.4.4* | 317 | Each evaluation's immutable resolver deterministically maps logical `/` prefixes, namespaces, and provider … |
| S2.4.5v2* | 324 | Paths have three root forms: rooted `/.a.b`, relative `\.a.b`, and absolute `SchemeName...` |
| S2.4.6 | 335 | A wildcard step may sit anywhere in a path, and a force reads exactly the files that a spelled step enters |
| S2.4.7 | 360 | A query step on a path follows the wildcard rule: it never crosses the file boundary by itself |
| **§S2.5** | 383 | *Lists and blocks* |
| S2.5.1v2 | 385 | A list spreads; an array does not |
| S2.5.2v2 | 395 | A for-expression produces a list, whatever its clauses |
| S2.5.3v3* | 398 | A functional block `{…}` produces a list |
| S2.5.4v3 | 408 | Declarations produce no result, not a `null` result |
| S2.5.5v2 | 419 | A list has at least two items |
| S2.5.6 | 434 | A list is transient; where it lands decides |
| S2.5.7v4 | 445 | A list is built, never computed: functions and pipes return an array for any sequence input; operators keep … |
| S2.5.8 | 470 | Text is placed as one value and walked as a sequence |
| **§S2.6** | 481 | *Content* |
| S2.6.1v2 | 483 | Element content and script top level share one content model |
| S2.6.2 | 495 | Absent and empty items are dropped |
| S2.6.3 | 499 | Lists spread into content; arrays do not (S2.5.1v2) |
| S2.6.4 | 505 | Adjacent strings merge, adjacent binaries merge, nothing else does |
| S2.6.5 | 513 | Content stays normalized under mutation |
| S2.6.6v3* | 520 | An explicitly PDF-formatted file selects a byte-output destination |
| **§S3** | 552 | *Truthiness* |
| S3.1v2 | 554 | The falsy set is exactly **`null` · `false` · `""`** |
| S3.2v2 | 559 | Truth classification is decidable from the type tag alone |
| S3.3v3 | 564 | Consequences to teach: truthy-0 keeps `or` a safe coalescing operator (no `??` needed); |
| S3.4 | 573 | Logical evaluation preserves an encountered error |
| S3.5 | 580 | An `if` expression evaluates its condition once |
| **§S4** | 589 | *Numerics* |
| **§S4.1** | 603 | *The `int` type (v5: int53, total)* |
| S4.1.1 | 608 | The `int` domain is **{n ∈ ℤ : \|n\| ≤ 2⁵³−1} plus the three closure points `inf`/`-inf`/`nan`**, carried … |
| S4.1.2 | 614 | `int` arithmetic is **closed and total**: `+ - * div % neg abs`, bitwise `& \| ^`, shifts, and `**` never … |
| S4.1.3 | 619 | Aggregates use **mathematical-value semantics**: the result is the true sum if in band, `±inf` by the true … |
| S4.1.4 | 624 | `int ∥ i64`: poison has no `i64` home; |
| S4.1.5 | 628 | Machine ints are Go-aligned: runtime overflow wraps two's-complement; |
| **§S4.2** | 632 | *Poison* |
| S4.2.1 | 634 | `int` and `float` share **one poison identity**, spelled bare `inf` / `-inf` / `nan` |
| S4.2.2 | 638 | A value shared across domains **types as its narrowest**: `type(nan)` → `int`; |
| S4.2.3 | 641 | Same-signed infinities are **one value** across all poison-bearing domains (`inf == decimal.inf`); |
| S4.2.4 | 645 | Classification flows up: widening admits poison with no check (`nan` flows into a `float` or `integer` … |
| **§S4.3** | 650 | *Literals and ingestion* |
| S4.3.1 | 652 | Literal type is **lexical, never value-selected**: a numeric literal is `int` iff it has no decimal point and … |
| S4.3.2 | 655 | An unsuffixed int-form literal outside ±(2⁵³−1) is a **compile error** |
| S4.3.3 | 657 | The suffix names the type: `n` = `integer` always; |
| S4.3.4 | 660 | Data cannot be rejected |
| **§S4.4** | 664 | *Promotion lattice* |
| S4.4.1 | 666 | One subsumption principle: `T1 ⊑ T2` iff every T1 value embeds **exactly** into T2 |
| S4.4.2 | 671 | Meets are **type-directed, never magnitude-directed**: int×int→int; |
| S4.4.3 | 674 | Every **bounded** integer type (`i8`…`u32`, `int`, `i64`, `u64`) meets `float` in **`float`**; |
| S4.4.4 | 678 | Sized×sized selects the smallest containing machine lane and stays there for `+ - *`, bitwise, shifts (`i8 + … |
| **§S4.5** | 683 | *Division and modulo* |
| S4.5.1 | 685 | A **literal** zero divisor (`x div 0`, `x % 0`) is a compile error; |
| S4.5.2 | 688 | `/` is true division, domain-selected: `int / int → float`; |
| S4.5.3 | 691 | `div` and `%` **stay in their operand domain**: `int div int → int`; |
| S4.5.4 | 696 | `div` truncates toward zero; |
| S4.5.5 | 699 | Vectorized integer division is per-lane: the result stays an int-family array with `inf`/`nan` at offending … |
| **§S4.6** | 703 | *Decimal* |
| S4.6.1 | 705 | `decimal` is one source-level type with invisible storage tiers (decimal128 when it fits, unbounded above); |
| S4.6.2 | 708 | `+ - *` are exact and may grow storage; |
| **§S4.7** | 713 | *Float ↔ decimal* |
| S4.7.1 | 715 | A float denotes its shortest round-trip decimal |
| **§S4.8** | 722 | *Printing* |
| S4.8.1 | 724 | Floats print as their shortest round-trip decimal; |
| S4.8.2 | 728 | Finite `int` values print as integers at every magnitude |
| **§S4.9** | 733 | *Guest boundary* |
| S4.9.1 | 735 | Numeric FFI is **type-directed, never value-directed** |
| **§S5** | 744 | *Equality* |
| **§S5.1** | 746 | *Error-preserving deep value equality* |
| S5.1.1v2 | 748 | `==` is deep, structural, value-based equality |
| S5.1.2v2 | 754 | NaN is unequal; errors propagate |
| S5.1.3 | 759 | Structural recursion is depth-limited; |
| S5.1.4v3* | 762 | `==` is the only **value** equality and compares content alone |
| S5.1.5* | 774 | Container equality recursively compares content and short-circuits |
| **§S5.2** | 785 | *Numbers* |
| S5.2.1v2 | 787 | Numeric equality is exact mathematical-value equality across ranks: `1 == 1.0 == 1n == 1.0m == 1.00m`; |
| **§S5.3** | 793 | *Sequences* |
| S5.3.1 | 795 | `range`, `list`, and `array` are one sequence family: `(1 to 3) == [1,2,3]`, element-wise in order |
| **§S5.4** | 799 | *Maps, objects, elements* |
| S5.4.1 | 801 | Map equality is key-unordered (JSON/XML, the document standards); |
| S5.4.2v3* | 803 | A nominal value equals only a value of the **same nominal type**, whatever its structural kind; |
| S5.4.3 | 813 | Element equality = tag + namespace, attributes as an unordered map (XML InfoSet), children ordered (document … |
| **§S5.5** | 816 | *Functions and types* |
| S5.5.1v2* | 818 | Function equality is **intensional**: same definition site + deep-equal captures |
| S5.5.2 | 825 | Type equality is **representational** |
| **§S5.6** | 829 | *Dedup, grouping, hashing* |
| S5.6.1v2* | 831 | `unique`, set semantics, and grouping use `==`: nulls group together and each `nan` stands alone |
| S5.6.2 | 836 | `a == b ⟹ hash(a) == hash(b)` wherever values are hashed: maps hash in canonically sorted key order; |
| **§S6** | 842 | *Ordering and Sort* |
| **§S6.1** | 844 | *Two relations, by design* |
| S6.1.1 | 848 | The comparison operators `< <= > >=` answer magnitude questions only |
| S6.1.2v2 | 854 | An error operand to `< <= > >=` propagates unchanged, including against null or nan |
| **§S6.2** | 859 | *The Lambda total order* |
| S6.2.1 | 861 | `sort` and `order by` use the separate total order:* |
| S6.2.2v3 | 869 | The total order **totally refines `==`** |
| S6.2.3 | 877 | Sort is stable; |
| **§S7** | 882 | *Absence and Errors* |
| **§S7.1** | 886 | *Reads are total; writes are checked* |
| S7.1.1v3* | 888 | Every invalid member/index **read yields `null`** |
| S7.1.2 | 896 | Slices **clamp** to bounds and return an empty collection (or `""` for string results |
| S7.1.3v3* | 903 | Every invalid member/index **write fails with a language error**, subject to S7.4.1v2/S7.5 |
| **§S7.2** | 912 | *Indexing and `last`* |
| S7.2.1 | 914 | Indexing is 0-based; |
| S7.2.2 | 918 | Reaching from the end is the reserved word **`last`** = `len(container) − 1` of the innermost enclosing … |
| S7.2.3 | 923 | The two-homes rule |
| S7.2.4 | 927 | Functions never take the `last` keyword and never accept signed counts |
| **§S7.3** | 933 | *Aggregation* |
| S7.3.1 | 935 | A `null` input makes the aggregate `null` |
| S7.3.2 | 939 | Monoid identities: `sum([])` = 0, `prod([])` = 1; |
| **§S7.4** | 943 | *Ordinary errors and system faults* |
| S7.4.1v2 | 948 | Ordinary errors are values with uniform acknowledgement |
| S7.4.2v4 | 954 | `T^` is shorthand for `T \| error` in every type position, including function returns |
| S7.4.3 | 967 | System faults |
| S7.4.4 | 971 | An error value is a first-class value carrying `code`, `message`, and source location; |
| S7.4.5v2* | 978 | System `fn` failures and effectful-reader failures obey the same ordinary-error discipline |
| S7.4.6v3* | 985 | `raise v` completes the enclosing callable early with an error |
| **§S7.5** | 993 | *Acknowledgment* |
| S7.5.1v6 | 995 | Acknowledgement follows possible ordinary-error outcomes |
| S7.5.2v4 | 1007 | Binding acknowledgement follows type admission |
| S7.5.3v3 | 1027 | `a or b` defaults only `null`, `false`, and `""`; |
| S7.5.4 | 1034 | Acknowledgement composes through error-preserving operations |
| S7.5.5 | 1042 | An unacknowledged fallible result may not be silently overwritten in a procedural sequence |
| S7.5.6 | 1051 | Explicit error classification acknowledges the tested outcome |
| S7.5.7 | 1059 | No `empty` keyword or predefined error-or-null shorthand |
| **§S7.6** | 1067 | *Discharge: handlers and postfix `^`* |
| S7.6.1v9* | 1069 | `e ^ { … ^ … }` is the unfiltered postfix error handler and is origin-independent |
| S7.6.2v5 | 1099 | The unfiltered and filtered handlers are left-associative postfix-primary forms `primary ^ { body }` and … |
| S7.6.3v3* | 1112 | Postfix `e^` propagates |
| S7.6.4 | 1123 | `?` is not propagation |
| S7.6.5v4* | 1126 | Retired split bindings `let a^err = e` and prefix error tests `^err` / `if (^err)` remain unavailable; |
| S7.6.6v6* | 1134 | Division of labor: `or` defaults ordinary falsy values and preserves errors; |
| S7.6.7v5* | 1142 | Procedure handlers produce values under the same rules as function handlers |
| S7.6.8 | 1151 | `e or^ { h }` combines ordinary-error recovery and falsy defaulting |
| S7.6.9 | 1161 | `e ^ErrorType { h }` handles matching ordinary errors |
| **§S7.7** | 1176 | *Validation outcomes and boundary behavior* |
| S7.7.1v3* | 1181 | Binding validation is an expression-level operation |
| S7.7.2v2 | 1189 | A deferred initializer binds its validation outcome |
| S7.7.3* | 1199 | A declared parameter guards at the **call site**: on failure the function is not entered and **the call … |
| S7.7.4v2 | 1203 | Whole-variable reassignment uses the initializer admission rules unchanged |
| S7.7.5v3 | 1214 | Initializer or whole-variable reassignment failure inside a loop does not skip its body or abandon the loop |
| S7.7.6v2* | 1221 | Element/field stores keep their separate failure contract |
| S7.7.7v4 | 1230 | Validation errors can be handled as ordinary values |
| **§S7.8** | 1238 | *Containers and errors* |
| S7.8.1* | 1240 | Acceptance is read from the **destination contract**, never from syntactic position |
| S7.8.2 | 1250 | Both spellings are visible in source: `[ f(x) ^ { 0 } for x in xs ]` → `int[]`, lane preserved; |
| **§S7.9** | 1257 | *What an error participates in* |
| S7.9.1 | 1262 | Type family participates |
| S7.9.2v3 | 1264 | Logical evaluation and comparisons propagate |
| S7.9.3 | 1271 | Value family propagates |
| S7.9.4 | 1277 | Containment is not participation |
| **§S7.10** | 1282 | *The sys-func return contract* |
| S7.10.1v3 | 1287 | Result shape is fixed by the contract, never by cardinality, for array, list, and text inputs: zero-to-many → … |
| S7.10.2 | 1293 | Admission is an explicit per-function contract: an **admissive** case has a meaningful no-answer reading and … |
| S7.10.3 | 1300 | No in-band sentinels, ever |
| S7.10.4 | 1306 | Error operands are rejected at the call boundary (parameters are `any \ error`), keeping "error operand" … |
| S7.10.5v3* | 1308 | Vectorized sys funcs are sequence-in, array-out (S2.5.7v4): array or list in → array out, one lane never … |
| S7.10.6 | 1314 | A mutator family picks one public convention (updated owner, or unit) and holds it; |
| **§S7.11** | 1318 | *System faults and recovery* |
| S7.11.1v2 | 1320 | Fault reasons are a closed, typed native set: stack overflow, side-stack exhaustion, out-of-memory, and … |
| S7.11.2v2 | 1326 | Faults pass transparently through `fn` frames; |
| S7.11.3 | 1333 | Transaction barriers (module init, hosted-guest entry) take priority over inner handlers: no handler may … |
| S7.11.4 | 1338 | Production containment is fail-stop: recoverable faults use recovery frames; |
| **§S8** | 1346 | *Membership and Iteration* |
| **§S8.1** | 1348 | *The `in`/`at`/`of` axis* |
| S8.1.1v2 | 1350 | Three axes, one key class each |
| S8.1.2v3 | 1365 | On elements and objects, `in` ranges over attribute values then children; |
| S8.1.3v2 | 1371 | Axis and arity are independent |
| **§S8.2** | 1390 | *The key space* |
| S8.2.1v4* | 1392 | Every container has a fixed key domain |
| S8.2.2v6* | 1415 | One name space, keyed by namespace and normalized spelling |
| S8.2.3* | 1450 | Methods are members of the type, never of the value |
| S8.2.4v3* | 1461 | Two subscript worlds: a type key answers as XPath, a positional selection as NumPy |
| S8.2.5 | 1463 | The item step `.*` and its closure |
| S8.2.6 | 1480 | A run is stepped set-at-a-time by every key kind except positional |
| **§S8.3** | 1502 | *`len`* |
| S8.3.1v3 | 1504 | The law: **`len(x)` is the number of iterations `for (i in x)` performs.** Consequences, not separate rules: … |
| S8.3.2* | 1517 | Lazy sequences: a forceable stream's `len` forces and returns the actual size; |
| S8.3.3v3 | 1522 | Two lengths: `len` measures content; |
| **§S8.4** | 1533 | *Projections* |
| S8.4.1v2 | 1535 | `keys(c)` ≡ `[for (k, v in c) k]`; |
| **§S9** | 1544 | *Mutability: Mutable Value Semantics* |
| **§S9.1** | 1546 | *The model* |
| S9.1.1 | 1553 | `let` is final: nothing reachable through a `let` binding ever changes |
| S9.1.2 | 1558 | Binding, assignment, and construction copy, observably, for every container kind |
| S9.1.3v2* | 1561 | `var` parameters (`pn f(var a: T)`) are explicit inout borrows; |
| S9.1.4v2* | 1569 | Capture permissions follow the callable's colour |
| S9.1.5v2 | 1583 | No reference cells; |
| S9.1.6* | 1589 | Member/index assignment may update or add only a member admitted by the container's existing key domain; |
| S9.1.7v2* | 1598 | No module-level mutable binding |
| **§S9.2** | 1610 | *Covariance, borrows, and views* |
| S9.2.1 | 1612 | *Covariance where values copy, invariance where they're borrowed*: `int[] <: any[]` holds for `is`, reads, … |
| S9.2.2* | 1616 | Read views are first-class values with snapshot semantics (a zero-copy slice observably behaves as a copy … |
| S9.2.3* | 1621 | Iterating a `var` container walks the entry-time value: the loop share-marks at the head, and the first … |
| S9.2.4v2* | 1625 | A **view-state** `var` may not be passed as a `var` argument (no call-site check can see the callee's … |
| **§S9.3** | 1633 | *Construction captures values* |
| S9.3.1* | 1635 | Placing a value into a container captures it **by value** at every constructor and insertion point |
| S9.3.2 | 1641 | Construction preserves each element's type and precision |
| **§S10** | 1659 | *Operators* |
| **§S10.1** | 1661 | *Union, pipe, and filter* |
| S10.1.1v5* | 1663 | `\|`, `&` and `!` are the type operators |
| S10.1.2v4 | 1664 | The pipe is dual-mode on a parse-time syntactic test: a body with a **free `~`** is a mapping pipe (binds `~` … |
| S10.1.3 | 1677 | `~` is lexically scoped to the RHS of its pipe; |
| S10.1.4 | 1681 | File write/append syntax is deferred; |
| S10.1.5v3 | 1683 | `that` is the single-value proviso |
| S10.1.6 | 1699 | `\|:` is the filter stage of the pipe family |
| S10.1.7v4* | 1722 | A body about one subject may leave `~` implicit; the pipe family always spells it |
| **§S10.2** | 1758 | *Vectorization* |
| S10.2.1 | 1760 | Arithmetic `+ - * /` is vectorized (element-wise with broadcasting) |
| S10.2.2v2* | 1765 | Bare comparisons `< <= > >=` are **scalar-only**, never element-wise (a mask is a container, containers are … |
| S10.2.3 | 1773 | Mask consumption is explicit and non-magical: `sum(mask)` counts true lanes; |
| S10.2.4 | 1777 | Element-wise arithmetic promotes exactly as scalar arithmetic does: each result element has the type the … |
| **§S10.3** | 1792 | *Keyword operators* |
| S10.3.1v3 | 1794 | `and or not is in to div that at eq ne lt le gt ge` |
| **§S10.4** | 1803 | *Parent navigation* |
| S10.4.1* | 1805 | Postfix `.~~` is the parent-navigation step at the ordinary member/index precedence tier; |
| S10.4.2* | 1811 | Bare `~~` is exactly `~.~~`: it is valid exactly where `~` is bound, selects the innermost current-value … |
| S10.4.3v2* | 1817 | Contextual parent navigation is occurrence-based and carries lineage in the evaluation context as a … |
| **§S10.5** | 1824 | *Root navigation* |
| S10.5.1* | 1826 | `/` is the one root-selection operation |
| S10.5.2* | 1831 | For a path, `./` selects its logical or provider/authority anchor |
| S10.5.3v2* | 1836 | Dynamic root navigation is occurrence-based |
| **§S10.6** | 1841 | *Concatenation* |
| S10.6.1 | 1843 | `++` concatenates sequences and appends scalars |
| **§S11** | 1859 | *Types and Patterns* |
| **§S11.1** | 1861 | *Types compose like values* |
| S11.1.1v3* | 1863 | A bracket type is a structural pattern whose positions mix values, types, and occurrence runs freely: `[1, … |
| S11.1.2v4 | 1874 | String and symbol structural patterns share the delimiter `\( ... )`; quotes and named patterns determine one … |
| S11.1.3 | 1909 | A range type `X to Y` denotes inclusive membership in the consecutive values between its bounds |
| S11.1.4v2 | 1917 | The binary type relation is spelled `<:` |
| S11.1.5v2* | 1927 | Three function types: `fn`, `pn`, and their union `function` |
| S11.1.6v3 | 1945 | Two type families, split by concept: a run and an array |
| S11.1.7* | 1972 | `none` is the empty type |
| **§S11.2** | 1974 | *Match* |
| S11.2.1 | 1976 | `match` is a **type match**: arms are type expressions tried in order, first match wins, no fall-through; |
| S11.2.2 | 1983 | Poison is unequal, not untypeable: `case float:` catches nan, `case error:` catches errors; |
| S11.2.3* | 1988 | Exhaustiveness is compiler-checked: unions need every constituent, `bool` both arms, `T?` needs `T` and … |
| **§S11.3** | 1993 | *Structural `is`, nominal objects* |
| S11.3.1v2 | 1995 | `is` is structural for maps/arrays/elements (extra fields permitted; |
| **§S11.4** | 2004 | *Declared types are contracts* |
| S11.4.1v5* | 2008 | An annotation is a strict **admission contract**, not a hint |
| S11.4.2v3* | 2026 | Signatures spell both failure dimensions: plain successful `T` excludes null and error; |
| S11.4.3 | 2037 | `any` is the top type and includes error; |
| S11.4.4 | 2043 | *When the user is explicit, we check explicitly*: an explicitly declared error possibility cannot enter a … |
| S11.4.5* | 2048 | Deferred numeric admission is **value-aware**: an exactly-embedding value passes and is re-represented … |
| S11.4.6v4* | 2053 | User-defined types are enforced by the validator: deep, on first crossing, yielding an admitted value or a … |
| S11.4.7v3* | 2065 | Validation and discharge follow §S7.7–S7.8: deferred initializer and whole-variable reassignment checks … |
| S11.4.8v2 | 2069 | A function signature may establish a **type binder** with an explicit `T: type` parameter, a … |
| S11.4.9 | 2086 | A system-function registry row may declare a **type relation** from one call argument to its success result |
| S11.4.10* | 2099 | Verified at the crossing, valid while unchanged |
| S11.4.11 | 2111 | A `that` predicate is an `fn` body over the scope it is written in |
| S11.4.12* | 2124 | A type name binds for its whole scope and never reads as `any` |
| **§S11.5** | 2128 | *Physical binary schemas* |
| S11.5.1v2* | 2130 | `type A : binary & {…} { field: T & {…}, … }` declares a physical binary layout |
| S11.5.2* | 2141 | `binary_decode(bytes, A)` returns a lazy, read-only element backed by its byte source and schema |
| S11.5.3v3* | 2147 | A native schema names a host binding |
| S11.5.4 | 2154 | A native pointer field `T[n] & {pointer: 'native'}` exposes a lazy read-only array of contiguous fixed-width … |
| **§S12** | 2170 | *Functions, Effects, Resources* |
| **§S12.1** | 2172 | *The one-bit effect system* |
| S12.1.1v3 | 2174 | `fn`/`pn` is a declared, compiler-checked, one-bit effect system: `fn` is pure and deterministic under any … |
| S12.1.2v3* | 2186 | Local imperative control is admitted in `fn` and `pn` |
| S12.1.3v2* | 2205 | Reactive templates are the doctrine applied: template body = pure `fn` transformation; |
| S12.1.4v4* | 2209 | Effect polymorphism: the `function` declaration |
| S12.1.5v2 | 2238 | Presentation authority by state tier |
| **§S12.2** | 2252 | *Assignment* |
| S12.2.1v3* | 2254 | `let` bindings and parameters are immutable; |
| S12.2.2 | 2263 | Element mutation is defined on both faces: `elem.attr = v` as map-field assignment; |
| **§S12.3** | 2267 | *The call contract* |
| S12.3.1 | 2269 | A function has at most **16 source-language argument slots** (a rest collector consumes one); |
| S12.3.2 | 2273 | Two dynamic-call restrictions are deliberate: a dynamic call with named arguments is rejected, and a dynamic … |
| S12.3.3v2* | 2276 | Member access is resolution, not membership |
| S12.3.4v2* | 2298 | `call(f, args)` is the dynamic-application form |
| S12.3.5v2 | 2311 | Spread splices into containers, never into an argument list; the spelling follows the container's shape |
| S12.3.6 | 2326 | No arity overloading for user definitions |
| S12.3.7* | 2335 | User definitions shadow system functions — user-first, module-lexical, warned |
| **§S12.4** | 2354 | *Resources* |
| S12.4.1v3* | 2359 | `open()` is resource acquisition and is `pn`-only |
| S12.4.2* | 2370 | A resource auto-closes at the end of its enclosing **block**; |
| S12.4.3* | 2377 | No `defer`, `with`, or `finally` keyword: auto-close is the only user-facing cleanup mechanism; |
| **§S13** | 2385 | *Concurrency* |
| **§S13.1** | 2390 | *Tasks and workers* |
| S13.1.1v3 | 2392 | `start` is a builtin `pn`, not a keyword |
| S13.1.2v2 | 2399 | Calls are **colorless**: `f(x)` synchronously yields the value and may suspend invisibly (`f(x)` ≡ … |
| S13.1.3v2* | 2404 | Two tiers, one handle vocabulary: tasks (`start(f, [x])`, shared context) and isolated workers (`start(f, … |
| S13.1.4 | 2410 | The capture rule |
| S13.1.5 | 2414 | Failures are values; faults are not |
| **§S13.2** | 2420 | *Messaging* |
| S13.2.1 | 2422 | Handle = address; |
| S13.2.2 | 2427 | `send(h, msg)` never blocks and returns `ok^`: a full mailbox is the error value `'mailbox_full'` |
| S13.2.3 | 2431 | Ordering: per-sender FIFO; |
| **§S13.3** | 2436 | *Scope and cancellation* |
| S13.3.1v2 | 2438 | A started handle is a scoped resource owned by the nearest lexical block: normal exit **joins**; |
| S13.3.2 | 2446 | Cancellation is an error value at park points (`'cancelled'`), unwinding by ordinary `^` propagation with … |
| **§S13.4** | 2452 | *Determinism* |
| S13.4.1* | 2454 | Builtin numeric reductions (`sum`, `avg`, `prod`, `variance`, dot, `min`/`max` join) are … |
| S13.4.2* | 2460 | In stream pipelines, `fn` stages auto-parallelize and are **ordered by default** (a re-sequencing buffer … |
| **§S14** | 2466 | *Data Processing* |
| **§S14.1** | 2468 | *For-clause grouping and joins* |
| S14.1.1v2 | 2470 | `group by KEY [as NAME], … into g` binds, for each group, **every key under its name** and **`g` to the … |
| S14.1.2v2 | 2477 | Key equality is `==` with numeric-tower coherence (`1` and `1.0` group together); |
| S14.1.3 | 2490 | Join `on` is restricted to conjunctions of equality tests (non-equi conditions are a compile error pointing … |
| **§S14.2** | 2496 | *Verbs and windows** |
| S14.2.1* | 2498 | The verb surface is generic over row-oriented data |
| S14.2.2* | 2502 | Column references in verb arguments are `~.field` (the pipe current-item reference extended into verb scope; |
| **§S14.3** | 2507 | *Streams and laziness** |
| S14.3.1v3* | 2509 | `input()` is eager, `stream()` lazy |
| S14.3.2* | 2523 | Two stream kinds: value-backed streams are true values (re-forcible, usable in `fn`); |
| S14.3.3* | 2527 | Handles are stream sources/sinks (`stream(h)`, `send_to(h)`); |
| **§S15** | 2534 | *Metaprogramming* |
| S15.1 | 2536 | Lambda is homoiconic through elements: the canonical AST is an element tree in the ambient **`lm.` … |
| S15.2 | 2541 | Element literals are inverted quasiquotation: expression children evaluate (splice); |
| S15.3* | 2545 | `input(f, 'lambda')` parses Lambda source into the `lm.` AST; |
| S15.4 | 2551 | `name(item)` is the shadow-proof accessor for intrinsic names (element tag, function name, type name); |
| **§S16** | 2558 | *Surface Syntax* |
| **§S16.1** | 2566 | *Whitespace and separation* |
| S16.1.1* | 2568 | Line breaks carry no meaning |
| S16.1.2v2* | 2573 | `;` is a **strict separator** between statements |
| S16.1.3v2* | 2579 | Adjacent statements need **no separator** when the second begins with a token that cannot continue the first, … |
| **§S16.2** | 2590 | *Line-start classification* |
| S16.2.1* | 2592 | An **incomplete** expression continues across a line break unconditionally: a trailing operator, an unclosed … |
| S16.2.2v3* | 2595 | After a **complete** expression, a line-start token that can only continue an expression does continue it: … |
| S16.2.3v3* | 2601 | After an **open-tail** statement (S16.1.3v2), a line-start **dual-role** token |
| S16.2.4v3* | 2615 | One carve-out: `.` followed by a step other than a digit |
| S16.2.5* | 2623 | `return` followed by a line break and a start token returns that value: `return` ⏎ `42` is `return 42` |
| S16.2.6v2* | 2628 | Handler bodies open on the same line as their introducer: `e ^ { h }`, `e ^ErrorType { h }`, `e or^ { h }` |
| **§S16.3** | 2635 | *Juxtaposition* |
| S16.3.1* | 2637 | Juxtaposition **sequences, never combines.** Adjacent expressions are separate statements or content items; |
| **§S16.4** | 2645 | *Braces* |
| S16.4.1v4* | 2647 | Interior decides, wherever braces are an expression |
| S16.4.2v2 | 2662 | Empty `{}` resolves by context, and only where a tie exists |
| S16.4.3v4* | 2673 | Declaration braces are structural and never read as maps |
| **§S16.5** | 2701 | *Element scope* |
| S16.5.1v2* | 2703 | In an exposed element attribute value or bare content expression, `< > <= >=` **are not operators**: `>` … |
| **§S16.6** | 2717 | *Control forms* |
| S16.6.1* | 2719 | `if`, `for`, and `while` each have **one node with two spellings**: parenthesized head with any-expression … |
| S16.6.2* | 2724 | `(` immediately after `if` or `while` **commits** to the parenthesized spelling |
| S16.6.3* | 2729 | `else` is **optional** in both spellings |
| S16.6.4* | 2732 | `match` keeps its single braced form: its braces delimit an arm list, not a body, so no parenthesized … |
| S16.6.5v2* | 2735 | Control capability and call effects are separate |
| S16.6.6* | 2743 | Control statements require braces |
| S16.6.7v3* | 2752 | A procedure's body is always the braced statement block, and the arrow is the one anonymous function |
| S16.6.8v3* | 2769 | An imperative control block is a statement, not a general expression |
| S16.6.9v2* | 2778 | Branch homogeneity |
| **§S16.7** | 2789 | *Script top level* |
| S16.7.1 | 2791 | A script's top level is element content, not a list |
| S16.7.2v2 | 2799 | Nulls and empty strings are dropped (S2.6.2) |
| S16.7.3v2 | 2804 | Lists spread and adjacent strings or binaries merge (S2.6.3, S2.6.4) |
| S16.7.4 | 2807 | A REPL entry echoes its value unless it is declarations only |
| S16.7.5 | 2814 | A session top level rebinds nothing |
| S16.7.6 | 2819 | Two session kinds |
| **§S16.8** | 2828 | *Lexical forms* |
| S16.8.1 | 2830 | `not` is the one logical negation |
| S16.8.2 | 2834 | `not` binds loose |
| S16.8.3 | 2837 | Numeric spelling |
| S16.8.4* | 2841 | No implicit adjacent-literal concatenation |
| S16.8.5 | 2846 | Unary `+` is kept |
| S16.8.6v4 | 2849 | `*` is spread; `*` and `...` are two wildcard families, not one |
| S16.8.7 | 2861 | A single-quoted literal is a symbol, not a string |
| S16.8.8* | 2865 | The backtick syntax space is reserved and must not be spent otherwise |
| S16.8.9* | 2869 | Computed keys: `[expr]: val` |
| **§S16.9** | 2881 | *Declarations, elements, paths* |
| S16.9.1 | 2883 | `pub` is a uniform prefix modifier |
| S16.9.2* | 2886 | The **`apply;` fused token is retired**: bare `apply` is the keyword statement, disambiguated from … |
| S16.9.3 | 2889 | `;` has exactly one role language-wide: statement separation |
| S16.9.4 | 2900 | The relative path is spelled `\.a.b` |
| S16.9.5 | 2906 | `a?: T` marks an optional field |
| S16.9.6 | 2911 | `.` is the only import separator |
| S16.9.7 | 2915 | A rest parameter `...` closes a parameter list |
| S16.9.8 | 2920 | An import path's first token picks one of three roots |
| **§S16.10** | 2937 | *Keywords as names* |
| S16.10.1v3* | 2939 | Keywords never name bindings — where they could capture |
| S16.10.2* | 2971 | Data names admit keywords |
| S16.10.3* | 2988 | Member steps admit keywords |
| **§S17** | 2996 | *System Library* |
| **§S17.1** | 3001 | *String splitting* |
| S17.1.1 | 3003 | `split` follows ECMAScript `String.prototype.split` |
| **§S17.2** | 3022 | *The system-function namespace* |
| S17.2.1 | 3024 | System functions live at `lambda.sys.*`, and the prelude imports them unqualified |
| S17.2.2 | 3030 | `lambda.sys.f` is the escape from a shadow |
| **§S17.3** | 3040 | *Process information* |
| S17.3.1 | 3042 | `sys.proc.self.argv` is the sole argument-vector path |
| **§S17.4** | 3048 | *String search positions* |
| S17.4.1 | 3050 | A text search result uses its source's code-point index domain |
| **§S17.5** | 3056 | *Sized integer conversions* |
| S17.5.1 | 3058 | Sized integer conversions use the type names as callable constructors |
| **§S17.6** | 3064 | *String replacement* |
| S17.6.1 | 3066 | `replace` steps through matches as ECMAScript `String.prototype.replaceAll` does, and inserts its replacement … |
| **§S17.7** | 3079 | *Letter case* |
| S17.7.1 | 3081 | Case-insensitive matching folds by Unicode simple case folding |
| S17.7.2 | 3093 | `lower` and `upper` map by Unicode full case mapping |
| **§S17.8** | 3101 | *Option maps* |
| S17.8.1* | 3103 | An option name a system function does not define is a compile-time error where the compiler can see it, and a … |
| SO1 | 3245 | Sized-lane `div`/`%`: [Number_Model §3.3.2](../vibe/Lambda_Semantics_Number_Model.md) says sized×sized `div` … |
| SO2 | 3246 | Int v5 §5 details: poison-algebra table ratification; |
| SO3 | 3247 | The `int?` fourth lane value (`INT_LANE_NULL`) is undocumented in the Int_Type sentinel table; |
| SO4 | 3248 | Bitwise semantics were ruled (S4.1.2), but the interaction with the retired sparse band in old goldens needs … |
| SO50 | 3251 | Error-preserving comparison follow-ups: canonical field traversal for key-unordered maps, selection among … |
| SO5 | 3257 | TE-17 transitivity: does discharging `(int \| error)[]` re-narrow in place, or only by copy? Copy is the safe … |
| SO6 | 3258 | Lazy/streaming `for` bodies vs typed-lane destinations (boxed-until-proven presumed, undecided); |
| SO7 | 3259 | TE-5 R5 sticky `any`; |
| SO8 | 3260 | Should `is` become value-aware? Deliberately undecided (S11.3.1v2 records the intentional asymmetry) |
| SO9 | 3261 | A surface spelling for `any \ error` (the `!` exclusion operator route is broken |
| SO10 | 3262 | A deep "does this data contain an error anywhere?" check (`valid(item)`-shaped) |
| SO13 | 3265 | COW granularity on large documents: node representation for spine-copying, refcount discipline for … |
| SO14 | 3266 | Nested-mutation ergonomics (`t.nodes[i].value`): path-shaped `var` borrows, `_modify`-style accessors, or … |
| SO15 | 3267 | Exclusivity granularity endpoint (whole-base vs blessed splitters vs dynamic bookkeeping) |
| SO16 | 3268 | Close-error routing (double fault): proposed |
| SO17 | 3269 | Resource-carrying-type containment rules (when a wrapping value is itself resource-typed) |
| SO18 | 3270 | Snapshot iteration (C4.2d) |
| SO19 | 3271 | Root and upward-parent navigation are resolved by S10.4.3v2, S10.5.3v2 / PTH10, PTH29: lineage lives in a … |
| SO39 | 3275 | Node identity (S5.1.4v2): which operations preserve an identity (a COW detach and an in-place `var` write are … |
| SO42 | 3282 | Instance type alteration (S2.1.5): the surface spelling, which declared fields must be satisfiable from the … |
| SO43 | 3286 | Type alteration |
| SO41 | 3289 | Cross-reference form for document graphs under S9.1.5v2: an identity or key stored as data and resolved … |
| SO20 | 3295 | O-D: cross-isolate lifetime for shared graph Items (promote-on-share recommended) |
| SO21 | 3296 | `select` surface syntax; |
| SO22 | 3297 | Deferred opt-ins: blocking send, true selective receive, `unordered` streams, CPU-bound cancellation … |
| SO23 | 3300 | PD4 join column-collision suffixes; |
| SO24 | 3301 | PD12 sub-items: `on error` resume semantics (abort vs skip-record), handler scoping over multiple forced … |
| SO25 | 3302 | Deferred group-by vocabulary: `having`-style filter, post-group `let`, extended aggregates, … |
| SO26 | 3305 | RF6 mutator convention: updated-owner vs unit; |
| SO27 | 3306 | Whether debug logging inside `fn` is a permitted non-observable effect |
| SO44 | 3307 | Binder depth on function values: whether a binder (S11.4.8v2) over a function value selects its full … |
| SO29 | 3312 | File write/append syntax (C6a: `into`/`onto` candidates); |
| SO31 | 3313 | The `<file>` element shape (name/size/mime, content as child) |
| SO32 | 3314 | Match extensions: pipe-context shorthand, string-pattern capture binding in arms, range patterns |
| SO33 | 3315 | A10 residue: the aspirational generics text, `as` assertion semantics, and open-vs-closed map matching in … |
| SO34 | 3316 | `emit()` vs `send()` |
| SO47 | 3317 | Whether an empty literal needle matches at every code-point boundary |
| SO51 | 3319 | Local imperative control in comprehensions: mutation of a containing `fn` local across iterations, … |
| SO35 | 3326 | A dedicated formal syntax document: S16 parks the surface-syntax rulings here because syntax and semantics … |
| SO38 | 3327 | Whether `\|:` over a **map** should keep the surviving keys (yielding a map) rather than dropping them … |
| SO36 | 3333 | Whether a `pn` call may appear nested inside an expression (`(pn_func(), 123)`, `if (exists(path)) …`), or … |
| SO45 | 3342 | Whether a function type takes a suffix directly: `fn?`, `fn (x: int)?`, `fn ()[]` |
| SO46 | 3352 | How an element pattern spells *content must be empty* |
| SO48 | 3353 | PDF file content: remaining object kinds, decimal encoding, virtual source containers, direct array/scalar … |

## Design — [`Lambda_Formal_Design.md`](Lambda_Formal_Design.md) v60.0.1 (2026-11-03)

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
| D2.3.3 | 266 | Boxing is representation only (semantics S1.6); |
| **§D2.4** | 270 | *Value representation discipline* |
| D2.4.1* | 274 | Four facts, four authorities: semantic contract → AST `Type*`; |
| D2.4.2* | 281 | `transpile_expr()` returns a `MirValue` carrying the full `Type*` contract; |
| D2.4.3* | 286 | Representation conversion ≠ semantic coercion |
| **§D2.5** | 292 | *The nullable native lane* |
| D2.5.1* | 294 | `N(T?) = N(T) \| NULL_LANE(T)`: optionals keep a native carrier everywhere a representation is chosen |
| D2.5.2v3 | 301 | Sentinels: `INT_LANE_NULL` (numerically the `ItemNull` word |
| D2.5.3 | 318 | `a[i]` with an unproven index infers `T?` |
| **§D2.6** | 323 | *Containers and array storage* |
| D2.6.1v3 | 325 | Three physical array forms: boxed Array (Items), native Array (uniform descriptor-selected native slot per … |
| D2.6.2 | 334 | ArrayNum is strictly non-null |
| D2.6.3 | 341 | `ELEM_INT` element storage is the i64 lane (finite values or poison sentinels), mapped to IEEE at print/box … |
| D2.6.4v3 | 344 | A container that retains a raw wide-scalar Item points **only into that container's own buffer** (tail … |
| D2.6.5v4 | 350 | The append discipline follows the destination: content, sequence, or slot |
| D2.6.6v3* | 379 | One container hierarchy — map → array → element — and nominal is a descriptor property, not a kind |
| D2.6.7* | 406 | A bound method is an ordinary D6.2.1 function value whose closure environment is the receiver, captured by … |
| D2.6.8* | 412 | Node identity is data the container carries, never its address |
| D2.6.9v3* | 419 | A JavaScript object is a nominal Lambda `Map` — and therefore a Lambda object |
| D2.6.10* | 439 | Sealing at the representation level |
| D2.6.11* | 446 | The `is_nominal` bit packs into the existing header |
| D2.6.12v3* | 458 | Virtual output lists carry their builder |
| **§D2.7** | 477 | *The scalar-GC invariant* |
| D2.7.1 | 479 | No standalone scalar cell in the GC heap |
| D2.7.2v2* | 485 | Public entry wrappers speak the **companion-lane pair** (D5.2.1v5); |
| **§D2.8** | 495 | *The error-free lane invariant* |
| D2.8.1 | 500 | No native lane slot ever holds an error |
| D2.8.2* | 515 | No unboxed operator ever receives an error operand |
| D2.8.3v3* | 521 | Lane entry is gated on static proof, never on runtime rescue |
| **§D2.9** | 539 | *Mark file conventions* |
| D2.9.1 | 541 | `.mark` is the sole canonical file extension for Mark Notation |
| D2.9.2 | 550 | An explicit `mark` format overrides the filename |
| D2.9.3 | 556 | A domain document written in Mark takes its own format name and extension; it is not a Mark alias |
| **§D2.10** | 565 | *XHTML input* |
| D2.10.1v2 | 567 | `xhtml` uses XML syntax and emits the HTML element tree |
| **§D2.11** | 588 | *SVG admission, DOM export and frame snapshots* |
| D2.11.1 | 590 | SVG embedded in `text/html` follows browser-standard HTML parsing |
| D2.11.2 | 596 | External SVG reuses that same HTML5 SVG parsing path and result model |
| D2.11.3 | 605 | SVG source export serializes the current SVG DOM, including edits |
| D2.11.4* | 610 | Animation snapshots use the frame selected by the virtual timer |
| **§D3** | 618 | *Type and Shape* |
| **§D3.1** | 624 | *First-class type values* |
| D3.1.1v5* | 626 | A type value is a `Type*` graph node under one compact TypeId (`LMD_TYPE_TYPE`), discriminated by `Type.kind` … |
| D3.1.2 | 639 | Types compose as values (`\|` union, `?`, `[]`, constraints) and compare **representationally** |
| D3.1.3 | 644 | Reflection is by operators and functions, not properties: `type(x)` yields the type value, `name(T)` its name; |
| D3.1.4v2 | 648 | A type-binder site stores its written bound, name, and stable signature-local slot; |
| **§D3.2** | 661 | *The subtype foundation* |
| D3.2.1 | 663 | One subtype model, three distinct operations |
| D3.2.2v3* | 669 | The **validator is the runtime enforcer** for user-defined types: deep, on first crossing, producing a rich … |
| D3.2.3v3* | 678 | Declared and effective types are **separate recorded facts** (declaration on the binding node, inference … |
| D3.2.4v5* | 687 | A crossing into a named map contract is a reification, not merely a check |
| D3.2.5v2 | 721 | The surface spelling for the shared subtype relation is the binary `<:` operator |
| D3.2.6* | 730 | Verified once, valid while unchanged; a required field is a required field |
| **§D3.3** | 746 | *Inference* |
| D3.3.1v2 | 748 | A type-error-free script's evaluation result is never affected by inference |
| D3.3.2v2 | 768 | Entry-shape inference and body/result inference are **separate products**: float arithmetic on a parameter … |
| D3.3.3v3 | 778 | Inferred container element-type **narrowing dies with its binding**: it is a property of one binding's scope, … |
| D3.3.4 | 792 | Representation always follows the **full inferred contract** (D2.4, D2.5): an unproven indexed read infers … |
| D3.3.5 | 795 | System-function result relations are declarative registry metadata |
| **§D3.4** | 806 | *Shapes* |
| D3.4.1 | 808 | A map/element shape is a `ShapeEntry` chain on `TypeMap`/`TypeElmt`; |
| D3.4.2v2 | 814 | Shape identity is **structural, not nominal**: the ordered (field-name, TypeId) sequence, which is the path a … |
| D3.4.3v5 | 815 | Maps and elements share types through a per-`Input` transition tree, grown one field at a time |
| D3.4.4v4* | 816 | ShapeEntry name identity follows S8.2.2v4: a name is identified by its resolved namespace and its normalized … |
| D3.4.5 | 841 | Shape transitions obey the **map-layout invariant**: the exact runtime shape always describes the stored bytes |
| D3.4.6 | 849 | Shapes carry the immutable `LaneStorageDesc` derived from the full `Type*` via the one shared descriptor … |
| D3.4.7 | 853 | A runtime JavaScript `TypeMap` may carry one immutable `const JsClassMeta*` refinement selected before … |
| D3.4.8 | 862 | A JavaScript accessor descriptor is a virtual, object-local `ShapeEntry`: it owns a `JsAccessorCell*`, has … |
| **§D3.5** | 870 | *Physical binary schemas* |
| D3.5.1v2* | 872 | Binary schemas compile to immutable, module-owned field descriptors; |
| D3.5.2v2* | 881 | Native schema views retain an invalidatable lifetime control; |
| **§D4** | 893 | *Memory Management* |
| **§D4.1** | 895 | *Allocator tiers* |
| D4.1.1v2 | 897 | Four **content tiers**: **GC heap** (runtime values), **Input arena** (parsed documents), **AST/const pool**, … |
| D4.1.2 | 905 | Static Mark data is **born shared and immortal**: `MarkBuilder` sets the shared COW state at construction; |
| D4.1.3 | 911 | Input/format allocations are pool/arena-owned and **outside GC rooting entirely**; |
| D4.1.4v5* | 914 | Heap bytes are kept by exactly **four mechanisms** |
| D4.1.5* | 945 | Two mechanism-selection corollaries of D4.1.4 |
| **§D4.2** | 958 | *Memory Context* |
| D4.2.1v3 | 960 | `MemContext` is a **factory that owns every allocator** |
| D4.2.2v2 | 971 | Per-document sub-contexts key allocators to a document URL, with the **attribution/reclamation split**: … |
| D4.2.3* | 980 | Every arena and pool is **bound at creation to a context** |
| D4.2.4* | 986 | A shared arena/pool's lifetime is governed by an **allocator-level atomic `ref_count`**: created at 1 held by … |
| D4.2.5v3* | 993 | Allocation tracking is a **diagnostic, never the mechanism**: release builds default `memtrack` OFF; |
| D4.2.6 | 1010 | A pool may **own a resource that is not one of its blocks** through a **cleanup**: `pool_add_cleanup` … |
| **§D4.3** | 1024 | *Garbage collection* |
| D4.3.1 | 1026 | Non-moving mark-and-sweep, dual-zone |
| D4.3.2v2 | 1031 | Object zone = size-class segregated free lists backed by VM-owned extents; |
| D4.3.3 | 1037 | Precise closure-env tracing via `closure_field_count`; |
| D4.3.4 | 1040 | A shaped field whose declared lane is `null` is traced **conservatively**: the collector marks the eight-byte … |
| **§D4.4** | 1051 | *COW implementation* |
| D4.4.1 | 1053 | v1 ships the **1-bit shared flag** (`cow_state`, monotonic, idempotent; |
| D4.4.2 | 1059 | A COW copy is exactly **one level** |
| D4.4.3 | 1065 | All copy paths precisely root source/replacement/owner chain and reload after possible GC; |
| D4.4.4v4* | 1068 | A place handle |
| D4.4.6 | 1102 | A **place copy** (`let`/`var h = root.path`, root mutable) is share-marked at its bind iff **the place may be … |
| D4.4.5 | 1115 | A **move-out** place copy |
| **§D4.5** | 1126 | *The Radiant seam* |
| D4.5.1v4 | 1128 | One system-allocation substrate (`memtrack` + VM regions, owned through `MemContext`), **two policies**: … |
| D4.5.2* | 1139 | Radiant never retains a GC pointer |
| D4.5.3v2 | 1150 | UI/view-mode DOM construction deep-copies ordinary Elements and their content through MarkBuilder |
| **§D4.6** | 1163 | *Name identity* |
| D4.6.1v4 | 1165 | One semantic **property** identity is a `NameId`, never a `String*` address |
| D4.6.2v2 | 1182 | Evolve NamePool, don't replace it (first definer wins and parent-first lookup) |
| **§D4.7** | 1193 | *Const pool / MarkPack* |
| D4.7.1* | 1195 | One binary encoding for Lambda data, four consumers |
| D4.7.2* | 1201 | Two stream variants over one encoding (bare stream; |
| **§D5** | 1209 | *Execution State: Stacks and Rooting* |
| **§D5.1** | 1211 | *The stack model* |
| D5.1.1v3 | 1213 | Three stack-like mechanisms with distinct owners: the native C stack (invisible to GC); |
| D5.1.2 | 1229 | Static per-function frame sizing, no `MIR_ALLOCA`, two saved watermarks per frame, virtual reservation + … |
| D5.1.3 | 1234 | Downward flows never re-home; only upward flows and outliving stores do. Suspension is a re-homing barrier |
| D5.1.4 | 1238 | Cleanup order is load-bearing: all `MAY_GC` cleanup runs **before** root/number watermark restoration; |
| **§D5.2** | 1243 | *Scalar homes* |
| D5.2.1v5* | 1245 | Function wide-scalar returns use the **companion-lane convention**: a may-be-wide boxed return is the pair … |
| D5.2.2v3 | 1267 | Scalar homes are raw payload words, never GC roots, colored in a separate slot space from root slots; |
| D5.2.3* | 1284 | Watermark ownership selects the wide-return transport |
| **§D5.3** | 1307 | *GC rooting* |
| D5.3.1 | 1309 | Generated code uses **safepoint-current canonical slots**: a stable home per rootable binding plus colored … |
| D5.3.2 | 1314 | GC begins only inside a `MAY_GC` call |
| D5.3.3 | 1318 | C/C++ helpers use slot-backed `RootFrame` / `Rooted<T>` / handles / `PersistentRooted` on the same side-root … |
| D5.3.4 | 1325 | Rootability is driven by the four representation classes (`BOXED_ITEM`, `RAW_GC_POINTER`, `NON_GC_SCALAR`, … |
| D5.3.5 | 1331 | Argument passing reserves each function's **maximum lexically-overlapping argument arity as a fixed suffix of … |
| D5.3.6v2 | 1337 | Recovery frames form **one LIFO chain per activation** (the thread's base stack is one), a frame per boundary … |
| **§D5.4** | 1347 | *Runtime globals and EvalContext* |
| D5.4.1 | 1349 | One canonical long-lived `EvalContext` per isolate; exactly one sanctioned TLS root |
| D5.4.2 | 1354 | Context state lives in lazily-allocated opaque capsules; |
| D5.4.3 | 1359 | No context-dependent value at a code-baked address |
| D5.4.4 | 1364 | Frozen registries stay global (immutable after publication); |
| **§D6** | 1371 | *Function* |
| **§D6.1** | 1373 | *The effect coloring* |
| D6.1.1v2 | 1375 | `fn`/`pn` is a one-bit, declared, compiler-checked effect system (semantics §S12.1), realized as … |
| D6.1.2v2* | 1386 | The bit is load-bearing across the runtime: it is the const-folder's soundness gate, the stream-fusion and … |
| D6.1.3v2* | 1400 | Effect analyses beyond the bit are separate compiler facts: `may_await` (the call may park, so it may collect … |
| **§D6.2** | 1408 | *Function values and closures* |
| D6.2.1 | 1410 | A function value is a GC object carrying its entry pointers, its attached type/signature, and its closure … |
| D6.2.2v2 | 1416 | Dynamic calls dispatch through per-callee executable entries |
| D6.2.3v3* | 1430 | Closure representation respects capture permissions |
| D6.2.4 | 1444 | The closure env is precisely traced (`closure_field_count`); |
| **§D6.3** | 1449 | *Concurrency runtime* |
| D6.3.1 | 1451 | Tasks share the context heap under **one scheduler per context**; |
| D6.3.2* | 1457 | Workers are **share-nothing isolates** (thread default, process optional); |
| D6.3.3v2 | 1463 | Faults are scoped to one thread and one stack |
| **§D6.4** | 1470 | *System built-ins* |
| D6.4.1 | 1472 | Public sys funcs return **boxed `Item`** at the runtime/JIT boundary; |
| D6.4.2 | 1478 | Raw `-1` helpers are **adapter-only**; |
| **§D7** | 1485 | *Modules and Packaging* |
| **§D7.1** | 1491 | *Build packaging and layering* |
| D7.1.1 | 1493 | Build packaging is **static libraries** (dynamic is reserved for Jube modules): four archives + exe |
| D7.1.2v2* | 1500 | All resource IO lives in `lambda-io` |
| D7.1.3 | 1509 | Provider ownership beats include ownership |
| D7.1.4v2 | 1515 | Headless is **three profiles**: A (`lambda-cli`, D7.1.6) is a link-time omission of `radiant.a`; |
| D7.1.5 | 1525 | Mark API ownership: `MarkReader` is core; |
| D7.1.6 | 1529 | `lambda-cli` is the runtime-only host |
| D7.1.7v6 | 1537 | `lambda-wasm` is the browser evaluation profile |
| **§D7.2** | 1571 | *Script packages* |
| D7.2.1 | 1573 | A Lambda script module (package) is the compilation unit: its language profile is a unit property (D8.2.1), … |
| D7.2.2 | 1579 | Imports resolve to the **boxed `_b` symbol** of exported functions (export ⇒ boxed entry mandatory, D8.3.4v4); |
| D7.2.3 | 1584 | Imported packages are cached in-process (L1, D8.5.1) and distribute as **source** (D1.7); |
| D7.2.4v4 | 1587 | `lambda.*` is the one root for everything Lambda ships |
| D7.2.5* | 1606 | Full user-agent editing policy belongs to the shipped Lambda DOM behavior package, never to Radiant's native … |
| D7.2.6 | 1621 | Import resolution follows the S16.9.8 roots, one resolver for every caller |
| D7.2.7v2* | 1633 | LaTeX is read by TeX expansion, never by TeX typesetting |
| D7.2.8* | 1635 | Document math preserves its source notation's layout policy |
| D7.2.9 | 1647 | Bundled document fonts are shared file resources |
| **§D7.3** | 1654 | *Jube modules: the module system* |
| D7.3.1 | 1656 | Core = ECMA-262 + engine substrate + `./lib` + a **closed globals allowlist** (console, timers, … |
| D7.3.2 | 1661 | Modules are DSOs + `module.json`: hash-verified, ABI and hosted-API negotiated before any module code runs, … |
| D7.3.3 | 1667 | One strict host-API tier for everyone |
| D7.3.4 | 1673 | Core never links modules; |
| D7.3.5 | 1677 | Every module kind names its conformance gate, wired into CI |
| D7.3.6* | 1681 | The host exports an allowlist, nothing more |
| **§D7.4** | 1693 | *Jube modules: data and async contracts* |
| D7.4.1v2 | 1695 | `Item` is the only value currency; |
| D7.4.2v2 | 1702 | Modules are **fully shielded from the async substrate** |
| D7.4.3 | 1712 | Guest hosting crosses **no core types**: no `Runtime*`, `EvalContext*`, `Input*`, `MIR_context_t`, or … |
| D7.4.4 | 1722 | Declared interfaces + record-owned hooks are the ONLY host-object protocol — one way to be a host object, no … |
| D7.4.5v2* | 1734 | Three virtual carriers mirror the container taxonomy and occupy their structural TypeId bands: `vmap`, … |
| D7.4.6 | 1756 | A host module states each function's colour in its signature |
| **§D7.5** | 1765 | *Jube modules: trust and IO* |
| D7.5.1 | 1767 | Three trust tiers: **T1 system** (in-process DSO |
| D7.5.2* | 1776 | One central IO API is the sole IO door |
| D7.5.3v2 | 1782 | Lambda↔Radiant is two explicit contracts: Radiant embeds Lambda through a versioned, Jube-aligned embed … |
| D7.5.4* | 1790 | Audible output is a capability gated by document origin |
| D7.5.5* | 1808 | Apps and ad-hoc scripts execute within an admitted source/resource root |
| D7.5.6* | 1818 | New files under the app/project root are allowed by default; overwriting existing files requires user approval |
| D7.5.7* | 1826 | Per-app and per-ad-hoc-script grants live under `~/.lambda/setting`, controlled by the trusted host |
| D7.5.8* | 1834 | App/script network access is denied by default; package auto-update is a separate host-owned pull service |
| **§D8** | 1845 | *Compilation Pipeline* |
| **§D8.1** | 1847 | *Structure* |
| D8.1.1v18* | 1849 | First-party Lambda C lexer + hybrid recursive-descent/Pratt parser → the shared typed AST → **tiered … |
| D8.1.2v3 | 2004 | The shipped Lambda executable uses the first-party C parser as the final production implementation for normal … |
| D8.1.3v43* | 2015 | LambdaJS uses its first-party C lexer and hybrid recursive-descent/Pratt parser for normal JavaScript and … |
| **§D8.2** | 2328 | *Unified AST* |
| D8.2.1* | 2330 | One leveled core-node catalog serves host and guests; |
| D8.2.2* | 2335 | Variance tiers in strict preference order: fields/flags → clause chains → language-range node kinds; |
| D8.2.3v2* | 2342 | Migration is extract-after-convergence: the common core is extracted only after two working clients exist; |
| D8.2.4v2* | 2347 | The unified compiler has one **indexed compilation unit**: dense stable IDs name … |
| D8.2.5v3* | 2360 | One typed pass manager owns build → bind → validate → index → collect → capture/effect → environment layout → … |
| D8.2.6v2* | 2376 | Core expression lowering is demand-driven and returns the full `MirValue` of D2.4.2 |
| **§D8.3** | 2388 | *Dual-function compiling* |
| D8.3.1v2* | 2390 | Plan-dependent entries comprise the boxed `<name>_b` boundary and a **bounded set of immutable raw variants** |
| D8.3.2 | 2403 | The check lives in the callee |
| D8.3.3 | 2409 | Source-relative correctness is the invariant |
| D8.3.4v4 | 2414 | Visibility decides which versions exist |
| D8.3.5 | 2434 | Numeric admission at implicit boundaries is one rule (static whole-domain embedding admits + normalizes; |
| **§D8.4** | 2439 | *Dispatch policy* |
| D8.4.1v2 | 2441 | No inline caches anywhere in the Lambda lane or LambdaJS |
| D8.4.2v2* | 2456 | Core direct calls pass individual ABI operands (`Context*`, args); |
| D8.4.3v2* | 2460 | Every hosted-language helper uses an explicit completion ABI |
| **§D8.5** | 2479 | *MIR module cache* |
| D8.5.1v7* | 2481 | L1 |
| D8.5.2* | 2518 | The disk-cache direction is **code image + relocation journal** (Route B); |
| D8.5.3* | 2525 | The **differential write verifier is mandatory and fail-closed**: compile twice at different addresses, diff; |
| **§D8.6** | 2535 | *Emission testing* |
| D8.6.1 | 2537 | The MT7 ratchet: 0% slack |
| D8.6.2 | 2543 | One artifact contract: the MIR dump path honored in release; |
| D8.6.3 | 2547 | Liveness is verified by **dynamic oracles** |
| D8.6.4v2* | 2552 | Unified-AST consolidation has three **hard, fail-closed exit ratchets**: at least 2,000 net physical C/C++ … |
| DO1 | 2938 | Nullable-lane residue: boxed fallback for `any`/`number`/unions (confirm); |
| DO25 | 2942 | Node-identity carrier and operations (S5.1.4v2, D2.6.8): where the identity lives (a header word costs every … |
| DO2 | 2949 | Scalar-home audits: result-bearing dispatch-helper enumeration; |
| DO3 | 2952 | Box/unbox: union params stay boxed (revisit?); |
| DO4 | 2954 | ValueRep: `MirValue` size (measure first); |
| DO5 | 2959 | Numeric GC-fallback end state: per-boundary inventory to zero, then delete `scalar_heap`; |
| DO6 | 2963 | Side-root slot metadata (boxed-Item vs raw-pointer tag) |
| DO7 | 2966 | Stack maps (KIV) |
| DO8 | 2969 | CW4 shared-flag → saturating count/GC-refresh: blocked on CW5 counters from a live corpus; |
| DO9 | 2972 | Memory Context Stage 2 opens, incl |
| DO10 | 2975 | SF verification items: concat memcpy without tail-payload rebase (latent dangling wide scalars); |
| DO11 | 2980 | Dual-func: O1 promotion-aware numeric inference and lowering (Stage-1 boundary only today); |
| DO12 | 2984 | MIR cache: GC-root registry reset under L1 (verify before relying); |
| DO13 | 2990 | Emission testing: guest-transpiler scope (no guest is hosted since `lang-python` was removed, 2026-10-07); |
| DO14 | 2995 | Unified AST: the "views" fourth variance tier (adopt only when a real shared pass asks); |
| DO15 | 2997 | *(resolved 2026-08-07)* Online exception-poll doc status conflict: verified landed in `52c0f3c02` (2026-07-24) |
| DO25 | 3003 | Interpreter tier (D8.1.1v14) remains open for breadth/performance: satellite-module treatment under the MT7 … |
| DO16 | 3015 | Name identity: temporal canonical accepted; |
| DO17 | 3021 | Const pool: decimal sharing scope; |
| DO18 | 3025 | Sys-func registry: the public mutation convention (owner- returning vs unit) and `splice`'s public result; |
| DO19 | 3028 | Jube: `js_globals.cpp` allowlist audit; |
| DO20 | 3039 | Cross-isolate lifetime for shared graph Items (promote-on-share recommended) |
| DO21 | 3042 | Static modules: Class F scheduling (the browser-style engine/bindings split is the eventual shape); |
| DO22 | 3048 | First-class `Type*` representation needs its own design record: today the kind-discriminated layout is … |
| DO23 | 3053 | Shape system opens: cross-`Input` type sharing |
| DO24 | 3054 | Unnamed wide temporaries crossing a loop back edge, under D5.2.3's reclaim |
| DO26 | 3066 | The D2.6.6v2 migration, **updated after phase 1 landed and phase 2 began, 2026-09-03** |
| DO27 | 3088 | Representation of instance type alteration (S2.1.5): whether reconstruction reuses the old buffer when the … |
| DO28 | 3092 | Tier carrier divergence for annotated module bindings: the eager module init keeps a trusted `let T: string[] … |
| DO29 | 3102 | Typed `var` parameter rebind on the eager tier |
| DO30 | 3131 | *(closed 2026-09-07 |
| DO31 | 3143 | Whether an object declaration may name an element tag |
| DO32 | 3145 | Virtual PDF output follow-up: extend pure-producer demand coverage, retained-child representation (SO48), … |
