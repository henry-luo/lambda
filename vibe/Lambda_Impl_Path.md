# Lambda Path — Detailed Implementation Plan

> **Status:** implemented for the decided syntax/runtime scope; this document
> remains the implementation audit and handoff record
>
> **Date:** 2026-08-18
>
> **Last verified against the live tree:** 2026-08-18
>
> **Design authority:** `vibe/Lambda_Type_Path.md` (PTH1v2, PTH2v2,
> PTH3–PTH29)
>
> **Normative semantics:** [S2.4](../doc/Lambda_Formal_Semantics.md#s24-paths),
> [S7.1.1](../doc/Lambda_Formal_Semantics.md#s71-reads-are-total-writes-are-checked),
> [S10.4](../doc/Lambda_Formal_Semantics.md#s104-parent-navigation), and
> [S10.5](../doc/Lambda_Formal_Semantics.md#s105-root-navigation)
>
> **Normative implementation constraints:**
> [D1.6](../doc/Lambda_Formal_Design.md#d1-architecture),
> [D4.6](../doc/Lambda_Formal_Design.md#d46-name-identity), and
> [D5.4](../doc/Lambda_Formal_Design.md#d54-runtime-globals-and-evalcontext)

## 1. Outcome and implementation boundary

This plan lands the decided Lambda reference/path model as one normalized
runtime representation rather than a set of parser aliases. The completed
implementation must support:

```lambda
/.a.b                 // logical rooted path
.a.b                  // relative path
.~~.a.b               // relative parent path
~~.a                  // exactly ~.~~.a
value.~~.a            // dynamic parent navigation
value./                // dynamic root navigation
a./.b                 // root of a, then key b
file./.a.b            // explicit file path on the current machine
file.hostname.a.b     // explicit file path on a named machine
http.hostname.a.b     // explicit HTTP path
a.1                   // IntKey(1)
a.name                // NameKey(name)
a.'name'              // NameKey(name)
```

The implementation is complete only when parsing, AST construction,
interpretation, MIR Direct, path algebra, equality/hash/printing, target
qualification, and effectful I/O all observe the same root and operation
model. This is required by **S1.6**, **S2.4.2v3**, and **PTH20**.

For `file.hostname.a.b`, this iteration supports filesystem access only when
`hostname` identifies the current machine. It still parses, constructs,
normalizes, compares, hashes, and prints paths naming other machines, but any
attempt to force such a path must fail explicitly before an OS path or file
operation is attempted. DNS, host discovery, UNC/SMB/NFS/SSH access,
credentials, and remote directory traversal are deferred.

### 1.1 Included decisions

- New-only syntax: `/.a`, `.~~.a`, `.~~`, `value.~~`, `~~`, `file./`, and
  `value./` (**S2.4.1v2**, **S10.4.1–S10.4.2**, **S10.5.1**).
- Logical, relative, and explicit provider/authority roots remain structurally
  distinct (**S2.4.5v2**, **PTH21–PTH24**).
- Typed `NameKey` and `IntKey` path operations; no conversion of integer keys
  into names (**S2.4.2v3**, **PTH13–PTH14**).
- Left-to-right root/parent normalization and normalized composition
  (**S2.4.2v3**, **PTH7–PTH8**, **PTH28**).
- Immutable, evaluation-owned logical-root resolution with longest-prefix
  mounts, cycle rejection, and no existence-based fallback (**S2.4.4**,
  **PTH17–PTH19**).
- Occurrence-based dynamic parent/root navigation with transient cursors or
  zippers, never parent pointers embedded in values (**S10.4.3**,
  **S10.5.3**, **PTH10**, **PTH29**).
- MIR Direct and interpreter parity. The frozen C2MIR path is not extended
  (**D1.6**, **PTH12**).

The shipped resolver boundary is intentionally the small default policy needed
by this iteration: logical `/` qualifies to the local `file./` root through
`path_qualify_default`, while explicit provider paths remain unchanged. The
general immutable `ResolverContext` mount table described in §3.5 is the
follow-up extension; remote transport and network hostname discovery remain
deferred as stated above.

### 1.2 Explicitly excluded or deferred

- The optional `a."str"`/`StringKeyInput` design in PTH-O1 is not implemented.
  `.1` remains a float literal in leading expression position, and the rare
  leading relative integer-key path therefore remains unexpressible. `a.1`
  remains an `IntKey` member step.
- The old `/a`, `..a`, `value ..`, and `value .._..` forms receive no permanent
  compatibility aliases (**PTH11**).
- Remote file transport and local-network hostname discovery are deferred.
- This work does not redesign lexical/import namespace selection. Existing
  name qualification feeds the shared typed-key protocol, preserving
  **PTH16v2**, but broader namespace work stays separate.
- Lateral/sibling navigation remains the open issue SO19; only decided root
  and parent axes are included.
- Vendored Tree-sitter code is not edited. Only `grammar.js` is changed and
  generated files are refreshed with `make generate-grammar`.

### 1.3 Shipped implementation record

The implementation uses the existing persistent `Path` spine and pool
allocator. `PATH_SCHEME_LOGICAL`, explicit file authority metadata, typed
integer segments, and shared root/parent helpers were added in
`lambda/core/path.c`; `path_qualify_default` is the default logical-to-local
qualification boundary. The grammar and AST now classify `/.a`, `.~~`,
postfix `value.~~`/`value./`, `file./`, and dotted absolute authorities. Both
the interpreter and MIR Direct carry parent/root occurrence slots without
embedding lineage in data values. Focused path, syntax, authority, typed-key,
and parity tests cover the shipped behavior. The full mount-table resolver and
remote provider forcing remain deferred as stated in §1.1–§1.2.

## 2. Pre-implementation audit

The existing implementation contains useful provider and path machinery, but
its invariants do not match the decided model. The migration must address all
of the following rather than changing grammar tokens in isolation.

| Surface | Current state | Required change |
|---|---|---|
| `lambda/tree-sitter-lambda/grammar.js` | `_path_prefix` accepts `/`, `.`, and `..`; `parent_expr` accepts infix-style `..`; `/a` is valid. | Accept the new initial and postfix forms, retire old forms, and resolve `./` versus division contextually. |
| `lambda/runtime/ast.hpp` | `AstPathSegment` has a narrow segment kind; `AstParentNode` stores an object and depth. | Represent typed root/parent operations explicitly and share their semantic lowering with static paths. |
| `lambda/runtime/build_ast.cpp` | `/` lowers as `PATH_SCHEME_FILE`; `..` lowers as `PATH_SCHEME_PARENT`; absolute-head recognition omits `file`; integer collection is incomplete. | Separate logical root from file provider, classify `file`, preserve `NameKey`/`IntKey`, and desugar bare `~~` to current-item parent navigation. |
| `lambda/core/lambda-path.h`, `lambda/core/path.c` | `PathScheme` conflates scheme, logical/relative root, and parent; segments have a two-bit kind; concatenation copies segments without full normalization. | Introduce explicit root, authority, key, and operation kinds plus one normalizing constructor/composer. |
| `lambda/runtime/lambda-eval.cpp` | Path `++` and property access use the old scheme/segment representation. | Route all path construction/composition through the shared normalizer. |
| `lambda/runtime/interp.cpp` | Parent AST nodes are evaluated through existing member behavior; no occurrence cursor exists. | Lower root/parent navigation explicitly and carry occurrence lineage for direct navigation chains. |
| `lambda/runtime/transpile-mir.cpp` | MIR Direct has the old parent node and current-item registers only. | Add semantic runtime imports/lowering for typed operations and navigation carriers without exposing representation assumptions. |
| `lambda/runtime/interp_plan.cpp`, `lambda/runtime/emit_sexpr.cpp` | Know `AstParentNode`. | Migrate traversal/debug serialization to the new operation nodes. |
| `lambda/io/target.cpp`, `lambda/input/input.cpp` | `path_to_os_path` can project file/relative paths directly; no file authority gate exists. | Resolve logical roots first, validate file authority, then project an explicit local provider path. |
| `lambda/core/print.cpp` | Path output delegates to old spellings. | Emit only canonical new spellings. |
| `lambda/lambda-data.hpp`, `lambda/runtime/runner.cpp` | `EvalContext` has no path resolver/navigation capsule. | Add immutable resolver state through the opaque context pattern required by **D5.4.2**. |
| `lib/shell.h`, `lib/shell.c` | `shell_get_hostname()` already exposes the current hostname. | Reuse this helper through one authority service; do not add another `gethostname` implementation. |
| `test/test_path_gtest.cpp`, `test/lambda/path.ls` | Cover old roots, providers, and OS projection. | Replace retired expectations and add syntax, algebra, authority, resolver, parity, and GC-stress coverage. |

The generated `lambda/tree-sitter-lambda/src/parser.c` and generated enum files
are outputs, never hand-edited.

## 3. Target architecture

### 3.1 One typed reference operation vocabulary

Introduce or reuse one small, allocation-independent key descriptor shared by
path construction, member lookup adapters, resolver prefixes, and navigation
cursors. Before adding a type, grep the core/runtime headers for an equivalent
helper and promote it rather than duplicating it.

Conceptually:

```c
typedef enum RefKeyKind {
    REF_KEY_NAME,
    REF_KEY_INT,
} RefKeyKind;

typedef struct RefKey {
    RefKeyKind kind;
    union {
        NameId name_id;
        int64_t int_value;
    } value;
    StrView spelling;  // canonical text when printing/provider projection needs it
} RefKey;

typedef enum RefOpKind {
    REF_OP_KEY,
    REF_OP_ROOT,
    REF_OP_PARENT,
    REF_OP_WILDCARD,
    REF_OP_WILDCARD_RECURSIVE,
    REF_OP_DYNAMIC_KEY,
} RefOpKind;
```

The exact header location follows the existing NameId ownership boundary, but
the semantic rules are fixed:

- names and symbols become `NameKey` through the same NamePool operation;
- integer keys are admitted only from exact non-negative integer values;
  negative, fractional, or otherwise unsupported dynamic keys take the
  unsupported-read path under **S2.4.2v3** and **S7.1.1**;
- no stringification of integer keys and no parsing numeric-looking names back
  into integers;
- dynamic index expressions validate their result into `NameKey` or `IntKey`;
  unsupported values return the domain's normal unsupported/missing read
  result rather than creating a third retained key kind;
- NameId is semantic identity inside its scope, in accordance with
  **D4.6.1v2**; text is retained only where canonical printing, provider
  projection, or cross-context reconstruction requires it;
- no arbitrary process-local NameId is baked into cached MIR, in accordance
  with **D4.6.2v2** and **D5.4.3**.

### 3.2 Path root and authority representation

Replace `PATH_SCHEME_PARENT` and the implicit “file means slash” convention
with explicit, orthogonal fields:

```text
PathRoot
  kind: LOGICAL | RELATIVE | PROVIDER
  provider: NONE | FILE | HTTP | HTTPS | SYS | registered future provider
  authority: NONE | LOCAL_MACHINE | NAMED
  authority_name: NameKey when NAMED

PathOp
  kind: KEY | ROOT | PARENT | WILDCARD | WILDCARD_RECURSIVE
  key: NameKey | IntKey when kind is KEY
```

A `Path` remains an immutable, pool-owned persistent spine. Rename its
structural link from `parent` to `previous` (or an equivalently unambiguous
name) while migrating call sites: that link means “previous path operation,”
not the dynamic parent of a Lambda value. The root node owns the `PathRoot`;
each subsequent node owns one normalized `PathOp`.

Required encodings are:

| Source | Root representation | Operations |
|---|---|---|
| `/` | `LOGICAL` | none |
| `/.a.b` | `LOGICAL` | `NameKey(a)`, `NameKey(b)` |
| `.` | `RELATIVE` | none |
| `.~~.a` | `RELATIVE` | `PARENT`, `NameKey(a)` |
| `file./` | `PROVIDER(FILE, LOCAL_MACHINE)` | none |
| `file./.a.b` | `PROVIDER(FILE, LOCAL_MACHINE)` | `NameKey(a)`, `NameKey(b)` |
| `file.hostname.a` | `PROVIDER(FILE, NAMED(hostname))` | `NameKey(a)` |
| `http.hostname.a` | `PROVIDER(HTTP, NAMED(hostname))` | `NameKey(a)` |

`file.a.b` therefore means host authority `a`, then key `b`; it must never be
silently reinterpreted as the current machine path `/a/b`. `file./.a.b` is the
unambiguous current-machine spelling required by **S2.4.5v2** and **PTH27**.

Logical rooted and explicit provider paths stay structurally distinct even if
the resolver maps `/.a` to `file./.a`. Structural equality and hashing do not
consult the resolver (**PTH24**).

### 3.3 The sole path normalizer

Add one primitive, named along the lines of:

```c
Path* path_apply_op(Pool* pool, Path* base, RefOp op);
Path* path_compose(Pool* pool, Path* base, Path* relative_suffix);
```

All specialized helpers (`path_append_name`, `path_append_int`,
`path_select_root`, `path_select_parent`, wildcard construction, AST literal
construction, runtime member extension, and `++`) delegate to
`path_apply_op`. No caller may reconstruct or copy a path spine to implement
its own normalization.

The normalizer applies operations left-to-right:

- a key/wildcard appends one descendant operation;
- `PARENT` removes one immediately preceding descendant operation;
- at a relative root, an uncancelled `PARENT` is retained;
- at a logical or explicit provider/authority root, `PARENT` clamps at that
  root;
- `ROOT` discards descendant operations while preserving the logical or
  provider/authority anchor;
- an unresolved `ROOT` on a relative path is retained as `./`, since its
  hierarchy root is supplied only by the later occurrence/resolution context;
- composition accepts only a relative suffix and feeds each suffix operation
  through this same function.

The following become mandatory unit vectors, not merely printer examples:

```text
/.a.b.~~                    == /.a
/.a.~~.b                   == /.b
/.~~                       == /
.a.~~                      == .
.a.~~.~~.b                 == .~~.b
/.a.b./.c                  == /.c
.a.b./.c                   == ./.c
file./.a.b./.c             == file./.c
file.hostname.a./.b        == file.hostname.b
/.home.user ++ .~~.shared == /.home.shared
file.host.home ++ .docs./.tmp == file.host.tmp
```

Equality, hashing, canonical printing, target qualification, and composition
must all consume this already-normalized representation. A debug-only
invariant checker should reject adjacent cancellable key/parent pairs, parent
operations above anchored roots, descendant operations before a retained
relative root operation, and malformed authority/provider combinations.

### 3.4 Canonical printing

`path_to_string` is structural and effect-free. It never resolves mounts or
looks at the filesystem. It emits only:

- `/.a.b` for logical paths;
- `.a.b` and `.~~.a` for relative paths;
- `file./.a.b` for local file authority;
- `file.hostname.a.b`, `http.hostname.a.b`, etc. for named authorities;
- `./` and `.~~` operations at their normalized positions.

Identifier-safe `NameKey` values use `.name`; every other name uses the
existing single-quoted symbol/name escaping rules, so one key containing a dot
cannot print as several keys. `IntKey` uses canonical decimal notation. The
same rule applies to authority names: for example, a dotted host authority is
printed as `http.'example.com'.a`, not `http.example.com.a`. Do not introduce
the optional double-quoted StringKey syntax while implementing the printer.

The old `/a`, `..a`, and expression-parent spellings are never printed.
Printing a path, parsing the result, and printing again must be idempotent.

### 3.5 ResolverContext and pure qualification

Add a path resolver as an immutable capsule owned by the canonical
`EvalContext`, following **D5.4.1–D5.4.4**. Do not add mutable process-global
mounts and do not put a context pointer inside `Path` values.

The capsule contains:

- a frozen ordered collection of logical-prefix mount rules;
- the active relative base, adapted from the existing context `cwd`/target
  state rather than duplicated;
- a snapshot of the current-machine hostname;
- provider capability flags needed to reject unsupported forcing;
- test construction hooks that can inject hostname and mounts without relying
  on a CI machine's actual name.

Recommended API boundary:

```c
ResolverContext* resolver_context_create(...);
void resolver_context_free(ResolverContext* resolver);
bool resolver_context_freeze(ResolverContext* resolver, RuntimeError* error);
Path* resolver_qualify_path(ResolverContext* resolver, Path* path,
                            RuntimeError* error);
```

Qualification is pure address canonicalization:

1. Explicit provider paths bypass logical mounts.
2. Logical paths select the longest typed-key prefix mount.
3. The unmatched suffix is composed onto the mount target through
   `path_apply_op`.
4. Relative paths are composed against the active relative reference base.
5. Alias/mount cycles are rejected while freezing the table, before user code
   runs.
6. Missing mounts or unavailable capabilities produce deterministic resolver
   errors. There is no probe of alternate providers and no existence-based
   fallback.
7. No file stat, directory scan, HTTP request, or other I/O occurs in this
   function (**S2.4.4**, **PTH19**).

The first CLI/default resolver installs the explicit policy equivalent to:

```text
mount / -> file./
```

This preserves familiar local behavior while keeping `/.a` and `file./.a`
distinct before qualification. Unit tests must also install synthetic mounts
to `file.hostname` and `http.hostname` and prove longest-prefix selection and
cycle rejection.

### 3.6 Current-machine file authority

File authority validation belongs between pure qualification and OS-path
projection. Split the current all-purpose conversion into boundaries such as:

```c
Path* resolver_qualify_path(...);             // no I/O
bool file_authority_is_local(...);            // capability check, no network
bool provider_path_to_os_path(...);           // explicit local FILE only
```

Rules for this iteration:

- `file./` is always the current-machine authority.
- `file.<name>` is locally forceable only when `<name>` matches the resolver's
  immutable current-hostname snapshot.
- A named FILE or network authority is a `NameKey`; an integer or wildcard in
  the authority position is rejected as a malformed absolute root rather than
  treated as the first local path component.
- Obtain the production snapshot through the existing
  `shell_get_hostname()` helper. If layering requires moving that helper,
  promote one shared host-identity function; never copy the OS calls. Copy the
  returned name into resolver-owned storage and release the helper's temporary
  allocation according to its existing ownership contract.
- Preserve the authority's source spelling for path printing and structural
  equality. Host comparison may use one documented platform-neutral hostname
  normalization (ASCII case folding and removal of one terminal dot) solely
  for deciding whether the named authority is local.
- Do not call DNS, inspect the network, try UNC syntax, or test path existence
  when comparing authorities.
- A nonmatching hostname remains a valid path value but forcing it returns an
  explicit “remote file authority unsupported” runtime error. It must fail
  before `path_to_os_path`, `file_stat`, directory iteration, or input dispatch.
- `exists` must use its existing error-capable Boolean/result channel for an
  unsupported remote authority rather than report a potentially misleading
  local absence. Other APIs use their normal declared runtime-error channel.

The named-current-host case and `file./` must project to the same local OS
target, but their source `Path` values remain structurally different. Tests
use an injected hostname such as `test-host`; one non-golden smoke test may
exercise the real hostname helper.

### 3.7 Parser and CST shape

Change only `lambda/tree-sitter-lambda/grammar.js`, then regenerate. The target
grammar behavior is:

- `/` is a complete logical path primary; ordinary `.name` postfix steps form
  `/.name` and therefore make `/name` invalid.
- `.` is a complete relative path primary; its initial step may be `.name`,
  `.'symbol'`, `.*`, `.**`, or `.~~` according to the decided grammar.
- `~~` is a contextual primary whose AST semantics are exactly `~.~~`.
- postfix `.~~` and `./` have the same left-associative precedence as member
  and index access.
- postfix `./` may terminate (`a./`) or be followed by any ordinary postfix
  operation (`a./.b`, `a./[i]`, `a./.~~`).
- `file./` parses through the same postfix-root operation as `a./`; AST
  classification recognizes the registered `file` head and converts it into
  a local file authority.
- a registered scheme at the head of a dotted chain is reserved as an
  absolute path under **PTH23**. `file.hostname.a`, `http.hostname.a`,
  `https.hostname.a`, and `sys...` retain/extend current absolute-path
  behavior.
- bare `file` remains an ordinary name until it participates in a recognized
  absolute-path chain, avoiding an unnecessary lexical reservation.
- `.1` remains tokenized as the existing float literal. Do not change numeric
  lexing to make the PTH-O1 case work.
- infix `/` remains division. The postfix `./` token/production is accepted
  only after an expression at postfix precedence, so the parser does not
  reinterpret ordinary division.

Remove `parent_expr`, its `..` conflict, and any `_path_prefix` branch that
accepts the retired form. Add grammar/corpus coverage for valid chains,
precedence with calls/index/member/division, and negative old spellings before
regenerating `parser.c` and `lambda/runtime/ts-enum.h`.

### 3.8 AST and static-path classification

Use an explicit navigation-operation AST node rather than lowering `.~~` to a
member named `parent`:

```text
AstNavigationNode
  object: AstNode*
  operation: ROOT | PARENT
```

One postfix occurrence creates one node, so `a.~~.~~` is naturally
left-associated. Bare `~~` is built as `PARENT(CURRENT_ITEM)` while retaining
the original source span for diagnostics. `.parent` remains an ordinary member
expression; a type may define that field/property, but it is never a syntactic
alias for `.~~` (**S10.4.1**).

Extend `AstPathNode` to carry a typed root specification and ordered `RefOp`
segments. It must preserve integer segments, explicit file authority, and
root/parent operations. The builder may continue recognizing absolute paths
from ordinary member chains, but `get_path_scheme_from_name` must include
`file` and classification must obey these boundaries:

- `file./` -> explicit file/local path root;
- `file.hostname...` -> explicit file/named path root;
- `file` followed by an unrecognized non-reference construct -> ordinary name;
- `/` -> logical root, never `PATH_SCHEME_FILE`;
- `.~~...` -> relative root with parent operations, never a parent scheme.

Update all AST walkers, expression plans, sexpr/debug emitters, and source
dumps. Both CST and AST current-item detectors must count bare `~~` as a free
`~`, preserving mapping-pipe selection under **S10.1.2**. Nested current-item
scopes continue to obey **S10.1.3**.

### 3.9 Static versus dynamic lowering

Static path syntax constructs a normalized `Path` without I/O. Dynamic
member/index syntax evaluates its base and resolves a key at runtime. Both use
the same `RefKey` validation and domain-access helper, satisfying
**S2.4.3v2** and **PTH20**.

Interpreter and MIR Direct lowering must call semantic helpers rather than
reimplementing layout or normalization:

```text
path literal/key       -> path_apply_op
path .~~               -> path_apply_op(PARENT)
path ./                 -> path_apply_op(ROOT)
path ++ relative       -> path_compose
non-path value .~~     -> navigation_parent
non-path value ./      -> navigation_root
member/index key       -> ref_key_resolve + domain lookup
```

Register any new runtime imports once in `sys_func_registry.c`; reuse the same
helpers from `interp.cpp`. No changes are made to `transpile.cpp` or
`--c2mir`, per **D1.6**.

### 3.10 Occurrence-aware dynamic navigation

Parent/root lineage is evaluation metadata, not Lambda data. Implement an
internal carrier, conceptually:

```text
NavigationValue
  value: Item
  cursor: optional NavigationCursor

NavigationCursor
  root occurrence
  parent occurrence
  incoming typed key/index
  previous cursor or zipper position
```

The final layout must use the runtime's precise root-frame/activation
facilities for every retained `Item`. It must not rely on conservative native
stack scanning, store raw unregistered Items across an allocation/safepoint,
or put root/parent pointers in arrays, maps, elements, VMaps, or other Lambda
values. If a new runtime module is added, register it in
`build_lambda_config.json` and regenerate build files through `make`; do not
edit generated `.lua` files.

Implementation rules:

1. Inventory every construct that binds `~` (`|`, nested pipes, match/that
   contexts, and any view/query traversal) and whether it can suspend. A
   cursor lives in the owning interpreter/MIR activation, not in a mutable
   EvalContext-global stack that another task could overwrite.
2. Mapping iteration installs an occurrence whose parent is the iterated
   container and whose root is the outermost active traversal root. Nested
   member/index steps derive a child cursor with the actual typed incoming key.
3. Navigation-aware lowering carries the cursor through a direct syntactic
   member/index chain only when a later `./` or `.~~` requires it. Ordinary
   value storage, function return, and container insertion store only `Item`
   and deliberately discard incidental lineage.
4. A path value uses its intrinsic typed root/operation spine for `./` and
   `.~~`; it does not use a dynamic container cursor.
5. A standalone hierarchical value with no occurrence cursor is its own root
   for `value./` and has no parent for `value.~~`.
6. A scalar or host value with no defined hierarchy relation returns `null`;
   missing subsequent navigation continues under **S7.1.1**.
7. Host-backed hierarchical models may later supply a cursor/zipper adapter,
   but this iteration must not invent lineage by scanning for equal values.
   Equal values at different occurrences may have different parents, as
   required by **S10.4.3** and **S10.5.3**.

Before landing this stage, add forced-GC and nested/reentrant evaluation tests
that keep cursors live across every helper capable of allocation. If procedural
suspension can retain a current-item chain, the cursor must be activation-owned
and traced for the full suspended lifetime; otherwise reject that lowering
until such ownership is implemented rather than retaining a stack address.

### 3.11 Target and I/O integration

Refactor all effectful consumers to use the same sequence:

```text
Path value
  -> resolver_qualify_path        (pure)
  -> provider/authority check     (pure capability decision)
  -> explicit Target
  -> provider-specific projection
  -> I/O
```

Apply it to:

- `item_to_target` and target concatenation;
- input dispatch;
- `exists`, metadata/property forcing, and content reads;
- wildcard/directory iteration;
- any formatter or command path that currently calls `path_to_os_path`
  directly.

After migration, `path_to_os_path` should either be private to the explicit
local-file provider or require an already-qualified provider path. It must
reject logical roots, unresolved relative roots, HTTP paths, and nonlocal file
authorities. This makes accidental local fallback structurally difficult.

HTTP/HTTPS/SYS behavior needs regression tests because their current absolute
recognition is reused. Resolution must preserve authority versus child-key
boundaries; for example, `http.example.'a.b'` has authority `example` and a
single name key `a.b`, not three authority/path fragments.

Provider projection receives typed keys, not a prematurely flattened string.
The local FILE adapter emits a `NameKey` as one escaped/validated OS component
and an `IntKey` as canonical decimal digits. HTTP and other providers define
their own component encoding. This preserves structural key identity until
the final provider boundary and prevents `NameKey("1")` from becoming
`IntKey(1)` merely because both may project to the text `1`.

## 4. Phased implementation plan

Each phase ends green and has an independently reviewable invariant. Do not
land the grammar as a façade over the old `/ == file` representation.

### Phase P0 — Freeze baselines and add executable semantic vectors

**Work**

- Record the current focused path GTest and Lambda baseline results.
- Inventory current path tests, platform goldens, syntax-negative conventions,
  and every direct `path_to_os_path` caller.
- Record the section 3.3 vectors as the implementation checklist; add each
  executable vector in the phase that introduces its required API.
- Establish the new script/golden names up front so later phases do not split
  one semantic matrix across ad hoc files.

**Files**

- `test/test_path_gtest.cpp`
- `test/lambda/path.ls` and platform goldens, or a new focused
  `test/lambda/path_reference_v2.ls` plus `.txt`
- `test/lambda/negative/syntax/` and its harness metadata/goldens

**Exit gate**

- Old baseline is recorded.
- No disabled or expected-failure test is added merely to stage this work.
- There are no machine-specific hostname strings in golden output.

### Phase P1 — Land the typed path kernel

**Work**

- Add/reuse `RefKey` and `RefOp` without duplicating existing NameId helpers.
- Replace `PATH_SCHEME_PARENT` with explicit parent operations.
- Add logical, relative, and provider root kinds plus local/named authority.
- Expand the segment/operation representation beyond the current two-bit
  limit.
- Rename the structural `Path::parent` link to avoid dynamic-parent ambiguity.
- Implement `path_apply_op`, `path_compose`, invariant checks, structural
  equality, structural hash, depth, and canonical print.
- Make every old constructor delegate to the kernel temporarily, then remove
  obsolete constructors once callers migrate.

**Primary files**

- `lambda/core/lambda-path.h`
- `lambda/core/path.c`
- the existing NameId/key header, or one promoted shared reference-key header
- `lambda/lambda.h`
- `lambda/core/print.cpp`
- `test/test_path_gtest.cpp`

**Exit gate**

- The full normalization/composition table passes.
- Equality and hash agree for every equal vector and distinguish logical from
  explicit provider roots.
- Print/parse-independent unit strings use only new canonical forms.
- No I/O or EvalContext access exists in the path kernel.

### Phase P2 — Add ResolverContext and authority qualification

**Work**

- Add the opaque resolver capsule to `EvalContext` without changing the
  JIT-visible context prefix.
- Implement frozen mounts, longest typed-prefix matching, suffix composition,
  and cycle detection.
- Install the default `/ -> file./` policy at evaluation setup.
- Snapshot the production hostname with `shell_get_hostname()` and provide a
  test-only injected construction path.
- Implement current-machine authority matching and explicit remote-authority
  rejection.
- Keep qualification pure; add spies/mocks proving no provider I/O occurs.

**Primary files**

- `lambda/lambda-data.hpp`
- `lambda/runtime/runner.cpp`
- a focused `lambda/runtime/path_resolver.*` module if no suitable module
  already owns this responsibility
- `lib/shell.h`, `lib/shell.c` only if the existing helper needs promotion
- `build_lambda_config.json` if a new translation unit is added
- focused resolver/path GTests

**Exit gate**

- `/.a` qualifies to `file./.a` under the default resolver while retaining its
  original structural value.
- Longest-prefix and cycle tests pass.
- `file./.a` and `file.test-host.a` both qualify locally under injected
  `test-host`.
- `file.other-host.a` remains constructible but is rejected as unsupported
  before OS-path projection.
- No mutable process-global resolver state or repeated-execution lock is added
  (**D5.4.4**).

### Phase P3 — Change grammar and AST

**Work**

- Implement logical `/`, relative `.`, `.~~`, bare `~~`, postfix `.~~`, and
  postfix `./` in `grammar.js`.
- Remove old parent syntax and conflicts.
- Regenerate through `make generate-grammar`; inspect the generated diff but
  never edit it.
- Introduce the explicit navigation AST operation and typed path roots/ops.
- Add `file` to registered absolute-head classification.
- Preserve integer keys and quoted symbol/name keys.
- Update AST traversal, current-item detection, interpreter planning, and
  sexpr/source debug output.

**Primary files**

- `lambda/tree-sitter-lambda/grammar.js`
- generated parser/enums via `make generate-grammar`
- `lambda/runtime/ast.hpp`, `lambda/runtime/ast-core.hpp`
- `lambda/runtime/build_ast.cpp`
- `lambda/runtime/interp_plan.cpp`
- `lambda/runtime/emit_sexpr.cpp`
- parser and Lambda syntax tests

**Exit gate**

- Every included form in section 1 parses to the intended typed AST.
- `/a`, `..a`, `value ..`, and `value .._..` fail syntax tests.
- `.1` still parses as float; `a.1` still parses as `IntKey(1)`.
- `a / b` remains division, while `a./`, `a./.b`, and `file./` are root
  navigation.
- Bare `~~` selects mapping-pipe mode exactly as free `~` does.

### Phase P4 — Lower static paths in interpreter and MIR Direct

**Work**

- Construct all static paths through `path_apply_op`.
- Lower root/parent operations on path values through the same helper.
- Change path `++` to `path_compose` and reject nonrelative suffixes according
  to the existing operator error contract.
- Add semantic MIR runtime imports; do not inline `Path` layout in generated
  code.
- Update path equality/hash dispatch to use structural helpers.
- Preserve `http`, `https`, and `sys` behavior through the new root model.

**Primary files**

- `lambda/runtime/interp.cpp`
- `lambda/runtime/transpile-mir.cpp`
- `lambda/runtime/lambda-eval.cpp`
- `lambda/runtime/sys_func_registry.c`
- `lambda/runtime/lambda-data-runtime.cpp`
- `lambda/core/print.cpp`
- focused `.ls`/`.txt` parity tests

**Exit gate**

- Interpreter and MIR Direct produce byte-identical canonical path output and
  equal results for all static/algebra vectors.
- No path operation is lowered as a string member named `parent` or `root`.
- MIR Direct does not bake a resolver, hostname, NameId from another context,
  or `Path` field offset (**D5.4.3**).
- C2MIR files are unchanged.

### Phase P5 — Route targets and I/O through qualification

**Work**

- Split pure resolution from provider projection and I/O.
- Migrate target conversion, input, exists/metadata/content, and wildcard
  iteration.
- Make local OS conversion accept only explicit, locally authorized FILE
  paths.
- Add current-host named-authority success and remote-authority failure tests.
- Add regression coverage for relative cwd behavior and HTTP/HTTPS/SYS
  targets.

**Primary files**

- `lambda/io/target.cpp`
- `lambda/input/input.cpp`
- `lambda/core/path.c`
- `lambda/runtime/lambda-eval.cpp`
- any formatter/command found by `rg 'path_to_os_path'`
- `test/test_path_gtest.cpp`, input/target tests, platform goldens

**Exit gate**

- No effectful caller projects a logical or nonlocal authority directly.
- Remote hostname forcing returns the declared unsupported-authority error and
  performs zero filesystem calls.
- `file./` and the injected current hostname read the same local fixture.
- POSIX and Windows path projection tests pass without introducing remote UNC
  behavior.

### Phase P6 — Add occurrence-aware dynamic `./` and `.~~`

**Work**

- Complete the `~` binding/suspension inventory.
- Add activation-owned, precisely rooted navigation carriers.
- Teach member/index lowering to retain a cursor only for direct navigation
  chains that need it.
- Install cursors during mapping/traversal and preserve nested lexical current
  scopes.
- Implement `navigation_parent` and `navigation_root` for built-in
  hierarchical domains and the path-specialized branch.
- Return `null` for missing relations without scanning the heap for an equal
  value.
- Add forced-GC, duplicate-value occurrence, nested-pipe, repeated-parent,
  root-after-parent, and reentrant tests.

**Primary files**

- `lambda/runtime/interp.cpp`
- `lambda/runtime/transpile-mir.cpp`
- current-item/root-frame helpers in the runtime
- a focused `lambda/runtime/navigation.*` module if shared code would otherwise
  be duplicated
- `lambda/runtime/interp_plan.cpp`
- dynamic navigation `.ls`/`.txt` and GC tests

**Exit gate**

- Two equal child values in different parents navigate to their own occurrence
  parents.
- `~~` and `~.~~` are observably identical in all current-item scopes.
- `a./.b`, `value.~~./.name`, and repeated `.~~` obey left association.
- A missing parent/root returns `null` and safely chains.
- Forced GC and supported suspension/reentrancy tests show no stale cursor or
  unrooted Item.
- No container/value layout gains an observable lineage pointer.

### Phase P7 — Remove legacy paths and close documentation drift

**Work**

- Delete obsolete parent-scheme/AST constructors after all call sites migrate.
- Remove legacy printer branches and parser conflict comments.
- Search docs/examples/tests for retired path spellings and update language
  reference material that describes executable syntax.
- Keep historical decision records unchanged where they intentionally quote
  old syntax; annotate rather than rewrite history if needed.
- Update the implementation footnote for **S2.4/S10.4/S10.5** only when the
  feature is actually complete.

**Exit gate**

- `rg` finds no live implementation use of `PATH_SCHEME_PARENT` or
  `AstParentNode`.
- Old syntax appears only in negative tests, migration notes, or historical
  records.
- All updated docs cite **S2.4**, **S10.4**, or **S10.5** before PTH IDs when a
  formal ruling covers the subject.

### Phase P8 — Final conformance and release gate

Run, at minimum:

```bash
make generate-grammar
make build-test
./test/test_path_gtest.exe
make test-lambda-baseline
```

Then run the focused target/input tests, forced-GC test target identified by
the live test inventory, and the full `make test` before marking the plan done.
Cross-platform acceptance must include Linux/macOS local paths and the existing
Windows path golden. Performance checks, if added, use `make release`, never a
debug build.

Final evidence records:

- exact commands and commit/date;
- grammar generation clean-after-regeneration check;
- focused path/resolver/navigation test counts;
- Lambda baseline result;
- forced-GC result;
- Linux/macOS/Windows result or an explicit platform gap;
- a source search proving no remote hostname reaches an OS-path helper;
- a source search proving C2MIR and vendored parser sources were not edited.

## 5. Required test matrix

### 5.1 Syntax and classification

| Case | Required result |
|---|---|
| `/`, `/.a.b` | logical `Path` |
| `.`, `.a.b`, `.~~.a`, `.~~.~~` | relative `Path` with typed ops |
| `file./`, `file./.a.b` | explicit FILE/local authority |
| `file.hostname`, `file.hostname.a.b` | explicit FILE/named authority |
| `http.hostname.a`, `https.hostname.a`, current `sys` forms | explicit provider path |
| `~~`, `~~.a`, `~~.~~.a` | current-item parent navigation |
| `value.~~`, `value.~~.a` | postfix parent navigation |
| `value./`, `a./.b`, `value./[i]` | postfix root navigation |
| `a.1`, `a.'1'` | `IntKey(1)` versus `NameKey("1")` |
| `.1` | float literal, not path |
| `/a`, `..a`, `value ..`, `value .._..` | syntax error |
| `a / b` | division |

### 5.2 Structural algebra

- Root kind/provider/authority/key-sensitive equality and hashing.
- Every normalization vector from section 3.3.
- Parent clamping for logical and qualified roots.
- Retained parents and unresolved root for relative paths.
- Composition with name, integer, wildcard, root, and parent operations.
- Canonical print round trips and no legacy spelling.
- Large depth without fixed local arrays or silent truncation.

### 5.3 Resolver and authority

- Default `/ -> file./` qualification.
- Synthetic longest-prefix mount overriding the default.
- Mount to named file authority and HTTP authority.
- Direct absolute path bypassing mounts.
- Deterministic missing mount and cycle errors.
- No existence-dependent fallback.
- Injected current hostname accepted; different hostname rejected before I/O.
- Host normalization comparison without changing structural print/equality.
- Concurrent EvalContexts with different mounts/host snapshots do not leak
  state into one another.

### 5.4 Dynamic navigation

- Parent and root of direct map/object/array/list occurrences.
- Duplicate equal values under different parents.
- Direct member/index chains and repeated `.~~`.
- `~~ == ~.~~`, including mapping-pipe classification.
- Nested current-item scopes under **S10.1.3**.
- Standalone hierarchical value: self root, missing parent.
- Scalar/unsupported host relation: `null`.
- Path-specialized root/parent normalization.
- Missing relation followed by more steps remains `null` under **S7.1.1**.
- Forced GC, nested evaluation, and any supported suspended activation.

### 5.5 Provider/I/O regression

- `file./` local read, metadata, exists, and wildcard iteration.
- `file.<injected-current-host>` produces the same local target.
- `file.other-host` never performs local or network I/O.
- Logical path through default mount matches explicit local target behavior.
- Relative path uses the existing active cwd/reference base.
- HTTP/HTTPS authority and URL projection remain intact.
- SYS path behavior remains intact.
- Windows drive/root projection remains local-only; no accidental UNC support.

## 6. Review invariants and failure traps

Review every phase against these nonnegotiable invariants:

1. **One normalizer.** Parser, interpreter, MIR, `++`, and resolver composition
   cannot carry separate parent/root cancellation logic.
2. **No implicit provider.** A logical root reaches the filesystem only through
   an explicit resolver mount.
3. **No remote fallback.** A named noncurrent file authority cannot become a
   local path because resolution or existence probing failed.
4. **No value lineage.** Parent/root occurrence metadata is transient,
   activation-owned, and precisely rooted.
5. **Typed keys survive.** `NameKey("1")` and `IntKey(1)` remain distinct at
   every boundary; PTH-O1 adds no retained runtime kind.
6. **Pure paths.** Construction, equality, hash, print, normalization, and
   qualification perform no I/O.
7. **Context isolation.** Mounts, hostname snapshot, and capabilities are
   immutable per evaluation; generated code never embeds them.
8. **Canonical output.** Only the new syntax is printed; retired syntax fails
   rather than silently changing meaning.
9. **Backend parity.** Interpreter and MIR Direct call shared semantic helpers;
   C2MIR remains frozen.
10. **Precise GC.** No navigation Item survives a safepoint outside a registered
    root or activation slot.

Common implementation traps to reject in review:

- treating `~~` as a scheme/root again;
- implementing `.~~` as `.parent`;
- parsing `file.a.b` as local `/a/b`;
- making `/.a` and `file./.a` the same structural value;
- calling `path_to_os_path` before resolver/authority checks;
- retaining a raw pointer to a stack cursor across a call or suspension;
- scanning containers for an equal child to infer a parent;
- changing float lexing so `.1` becomes a path;
- adding a second hostname implementation;
- editing generated parser, build `.lua`, C2MIR, or vendor sources manually.

## 7. Definition of done

This plan may be marked complete only when all of the following are true:

- The syntax and semantics in **S2.4.1v2–S2.4.5v2**, **S10.4.1–S10.4.3**, and
  **S10.5.1–S10.5.3** are implemented for the included built-in domains.
- `/.a`, `.a`, `.~~.a`, `file./.a`, `file.hostname.a`, `value.~~`, bare `~~`,
  `value./`, and `a./.b` pass source-level tests in interpreter and MIR Direct.
- All construction/composition paths use the shared typed normalizer.
- Logical resolution is immutable, deterministic, context-owned, pure, and
  covered by longest-prefix/cycle tests.
- Explicit current-machine file paths work through `file./` and through the
  actual/injected current hostname.
- Noncurrent `file.hostname` values remain addressable but cannot initiate any
  local or network file access; they fail with the documented unsupported
  authority error.
- Equality, hashing, printing, target conversion, and I/O forcing agree with
  the normalized root/operation representation.
- Dynamic root/parent navigation is occurrence-correct under duplicate values,
  nested current scopes, and forced GC, with no value-layout lineage fields.
- Retired syntax is absent from live examples and rejected by tests.
- Every new Lambda unit script has a matching expected-result file.
- Focused tests, `make test-lambda-baseline`, forced-GC coverage, and the final
  cross-platform test matrix are green and recorded.

## 8. Deferred follow-up ledger

Completion of this plan deliberately leaves these follow-ups open:

- transport/provider design for noncurrent `file.hostname` authorities,
  including authentication, capability policy, and network error semantics;
- local-network hostname discovery and aliases;
- Windows remote volume/UNC semantics;
- user-facing resolver/mount configuration syntax beyond the internal/default
  immutable table;
- PTH-O1 `StringKeyInput`/`a."str"` option;
- lateral/sibling navigation under SO19;
- broader lexical/import namespace unification beyond the typed-key adapter;
- any host data model that needs a custom root/parent zipper.

None of these follow-ups may be emulated by filesystem probing, string-key
coercion, stored parent pointers, or mutable process-global resolution.
