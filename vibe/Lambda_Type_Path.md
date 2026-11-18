# Lambda Path and Unified Reference Semantics

**Date:** 2026-08-18
**Status:** Concrete provider access and path references ratified 2026-11-18. The local filesystem model is implemented on interpreter and MIR. [Implementation plan](impl/Lambda_Impl_Path_Node_Access.md).
**Decision IDs:** PTH1v3, PTH2v4, PTH3, PTH4v2, PTH5–PTH13, PTH13.1–PTH13.2, PTH14–PTH15, PTH16v3, PTH17–PTH20, PTH21v3, PTH22–PTH26, PTH27v5, PTH28v2, PTH29, PTH81–PTH86
**Revised:** 2026-09-21 — the relative path is spelled `\.a.b` (S16.9.4, [Design_Syntax §7.15](Lambda_Design_Syntax.md)); PTH2, PTH4, PTH21, PTH28 revised, prior wording in Appendix S. Later the same day (USER): PTH2v4 (the relative root is `\`, like `/`, so `\[1]` is the relative index step and `\.` alone is an error), PTH13.1 (a subscript on a path is the dotted step, `(/)[1]` is `/.1`, and every step inside a path literal is a key step), and PTH13.2 (an integer key may be the first step, `\.1`, `/.1`), closing PTH-O1's residue (S2.4.2v5, S16.2.4v3). **2026-10-20 (USER):** PTH81–PTH84 (§6.4) — a wildcard step may sit anywhere, forcing continues per match, a step applied to a file match enters the file while `**` never crosses the boundary by itself, path-domain results deduplicate, `**` is the closure of `*` and includes its base, and `#` is the line between reference and value (S2.4.6, S8.2.5, S8.2.6); implemented on both tiers the same day
**Open issue IDs:** PTH-O1

> **Normative anchors:** [S1.7](../doc/Lambda_Formal_Semantics.md#s1-core-principles)
> (one symbol, one concept),
> [S2.4](../doc/Lambda_Formal_Semantics.md#s24-paths) (path syntax and
> algebra), [S7.1](../doc/Lambda_Formal_Semantics.md#s71-reads-are-total-writes-are-checked)
> (total reads), and
> [S10.4](../doc/Lambda_Formal_Semantics.md#s104-parent-navigation)
> (contextual parent access), and
> [S10.5](../doc/Lambda_Formal_Semantics.md#s105-root-navigation)
> (root access). This record contains the full design and implementation
> consequences; the formal semantics is authoritative when the two disagree.

This record is the full path-design authority. The concise
[`Lambda_Expr_Path.md`](Lambda_Expr_Path.md) companion covers current mapping,
forcing, and implementation boundaries; the detailed implementation audit is
[`Lambda_Impl_Path (done).md`](impl/Lambda_Impl_Path%20(done).md).

## 1. Decision

Lambda references plus root and parent navigation use one dotted key
hierarchy:

```lambda
/.a.b          // rooted node; the default file mount selects /a/b
\.a.b          // reference relative to the active reference base
\.~~.a.b       // parent-relative reference; file projection is ../a/b
\.~~.~~.a      // two parent steps; file projection is ../../a
file.localhost.a.b     // file node on the current machine
file.host.a.b  // file node on machine host
http.host.a.b  // HTTP provider value
\.http.host.a.b // absolute path reference

~.a            // a of the current contextual value
~~.a           // parent of ~, then a
~~.~~.a        // grandparent of ~, then a
value.~~.a     // parent of value, then a
value./.a      // root of value's hierarchy, then a
a./.b          // root of a's hierarchy, then b
^.message      // message of the current handled error; unchanged
```

The central rule is:

> **`~~` is the parent step. It is never a path root. Bare `~~` is exactly
> `~.~~`. Every subsequent static step, including another parent step, is
> introduced by `.`.**

The corresponding root rule is:

> **`/` is the root-selection operation. At the start of a path it selects the
> logical global root. After a value or path, postfix `./` selects the root of
> that base's hierarchy. Subsequent static steps are again introduced by `.`.**

The old spellings `/a`, `..a`, `value ..`, and `value .._..` are retired, as
is the bare-dot relative `.a.b` (S16.9.4). There is no permanent compatibility
alias.

A path's initial form is exactly one of logical rooted `/`, relative `\`, or
an explicit absolute `SchemeName`; after that root choice, every form uses the
same dotted/indexed steps.

## 2. Unified reference scheme

Lambda has one semantic address shape for bindings, namespace members,
container members, indexed data, files, URLs, and other hierarchical
providers:

```text
Reference = (root, operation*)
Key       = NameKey | IntKey
Operation = Key | Root | Parent | Wildcard | DynamicKey
```

A reference root is one of:

- the logical global root `/`;
- the active relative root `\`;
- a statically selected namespace root;
- an explicit absolute provider root such as `file`, `http`, `https`, or
  `sys`, optionally qualified by authority keys such as `http.hostname`;
- a runtime value/occurrence, including `~`, `^`, or an evaluated member base,
  for a dynamic member expression.

Ordinary hierarchical keys have exactly the two kinds already present in
Lambda's unified container key space (**S8.2.1v2**): a `NameKey` normalized
from a string or symbol, or an `IntKey` normalized from an exact non-negative
integral numeric value. Parent and wildcard steps are navigation operations,
not additional key kinds. Root selection is likewise an operation, never a
key. `DynamicKey` defers choosing a key until evaluation; the resolved
container must admit the normalized key kind. A mismatch is an invalid member
access: reads yield `null` under **S7.1.1v2**, while writes raise through the
hard `T^` channel under **S7.1.3v2** and **S7.4.2**.

This is a reference/address model, not a new mutable reference-cell type.
It does not introduce aliasing, object identity, or a `ref` equality relation;
Lambda retains structural value equality and copy-on-write value semantics
(**S5.1.4**, **S9.1.5**).

### 2.1 One key hierarchy

Dots expose the same ordered key hierarchy in every domain:

```lambda
a.b.c       // name keys a, b, c
a.1.b       // name key a, integer key 1, name key b
a.'1'.b     // name key a, symbol/name key "1", name key b
```

`IntKey(1)` and `NameKey("1")` are distinct reference keys. A resolver applies
the key according to the current hierarchy:

| Current value/provider | `NameKey` | `IntKey` |
|---|---|---|
| Namespace/module | Exported or visible name | Unsupported → `null` |
| Map/object | Symbol-key member | Unsupported unless its model declares it |
| Array/list/range | Unsupported → `null` | Indexed member |
| Element | Attribute/name member | Child index |
| File/URL provider | Named segment | Canonical decimal segment |

Name-only external providers may project `IntKey(1)` to the segment `"1"`.
That provider projection does not erase the typed distinction in the Lambda
reference: two different references may reach the same external target just
as two filesystem paths may meet through a symlink.

Negative integer steps carry no special upward meaning and remain invalid or
absent according to the target's indexing contract (**S7.2.1**). Upward
navigation is always the explicit `~~` operation.

### 2.2 Static and dynamic references

The source forms share the reference plan but differ in when their root is
known and in what evaluation returns:

| Source form | Reference class | Evaluation contract |
|---|---|---|
| `/.a.b` | Rooted access | Reads the node from the logical root, local by default |
| `\.a.b` | Static relative reference | Retains the active relative base and produces a lazy `path`/target handle |
| `file.localhost.a.b`, `file.host.a`, `http.host.a` | Absolute access | Returns the concrete node. File-kind observations use metadata (S2.4.3v4, D10.1.8) |
| `\.file.users.peter.doc` | Escaped absolute reference | Captures a `path` without looking up the target |
| `p#` | Path dereference | Evaluates the address and returns the target node |
| `node[r]` with a relative path `r` | Relative traversal | Applies the steps from the supplied node (S8.2.1v5) |
| `a` | Static binding reference | Resolves through lexical/import namespace selection and reads the binding |
| `'a'` | Static name-key reference value | Produces the symbol key; it is not implicitly dereferenced |
| `a.b` | Dynamic member reference | Resolves `a`, then applies `NameKey("b")` to that runtime value |
| `a.1` | Dynamic indexed member reference | Resolves `a`, then applies `IntKey(1)` |
| `a[k]` | Dynamic-key reference | Resolves both the base and key at runtime |
| `a./`, `a./.b` | Dynamic root reference | Resolves `a` as an occurrence, selects its hierarchy root, then optionally continues |
| `a.~~` | Dynamic parent reference | Resolves `a` as an occurrence, then selects its parent |

A bare absolute and its escaped counterpart use the same provider selection.
With `let d = file.users.peter.doc` and `let p = \.file.users.peter.doc`,
`d is file` and `p# is file` can both be answered from metadata. If the
document exposes a body, `d.body`, `d[\.body]` and `(p ++ \.body)#` select it.
The absolute address can also name a destination that has yet to be created.
Duplicating or passing a reference never requires the target to exist
(S2.4.3v4, S2.4.4v2, S8.2.1v5, S10.6.1v3, D10.1.8).

Member expressions are semantically dynamic even when their key token is
static: their base is a runtime value. An optimizer may prove a namespace,
shape slot, or fixed index and lower the lookup statically, but the result must
remain identical to the dynamic resolver (**S1.6**).

### 2.3 Namespace qualification

A bare name is statically qualified by ordinary lexical/import resolution:

```text
a  --current namespace is ns-->  ns.a
```

In an element tag or attribute name position, namespace qualification is
maximal. Once `ns.name` begins, the complete dotted name is consumed before
element content is considered; whitespace is only a grammar extra and does not
end the name. Therefore `<svg.rect>` and `<svg .rect>` both name `svg.rect`.
Relative-path content is spelled `<svg \.rect>` and needs no boundary at all
(PTH16v3, S2.4.3v3): S16.9.4 respelled the relative path `\.a.b`, so `.rect`
can no longer be a path and there is nothing left to disambiguate. The
S16.9.3 boundary comma is a biconditional, so with no attributes present a
comma is forbidden — `<svg, \.rect>` is an error, as is the retired
`<svg; .rect>`. This keeps a qualified name distinct from the relative-path
plan without a contextual scanner token or a runtime resolution guess.

Lexical shadowing chooses the namespace/binding before lowering; it is not a
runtime search through every namespace. Identifier spelling and symbol
spelling produce the same `NameKey` identity when used as a key in the same
identity scope; string subscripts normalize by the same exact contents,
consistent with **S8.2.2v2** and **D5.6.1**.

A standalone symbol still evaluates to its key value. When a symbol is used as
a static reference head, its namespace supplies the root just as it does for
an identifier:

```text
'a' --used as a reference head in ns--> ns.'a'
```

This preserves the difference between naming a key and reading a binding while
giving both spellings the same globally qualifiable reference identity.

The canonical internal identity is `(NamespaceId, NameId)` plus remaining
typed keys, not a `String*` address. Raw `NameId` values are scoped and are not
portable serialization. Persisted/linked references carry their qualified
root and key spellings and are resolved into the receiving NamePool, following
**D5.6.2**.

Every reference is globally resolvable *within its resolution universe*: the
linked program/module graph, its identity pools, and the active
`ResolverContext`. Private lexical roots remain inaccessible outside their
scope; exported module/provider roots can be serialized or linked by qualified
name. Global resolution never bypasses lexical visibility, module exports,
sandbox policy, or capabilities.

### 2.4 The logical `/` root and mounts

`/` is the logical global reference root. It is not intrinsically a local
filesystem root. The active `ResolverContext` deterministically qualifies a
rooted reference through an immutable mount table:

```text
mount / -> file.localhost
/.a.b   -> file.localhost.a.b

mount / -> file.hostname
/.a.b   -> file.hostname.a.b

mount / -> http.hostname
/.a.b   -> http.hostname.a.b

mount /.api -> http.api_host.v1
/.api.users -> http.api_host.v1.users
```

Longest-prefix mount selection is allowed so one logical hierarchy can join
multiple providers. A matched logical prefix is replaced by its qualified
target prefix and the unmatched typed-key suffix is appended. Mount aliases
are expanded transitively at context construction/link time; cycles are an
error.

Resolution is deterministic within one `ResolverContext`: it never probes the
filesystem and then falls back to HTTP, depends on resource existence, or
changes because a request happened to fail. A different application/isolate
may deliberately install a different `/` mount, just as it may install a
different module/import environment.

The resolver belongs to the isolate's canonical `EvalContext` (**D6.4.1**) and
is immutable during one evaluation. There is no mutable process-global
namespace or cross-isolate ambient registry. Frozen provider catalogs may be
process-global under **D6.4.4**; the context-specific root/namespace bindings
may not be.

### 2.5 Absolute provider paths

The hostname occupies the same position in a file URL and a Lambda access
expression (S2.4.5v6). A named authority remains meaningful even when the
provider cannot reach that machine. Selecting a host never falls back to
looking for a local directory of the same name.

| Lambda access | File URL on the default local mount | Meaning |
|---|---|---|
| `/.users.peter.doc` | `file:///users/peter/doc` | Ordinary local absolute access via the logical root |
| `file.localhost.users.peter.doc` | `file://localhost/users/peter/doc` | Explicit local file access, independent of the mount |
| `file./.users.peter.doc` | `file:///users/peter/doc` | Alias for explicit local file access |
| `file.users.peter.doc` | `file://users/peter/doc` | Host `users`, with absolute path `/peter/doc` |
| `file.local.peter.doc` | `file://local/peter/doc` | Host named `local` |

RFC 8089 gives `localhost` the meaning of an empty file authority. Lambda's
dotted grammar uses the root step `./` to represent the empty authority.
`file./.path` corresponds to `file:///path`, and `file.localhost.path`
corresponds to `file://localhost/path`. Both select the same local file. [RFC 8089 §2](https://www.rfc-editor.org/rfc/rfc8089.html#section-2)

On Windows, `file.myhost.C.path` selects the UNC share `\\\\myhost\\C\\path`,
including when `myhost` names the current machine. `file.localhost.C.path`
selects the local `C:\\path`. A UNC address with `localhost` as its server
captures the current machine's named authority (S2.4.5v6).

The same authority rules apply inside the escape. `\.file.localhost.a.b`
captures an address that can be compared or passed as an output destination
without a filesystem lookup (S2.4.1v3, S2.4.4v2). `\.file./.a.b` captures
the same address. Source printing preserves the written authority spelling.
Composition and root selection keep that spelling. Both spellings use the
same equality and hash identity (S2.4.5v6).

### 2.6 Resolution is not forcing

Reference resolution has two stages:

1. **Address resolution** canonicalizes roots, aliases, mounts, and typed
   keys into a qualified reference. It is deterministic and performs no I/O.
2. **Value/target forcing** asks the selected namespace, container, file, URL,
   or other provider for a value. In-memory reads follow **S7.1**; external
   effects retain their declared error/effect contract.

This separation lets `/.a.b` resolve to `file.localhost.a.b`,
`file.hostname.a.b`, or `http.hostname.a.b` without reading a file or issuing
a request. It also keeps the unified reference scheme from becoming an
implicit-I/O loophole in the `fn`/`pn` effect model.

## 3. Vocabulary

The syntax has three distinct concepts:

| Concept | Spellings | Meaning |
|---|---|---|
| Path root | `/`, `\`, `SchemeName` | Logical rooted, relative, or explicit absolute provider root |
| Context atom | `~`, `~~`, `^` | Current value, parent of current value, or current handled error |
| Step | `.name`, `.1`, `.'quoted'`, `.*`, `.**`, `./`, `.~~`, `[expr]` | Name/int key, wildcard, root, parent, or dynamic navigation |

`~` retains its current-value meaning under **S10.1.2–S10.1.3**. `^`
retains its handler-local current-error meaning under **S7.6.1v4** and
**S7.6.5v2**. Their ability to take ordinary dotted or indexed steps is a
surface-shape consistency, not a new shared runtime meaning.

## 4. Surface grammar

The intended grammar, abstracting the existing set of legal member names, is:

```ebnf
PathLiteral       ::= EscapedAbsolute | RelativePath
EscapedAbsolute   ::= "\." AbsolutePath
AbsoluteAccess    ::= RootedPath | AbsolutePath

RootedPath        ::= "/" PostfixStep*
RelativePath      ::= "\" PostfixStep*
AbsolutePath      ::= SchemeName "." PathPart PostfixStep*

PathPart          ::= MemberName
                    | NonNegativeInteger
                    | QuotedSymbol
                    | Wildcard
                    | RootStep
                    | ParentStep

Wildcard          ::= "*" | "**"
RootStep          ::= "/"
ParentStep        ::= "~~"

PostfixStep       ::= DottedStep | IndexStep
DottedStep        ::= "." PathPart
IndexStep         ::= "[" Expression "]"
PostfixRootExpr   ::= PrimaryExpr "." "/" PostfixStep*
ParentCurrentExpr ::= "~~"                 // desugars to ~.~~
```

`/` reads the logical root node and `\` is the empty relative path.
`file.localhost` denotes the root of the local file provider. Empty
steps and a trailing dot separator are not valid: `/.`, `\.`, `\.a.`, and
`/.a..b` are errors. A trailing dot is an incomplete expression, so a step on
the next line continues it (S16.2.1): `\.` ⏎ `a` is `\.a`, as `\. a` is
(S16.1.1). A line-start `.step` other than `.digit` continues a path too
(S16.2.4v3).

An `IndexStep` is the dotted step with a computed key (**PTH13.1**,
**S2.4.2v5**): `/[1]` is `(/)[1]`, the same rooted access as `/.1`, and `\[1]` is
`\.1`. The same holds for a path held in a variable, `p[1]` ≡ `p.1`; a
subscript never forces the target. Inside a path literal every step is a key
step, including the steps after a computed one: `\[1].name` is `\.1.name`,
never the path's `name` property. A dot is always followed by a `PathPart`,
so `.[` is an error anywhere — `/.[1]`, `\.[1]`, `a.[1]` — like `/.`. An
integer may be the first key (**PTH13.2**): `\.1` and `/.1` are `IntKey(1)`,
and `\.1.2` is keys 1 then 2. Only syntax can tell `/.1` from `x /.5`
(division by 0.5), so the parser, not the lexer, re-reads `.1` after a
leading root `/` as a step; after `\` the lexer can tell on its own.
A segment containing a dot or other non-name character remains quoted:

```lambda
/.'var'.log.'app.log'       // /var/log/app.log
\.~~.data.'config.dev.json' // ../data/config.dev.json
```

Dynamic steps retain bracket notation:

```lambda
let part = 'daily'
/.var.log[part].'app.log'
```

In `\.file.users` the unquoted scheme head selects the file provider.
A computed or quoted first step selects a relative key: `\['file'].users`.
A path printer must preserve this distinction, including for dynamically
constructed paths. The absolute roots use the same operations.
For `file`, the operation immediately after the scheme distinguishes local
root `./` from an explicit hostname. The implementation may keep parsing
absolute forms through the ordinary
identifier/member grammar and classify the chain during AST construction; the
EBNF states the semantic result rather than requiring a new lexical token.
This decision does not otherwise redesign scheme registration.

## 5. Canonical source spelling

Printers, formatters, diagnostics, and generated Lambda source emit only the
new canonical forms:

| Meaning | Canonical | Retired |
|---|---|---|
| Rooted keys `a/b` | `/.a.b` | `/a.b` |
| Relative keys `a/b` | `\.a.b` | `.a.b` (S16.9.4) |
| Absolute local-file keys `a/b` | `file.localhost.a.b` | none |
| Absolute remote-file keys `a/b` on `host` | `file.host.a.b` | none |
| Absolute HTTP keys `host/a/b` | `http.host.a.b` | unchanged |
| Root of `value` | `value./` | none |
| Key `b` from root of `a` | `a./.b` | none |
| Relative `../a/b` | `\.~~.a.b` | `..a.b`, `.~~.a.b` |
| Relative `../../a` | `\.~~.~~.a` | `(..)..a`, related compound forms, `.~~.~~.a` |
| Parent of `value` | `value.~~` | `value ..` |
| Two parents of `value` | `value.~~.~~` | `value .._..` |

The leading `\` distinguishes a relative reference from contextual
parent navigation:

```lambda
\.~~.a      // relative reference; file projection is ../a
~~.a        // parent of contextual ~, then a
```

This distinction is syntactic and does not require type-directed parsing.

## 6. Path value model

A path value is semantically:

```text
Path = (root, ordered steps)
```

`root` is logical-global, relative, or an explicit absolute
namespace/provider scheme. A step is a name key, integer key, dynamic key,
wildcard, recursive wildcard, root, or parent operation. Root and parent are
part of the path algebra; neither introduces another path form.

### 6.1 Root and parent normalization

The postfix root operation discards descendant steps back to the current
hierarchy anchor (**S2.4.2v5**):

1. At the logical-global root, the anchor is `/`.
2. At an absolute provider path, the anchor retains the provider and selected
   authority: `file.localhost`, `file.hostname`, or `http.hostname`.
3. At a relative root, the anchor is not known until resolution. Static
   normalization discards preceding relative child steps but retains `\./`
   as the unresolved root-selection operation.

For captured references:

```lambda
\.a.b./.c                      == \./.c
\.file.localhost.a.b./.c       == \.file.localhost.c
\.file.hostname.a./.b          == \.file.hostname.b
\.http.hostname.a./.b          == \.http.hostname.b
```

Parent steps are applied from left to right:

1. If a removable child step precedes `~~`, remove that child.
2. At the relative root, retain `~~` as one outward traversal.
3. If the relative path already consists only of retained parent steps, append
   another parent step.
4. At the logical-global or an absolute provider/authority root, clamp to that
   root.

Consequently:

```lambda
\.file.localhost.a.b.~~       == \.file.localhost.a
\.file.localhost.a.~~.b       == \.file.localhost.b
\.file.localhost.~~           == \.file.localhost

\.a.~~            == \
\.a.~~.~~.b       == \.~~.b
\.~~.~~           // ../../
```

Normalization is semantic, not merely cosmetic: equality, hashing, target
resolution, and canonical printing observe the normalized path. It must not
depend on whether a path was literal, dynamically composed, or returned by a
function (**S1.6**).

### 6.2 Composition

`base ++ suffix` accepts a relative path suffix and applies its steps to the
base from left to right. Root and parent steps therefore affect the base rather
than being discarded as relative scheme markers:

```lambda
\.file.localhost.home.user ++ \.docs.file      == \.file.localhost.home.user.docs.file
\.file.localhost.home.user ++ \.~~.shared      == \.file.localhost.home.shared
\.file.host.home ++ \.docs./.tmp  == \.file.host.tmp
\.work.build ++ \.~~.src        == \.work.src
\.work.build ++ \.cache./.src   == \./.src
```

A rooted or absolute suffix remains an error where composition requires a
relative suffix. A bare `\` suffix is the identity. With a path on the right,
a concrete receiver is a type error (S10.6.1v3). Use `node[r]` to traverse
from that receiver. A bound relative selector has the same meaning as an
inline escape, even when the ambient relative base points elsewhere (S8.2.1v5).

### 6.3 Provider and OS conversion

Logical source notation and provider notation remain separate. Explicit
absolute paths project directly through their named provider:

```text
file.localhost.a.b      <-> /a/b on the current machine
file.host.a.b   <-> /a/b on machine host
http.host.a.b   <-> http://host/a/b
https.host.a.b  <-> https://host/a/b
```

Rooted and relative paths first select their semantic base. When `/` is
mounted to a local file provider:

```text
/.a.b       -> file.localhost.a.b -> /a/b
\.a.b       <-> ./a/b
\.~~.a.b    <-> ../a/b
```

If `/` is mounted to HTTP or another provider, the same logical reference is
projected through that provider instead. Platform conversion happens only at
the target boundary. Windows drive and separator handling remain target-layer
concerns and do not alter Lambda source syntax.

### 6.4 Wildcard steps and forcing (PTH81–PTH84; S2.4.6, S8.2.5, S8.2.6)

`*` and `**` are steps like any other (**S2.4.2v5**), so a wildcard may sit
anywhere in a captured path: `\.src.*.name`, `\.file.localhost.*.b`, `\.src.**.**`. Building such a path reads
nothing (§2.6); it is a lazy reference with a query in it, and `#` forces it
like any path (PTH32).

**Per-match continuation (PTH81).** Forcing a wildcard enumerates the key
domain the step lands in and applies the remaining steps to each match,
set-at-a-time (**S8.2.6**). In the file-system domain `*` yields one level of
directory entries; `**` is the closure of `*` (**S8.2.5**) — the base itself,
then every entry beneath it, files and directories alike, in pre-order, to
any depth. A match that does not exist forces to `null` under **S7.1.1v3**
and drops.

**File and directory results (S2.4.6v2, S2.4.7v2).** Directory enumeration
produces concrete entries. `\.src.**#` enumerates them in preorder, including
the base, and `\.src?file#` selects descendant files. Neither operation needs
file payload bytes. `\.src.**.**#` also traverses file contents. Keep the
first occurrence of each normalized directory-entry address and preserve
document runs with their duplicates. The type operand of a captured query
participates in its identity. Steps following `#` operate on the actual
values returned, just as they do on a bound node (S2.4.3v4).

## 7. Root and parent navigation on expressions

### 7.1 Postfix root step

`value./` is a left-associative postfix root-selection step at the same
precedence as member access, indexing, and `.~~`:

```lambda
value./
value./.name
value./[i]
a./.b
~./.name
value.~~./.name
```

The root step may be terminal (`a./`) or followed immediately by any ordinary
postfix step (`a./.b`, `a./[i]`, `a./.~~`).

It invokes the root relation supplied for that value occurrence:

| Value/context | Root resolution |
|---|---|
| `path` | The provider/authority or logical anchor in §6.1 |
| A traversal/query occurrence | The outermost occurrence from its navigation path or zipper |
| A root-aware object/model | That model's declared root resolver |
| A standalone hierarchical value | The value itself |
| No root relation | `null` |

Missing root navigation is a total read and returns `null`; subsequent access
follows ordinary null propagation (**S7.1.1v2**). Initial `/` and postfix `./`
therefore retain one root-selection concept under **S1.7**: the initial form
has the resolution universe as its implicit base, while the postfix form has
an explicit runtime or path base (**S10.5.1–S10.5.2**).

### 7.2 Postfix parent step

`value.~~` is a left-associative postfix step at the same precedence as
ordinary member access and indexing:

```lambda
value.~~.name
value.~~[i]
value.~~.~~.name
```

It invokes the parent relation supplied for that value occurrence:

| Value/context | Parent resolution |
|---|---|
| `path` | The path-parent algebra in §6.1 |
| A traversal/query occurrence | The enclosing occurrence from its navigation path or zipper |
| A parent-aware object/model | That model's declared parent resolver |
| No parent relation | `null` |

A field literally named `parent` remains an ordinary `.parent` member.
`.~~` denotes the parent relation; a model may implement that relation using
its `parent` field, but the two spellings are not syntactic aliases.

Missing parent navigation is a total read and returns `null`; subsequent
access then follows ordinary null propagation (**S7.1.1v2**). Logical-global and
absolute provider/authority roots are the path-specific exception: their
parent operation clamps to the same root so path normalization remains closed.

### 7.3 Contextual parent atom

Bare `~~` is not a second operation. It desugars before typing to:

```lambda
~~  == ~.~~
```

It is valid exactly where `~` is valid, uses the innermost `~` binding, and
inherits all of `~`'s lexical shadowing rules (**S10.1.3**):

```lambda
~~.a          == ~.~~.a
~~.~~.a       == ~.~~.~~.a
```

For the pipe syntactic test in **S10.1.2**, a free `~~` contains an implicit
free `~` and therefore makes the pipe a mapping pipe. Outside a current-value
context, bare `~~` is the same semantic error as bare `~`; it does not mean a
relative path. That form starts with `\`, as in `\.~~`.

## 8. Context and ownership invariant

Contextual root and parent navigation are occurrence-based. Equal Lambda
values do not acquire identity merely because they appear in different
locations. A document/query/view traversal that supports `./` or `.~~` must
carry its lineage in a navigation path, cursor, or zipper owned by the
evaluation context.

It must never be implemented by adding mutable parent/root pointers to Lambda
containers or document values. Such pointers would make construction order,
copy-on-write sharing, and value equality observable, contradicting
**S1.4**, **S1.6**, **S9.1**, and **S9.3**. This decision closes the root and
upward-parent navigation parts of **SO19**; the spelling and semantics of
lateral axes remain open under that ID and must obey the same no-lineage-pointer
invariant.

## 9. Parser and AST consequences

The grammar should express the surface directly:

- classify a registered `SchemeName` at the head of a dotted member chain as
  an absolute access expression, and the escaped chain as an absolute path value; the existing member grammar may remain the concrete parse
  route;
- recognize `file` alongside `http`, `https`, and `sys` in that absolute-path
  classification;
- allow `/` as a root step after `.` in both path and general postfix chains,
  producing `./`; keep bare infix `/` as division;
- add a `~~` parent-step token with longest-token handling alongside `~#` and
  `~`;
- allow the parent step after `.` in both path and general postfix chains;
- add bare `~~` as the contextual-parent primary;
- make rooted path literals consume `/` followed only by dotted steps;
- remove `path_parent: '..'`, the special `_..` repetition, and the
  `_expr`/`parent_expr` conflict;
- preserve `...` exclusively as the variadic token.

The AST needs a root-navigation operation shared by path steps and `value./`.
It may likewise share one parent-navigation node across `value.~~` and the
desugared bare form. Path literals need typed root and parent steps so
normalization and composition do not lose either operation.

## 10. Runtime representation invariant

The runtime must represent a reference plan with a typed root and typed
operations. Ordinary keys share one `RefKey` sum (`NameId` or non-negative
integer); root, parent, and wildcard navigation remain operation kinds. Static
plans may be pool-owned compiler artifacts, while dynamic plans carry a rooted
runtime base/occurrence.

Root and outward traversal must be operations/typed segments, not solely
special scheme roots. A root-only encoding cannot faithfully represent
mid-chain `./`, repeated parents, or either operation during composition.

The intended migration is:

- relative path root remains `PATH_SCHEME_REL`;
- retain `PATH_SCHEME_FILE`, `PATH_SCHEME_HTTP`, `PATH_SCHEME_HTTPS`, and
  `PATH_SCHEME_SYS` for explicit absolute paths;
- add a distinct logical-root representation for `/`; `PATH_SCHEME_FILE`
  must distinguish logical root selection from explicit local provider selection;
- represent provider authority explicitly: at minimum local/current-machine
  authority versus a named hostname for `file`;
- add a root path-segment kind (for example `LPATH_SEG_ROOT`);
- add a parent path-segment kind (for example `LPATH_SEG_PARENT`);
- migrate and retire `PATH_SCHEME_PARENT` as a semantic root;
- resolve the logical `/` root through the `ResolverContext`, never as an
  unconditional alias for `PATH_SCHEME_FILE`;
- resolve namespace heads through `(NamespaceId, NameId)` and dynamic members
  through the same typed-key walker/provider protocol;
- route literal construction, `++`, root/parent access, printing, hashing,
  target conversion, the interpreter, and MIR Direct through the same
  path-navigation helper;
- do not extend the frozen C2MIR path (**D1.6**).

This is a representation requirement, not permission for separate literal and
runtime behaviors: a constructed path and an equal literal must normalize and
print identically.

### 10.1 Pre-implementation audit

Audit date: 2026-08-18.

| Layer | Current state | Design gap |
|---|---|---|
| Tree-sitter grammar | Dotted scheme forms parse through the ordinary `identifier` + `member_expr` grammar; there is no dedicated absolute-path production. `/` is only an initial path prefix or infix division. | Add contextual `./` postfix-root parsing without changing division. |
| AST classification | `http`, `https`, and `sys` at the head of a member chain become `AST_NODE_PATH_EXPR`. | `file` is explicitly excluded, and neither paths nor members have a typed root operation. |
| Rooted path lowering | `/a.b` currently lowers directly to `PATH_SCHEME_FILE`. | New `/.a.b` needs a distinct logical root; otherwise rooted and `file./.a.b` cannot coexist. |
| MIR Direct | `AST_NODE_PATH_EXPR` lowers through `path_new(pool, scheme)` plus typed path-extension calls. | It can reuse this path once the new logical root and `file` AST classification exist. |
| Runtime schemes | `PATH_SCHEME_FILE`, `HTTP`, `HTTPS`, and `SYS` and target-scheme conversion already exist. | File paths need explicit local/named authority, and the printer must distinguish `file./.a.b`, `file.host.a.b`, and logical `/.a.b`. |
| Provider behavior | Filesystem and `sys.*` resolution are implemented; HTTP/HTTPS target and `input()` dispatch paths exist. | Provider behavior is not yet uniform: generic remote `exists()` is unsupported, and dotted HTTP-path forcing lacks a conformance test. |
| Tests | Path unit tests cover runtime file/http/sys roots and URL projection; Lambda scripts cover `http.*`, `https.*`, and `sys.*` path values. | No source test covers `file./`, `file.hostname`, logical `/.`, postfix `a./`/`a./.b`, or root-operation normalization. |

This table records the baseline that motivated the implementation. The shipped
runtime now covers the decided syntax and the corresponding parser, AST,
interpreter, MIR Direct, normalization, printing, target qualification, and
current-machine file-authority checks. The intentionally deferred pieces are
the general immutable mount-table resolver, remote file transport, and network
hostname discovery; the default resolver maps logical `/` to local `file./`.

**Probed 2026-09-21** (after the `\.a.b` respelling): multi-step relative
paths, parent/root normalization, `++` composition, and printing all use
`\.`. The probe reported three gaps: the bare roots did not parse ("expected
a path segment" / "expected '.'"); the indexed leading form did not parse;
and `\.1.x` silently evaluated to `file./` — a wrong value, not an error.
(It spelled the gaps `\.`, `\.[1]`, and `/.[1]`. Under the USER rulings of
the same day the relative root is `\` and `.[` is an error anywhere, so the
bare roots are `/` and `\`, and the indexed forms are `\[1]` and `/[1]`.)

**Fixed the same day**, identically on both tiers:

- **Root cause of `file./`:** two path grammars. The C parser accepted `\.1`,
  but build_ast re-parsed the path's source text with a separate character
  parser (a Reduce5 leftover from when Tree-sitter owned paths). That parser
  rejected a leading digit without a diagnostic, and the stand-in node it left
  (scheme 0, `PATH_SCHEME_FILE`) ran as `file./`. `/ .a` failed the same way.
  The re-parser is deleted. `parse_path_slot` reduces only the root token,
  `/` or `\`; every step is an ordinary member or index reduction; and
  build_ast builds the path from those tokens. Scheme chains (`http.h.a`,
  `file./.a`) are recognised structurally, with no text re-parse.
- **One root grammar (S2.4.1v2):** the lexer emits `\` alone as the relative
  root, so `\.a`, `\[1]`, and bare `\` mirror `/.a`, `/[1]`, and `/`. `\.`
  alone and `.[` anywhere are E100. Only a step (`.`, `[`), a separator, a
  closer, or the end may touch a bare root: `/b` — the retired `/a`
  spelling, which a bare `/` briefly let split into `/` and a statement `b` —
  is E100 again, as are `\a` and `/1`. The reference grammar enforces the
  same rule with a zero-width `_root_boundary` token, the way `_num_boundary`
  guards `123abc`. The printer spells the empty relative path `\`, which
  re-parses.
- **Index steps (PTH13.1):** a `[k]` step on a path literal builds
  `AST_NODE_PATH_INDEX_EXPR`, which the CST cleanup (24e74b45ee) had stopped
  constructing. Static steps written after it stay key steps: `\[1].name` is
  `\.1.name`, and `\.a['name']` is `\.a.name`. Both tiers call the runtime
  helpers `fn_path_key` (S8.2.1v4 key normalization; a key naming nothing
  yields null) and `fn_path_step` (null stays null). On a path value,
  `fn_index` delegates to `fn_member`, so `p[1]` ≡ `p.1`. Before this, `p[1]`
  forced the target and indexed its content, which is why `\.a[1]` read
  `null`.
- **Integer steps (PTH13.2):** after a step dot the lexer reads a number as
  an integer key, so `\.1.2` and `a.1.2` are keys 1 then 2. `.digit` also
  continues a chain after `\`, `~`, `~~`, `./`, `.*` and `.**`. That last gap
  had split `file./.1`, `\.~~.1` and `\.a.*.1` into a path plus a juxtaposed
  float 0.1. After a leading `/` the parser rescans `.1`, since `x /.5`
  divides.
- **Line breaks (S16.1.1, S16.2.1, S16.2.4v3):** each broken path was checked
  against its joined line. A trailing `.` continues (`\.` ⏎ `a` is `\.a`,
  like `\. a`), and the step flag survives the break (`\.` ⏎ `1.2` is
  `\.1.2`). A line-start `.~~`, `./`, `.*`, `.**` or `.'sym'` continues like
  `.ident`. A line-start `.digit` or `[` is still an error. An interim build
  made `\.` ⏎ `b` two statements while `\. b` was one path — an S16.1.1
  violation, now gone.
- **`return \.a`** returned null: `\` was missing from the tokens that start
  a return value.

Tests: `test/lambda/path_roots_steps.ls`, `path_line_continuation.ls`,
`member_int_steps.ls`, `proc/return_relative_path.ls`, four negative syntax
fixtures, and S2.4 rows in both S16 conformance harnesses. A parenthesized
path is a value, not a literal, so `(\.a).name` still reads the `name`
property, as `p.name` does for a variable.

## 11. Migration

This is an intentional breaking syntax change. Migration is mechanical but
must be syntax-aware because `..`, `.`, `/`, division, and `...` currently
share lexical neighborhoods.

```text
/a.b             -> /.a.b
..a.b            -> \.~~.a.b
..                -> \.~~
value ..          -> value.~~
value .._..       -> value.~~.~~
.a.b             -> \.a.b         // second step, S16.9.4
```

The transition should update grammar, generated parser, AST builders, MIR
Direct, interpreter, path runtime, formatter/printer, active documentation,
and tests together. A source migration tool may accept the old grammar, but
the language grammar and canonical printer should not retain both spellings.

## 12. Conformance matrix

At minimum, implementation tests must cover:

1. Bare and multi-step rooted/relative paths and canonical printing.
2. `file.localhost` current-machine paths and `file.hostname` named-machine paths,
   including canonical printing and provider projection.
3. `http`, `https`, and `sys` absolute paths.
4. Logical root selection following its mount while explicit local provider access bypasses it.
5. Quoted, wildcard, recursive-wildcard, integer, root, and dynamic steps
   after all three path root forms.
6. `value./`, chained `value./.a`, and root selection after another member or
   parent operation.
7. Root normalization for logical, relative, local-authority, and named-host
   paths, including preservation of the provider/authority anchor.
8. One and repeated `.~~` steps, including root clamping and cancellation.
9. `base ++ \.~~.x` and `base ++ \.a./.x` for rooted and relative bases.
10. `value.~~`, `value.~~.~~`, bare `~~`, and nested current-value scopes.
11. Bare `~~` causing mapping-pipe classification.
12. Missing contextual root/parent returning `null` and chaining totality.
13. Traversal roots/parents carried by a zipper/path with no stored container
    root or parent pointer.
14. Equality/hash/print parity between literal and dynamically composed paths.
15. Rejection of `/a`, `..a`, bare-dot `.a`, `value ..`, `_..`, and accidental `...`
    reinterpretation without rejecting valid `./`.
16. `/` mount qualification to file, HTTP, and prefix-mounted providers,
    including cycle rejection and no existence-based fallback.
17. Bare-name namespace qualification, symbol/name key equivalence, integer
    keys, and the distinction between `a.1` and `a.'1'`.
18. Static-reference and dynamic-member parity, including optimized versus
    generic resolution.
19. Address resolution performing no I/O; external target forcing retaining
    its ordinary effect/error contract.
20. Wildcard steps before further steps: per-match continuation, a step
    entering a file match while `**` alone stops at files, path-domain
    deduplication, `**` including its base, and `#` as the line between
    reference and value (§6.4).
21. Query steps on paths (PTH85): `p?T`/`p.?T` lazy until `#`, directory
    enumeration filtered by `T` with no file read, file matches entered,
    and the `**?T`, `**.?T`, `?T.**`, `.?T.**` crossings.

## 13. Decision ledger

> **Series continues:** PTH30–PTH80 (the `reference` type, the postfix `#`
> force step and fragment sugar, `&expr` address-of, `===`, document-node
> identity, the three-tier update model with `put`/`del` + `before`/`after`/`into`, `commit`/`rollback`, `open`; `temp.` documents; the `~key` respell of `~#`) are proposed in
> [`Lambda_Design_Reference.md`](Lambda_Design_Reference.md) §9; PTH-O2–PTH-O18
> live in its §10. PTH81–PTH84 (wildcard steps, §6.4) are below.

| ID | Decision |
|---|---|
| **PTH1v3** | Rooted access uses `/.a.b`; `/` is the logical global root bound by a resolver context, not intrinsically the filesystem root. |
| **PTH2v4** | Relative references use `\.a.b`. The relative root is `\`, as the logical root is `/`: it selects the active relative reference base, which a file provider projects to its current directory. Its steps are ordinary `.part` and `[key]` steps, so `\[1]` is `\.1` and `\.` alone is an error. *(v4, 2026-09-21, USER; S2.4.1v2.)* |
| **PTH3** | `~~` is the one parent step and is never a path root. |
| **PTH4v2** | Relative parent paths are `\.~~.a`; repeated parents are `\.~~.~~`. *(v2, 2026-09-21, S16.9.4.)* |
| **PTH5** | Expression parent navigation is postfix `.~~`; arbitrary chains use `value.~~.~~`. |
| **PTH6** | Bare `~~` is exactly `~.~~`, inherits `~` scope, and counts as a free `~` in pipe classification. |
| **PTH7** | Parent steps normalize left-to-right; relative-root parents are retained and logical-global/qualified roots clamp. |
| **PTH8** | Relative-path composition applies parent steps to the base; it never discards them as scheme syntax. |
| **PTH9** | Missing contextual parent is `null`; path-root clamping is the closed path-specific rule. |
| **PTH10** | Document/context parents use navigation paths or zippers, never stored parent pointers. |
| **PTH11** | Printers emit only the new forms; old spellings are retired without permanent aliases. |
| **PTH12** | The runtime represents parent traversal as a typed operation/segment, not solely `PATH_SCHEME_PARENT`; C2MIR remains frozen. |
| **PTH13** | Every hierarchical address is a typed root plus ordered operations; ordinary keys are `NameKey` or `IntKey`. |
| **PTH13.1** | A subscript on a path is the same key operation as the dotted step and never forces the target: `p[k]` ≡ `p.k`, so `(/)[1]`, `/[1]`, and `/.1` select one rooted node, while `\[1]` and `\.1` capture the same relative path. Inside a path literal every step is a key step: `\[1].name` is `\.1.name`, never a property read. A dot is always followed by a step, so `.[` is an error anywhere (`/.[1]`, `\.[1]`). *(USER, 2026-09-21; S2.4.2v5.)* |
| **PTH13.2** | An integer key may be a path's first step: `\.1` and `/.1` are `IntKey(1)`. `.1` is the float 0.1 only outside a step position (S4.3.1). *(USER, 2026-09-21; S2.4.2v5; resolves PTH-O1's residue.)* |
| **PTH14** | Names and symbols share `NameKey` identity in one scope; integer keys stay typed and `a.1` differs from `a.'1'`. |
| **PTH15** | Paths, names, and symbols are static reference plans; member expressions have a runtime base and are dynamically resolved. Their forcing policies remain distinct. |
| **PTH16v3** | A bare name is statically qualified to its selected lexical/import namespace (`a` → `ns.a`); in element tag/attribute name position the complete dotted namespace-qualified name is maximal. Relative-path content is spelled `<svg \.rect>` and takes **no** boundary delimiter — S16.9.4 respelled the relative path `\.a.b` (dissolving the ambiguity that motivated the old `;`), and the S16.9.3 boundary comma is a biconditional that forbids a comma when the element has no attributes. Both `<svg; .rect>` (v2's spelling) and `<svg, \.rect>` are errors. Global resolution never bypasses visibility or exports. *(v3, 2026-08-28, with S2.4.3v3.)* |
| **PTH17** | `/` and namespace aliases are deterministically qualified by an immutable per-context mount/resolver table; longest-prefix mounts are allowed and alias cycles reject. |
| **PTH18** | The resolver is isolate/EvalContext-owned and capability-aware; no mutable process-global namespace or existence-based provider fallback participates in semantics. |
| **PTH19** | Address resolution is pure canonicalization and performs no I/O; external target forcing remains a separate effectful operation. |
| **PTH20** | Static specialization and dynamic lookup use one semantic typed-key/provider protocol and must produce identical results. |
| **PTH21v3** | Addresses have logical, relative and absolute roots. `\.http.host.a` captures an absolute reference and `http.host.a` reads its node (S2.4.5v6). |
| **PTH22** | An absolute path names its provider and authority directly, bypasses the logical `/` mount, and uses the same typed operations as rooted and relative paths. |
| **PTH23** | A registered scheme name at the head of a dotted chain is reserved as an absolute-path root; a dedicated grammar production is optional if AST classification is equivalent. |
| **PTH24** | Rooted and absolute paths retain distinct root kinds and structural path values even when resolution maps them to the same qualified target. |
| **PTH25** | `/` is the single root-selection operation: initial `/` selects the logical resolution root, while postfix `./` selects the root of its explicit base. |
| **PTH26** | `value./` is a normal postfix navigation step at member precedence and may be followed by the same dotted/indexed operations as any other reference. |
| **PTH27v5** | `file.hostname.path` corresponds to `file://hostname/path`. `file./.path` represents the empty authority in `file:///path` and aliases `file.localhost.path`. The source printer preserves either spelling. On Windows, a named host selects its UNC share even when it is the current host; `localhost` selects the local drive provider. `/.path` uses the logical root, local by default (S2.4.5v6, RFC 8089 §2). |
| **PTH28v2** | A root operation discards descendant steps while preserving the logical or provider/authority anchor; unresolved relative root selection remains as `\./`. *(v2, 2026-09-21, S16.9.4.)* |
| **PTH29** | Dynamic root navigation is occurrence-based and uses a navigation path or zipper; it never adds observable root/parent pointers to Lambda values. |
| **PTH81** | A wildcard step may sit anywhere in a path and the path stays a lazy reference until `#`. Forcing enumerates the key domain the step lands in and continues the remaining steps per match, set-at-a-time (S8.2.6); `*` is one level, `**` the closure of `*` (S8.2.5) — the base, then every entry beneath it, files and directories alike, pre-order, any depth. *(USER, 2026-10-20; S2.4.6.)* |
| **PTH82** | A step applied to a file match enters the file (PTH34 per match); `**` never crosses the file boundary by itself. A force reads exactly the files that a spelled step enters: `\.src.**#` reads nothing, `\.src.**.name#` reads every file, `\.src.**.**#` is the explicit full-content search. *(USER, 2026-10-20; S2.4.6.)* |
| **PTH83** | A wildcard result in the path domain is a set, deduplicated by path equality in first-appearance order, so `**.**` is idempotent on the directory side; results inside a document stay runs and never deduplicate. *(USER, 2026-10-20; S2.4.6.)* |
| **PTH84v2** | Dereference returns the selected entries. Following steps use ordinary value access (S2.4.6v2). |
| **PTH85v2** | A directory query filters concrete entry nodes by type and crosses into file payloads only when a further step requires them (S2.4.7v2). |
| **PTH86** | Provider access returns its concrete node. The escape captures an absolute or relative address, `#` reads it, `++` composes a relative suffix, and `node[relativePath]` reads from its receiver (S2.4.1v3–S2.4.5v6, S8.2.1v5, S10.6.1v3, S16.9.4v2, D10.1.8). |

## 14. Open issues and non-decided options

### PTH-O1: leading relative integer key and string-key input — RESOLVED

A path's first key may be an integer, spelled directly (**PTH13.2**): `\.1`
and `/.1` are `IntKey(1)`. **S4.3.1** keeps `.1` the float 0.1 only outside a
step position. The indexed form names the same path, because a subscript on a
path is the dotted step (**PTH13.1**): `\[1]` ≡ `\.1` and `/[1]` ≡ `(/)[1]` ≡
`/.1`. `.[` is an error anywhere, so `\.[1]` and `/.[1]` are rejected. The symbol spelling `\.'1'` deliberately remains
`NameKey("1")` under **S2.4.2v5** and **PTH14**. *(USER, 2026-09-21. Earlier
wording in Appendix S.2.)*

The former `StringKeyInput` option is rejected by **S8.2.1v2** and C5.3b.
Strings are names, not a second spelling of numeric keys: `"1"` normalizes to
`NameKey("1")` on a map or element's attribute face and is an invalid member
key on an array: reads yield `null`, and writes hard-raise. It is never parsed
into `IntKey(1)`. The empty string is
likewise a valid `NameKey`, resolving the former SO30 uncertainty.

Double-quoted dotted member syntax remains unselected. If introduced later,
it must preserve the string-to-`NameKey` rule and cannot restore numeric-string
coercion. The decided model remains `NameKey | IntKey`; ordinary string and
symbol inputs normalize to `NameKey`, while only exact integral numeric inputs
normalize to `IntKey`.

## Appendix S — Superseded Rulings

Superseded ledger wording, kept for the record only. **Nothing here is
normative.**

### S.1 Bare-dot relative spelling — SUPERSEDED 2026-09-21 by S16.9.4

The relative path was respelled `\.a.b` (Design_Syntax §7.15) because `.name`
at line start is member continuation (S16.2.4v2) and `./a.b` would collide
with the postfix root step.

| ID | Superseded wording | Replaced by |
|---|---|---|
| PTH2v2 | ~~Relative references use `.a.b`; `.` selects the active relative reference base, which a file provider projects to its current directory.~~ | PTH2v3 |
| PTH4 | ~~Relative parent paths are `.~~.a`; repeated parents are `.~~.~~`.~~ | PTH4v2 |
| PTH21 | ~~Paths have three root forms: logical rooted `/.a`, relative `.a`, and explicit absolute `SchemeName.a` such as `file./.a` or `http.host.a`.~~ | PTH21v2 |
| PTH28 | ~~A root operation discards descendant steps while preserving the logical or provider/authority anchor; unresolved relative root selection remains as `./`.~~ | PTH28v2 |

### S.2 Leading integer key only by index — SUPERSEDED 2026-09-21 by PTH13.1–PTH13.2

The USER ruled `\.1` a valid relative path and `/.1` the same path as
`(/)[1]`, so a subscript on a path became the dotted step (PTH13.1) and an
integer may be the first key (PTH13.2).

| ID | Superseded wording | Replaced by |
|---|---|---|
| PTH-O1 | ~~Lambda assigns `.1` to the float literal `0.1` under **S4.3.1**; it must remain a float. A relative path beginning with `IntKey(1)` therefore uses the indexed form `\.[1]`. The symbol spelling `\.'1'` deliberately remains `NameKey("1")` under **S2.4.2v4** and **PTH14**. *(Respelled 2026-09-21 under S16.9.4. The `\` prefix removes the float collision that motivated `[1]`; whether `\.1` may now spell `IntKey(1)` directly is unruled.)*~~ | PTH13.1, PTH13.2 |

### S.3 `\.` as the relative root — SUPERSEDED 2026-09-21 by PTH2v4

The USER ruled `\.[1]` invalid like `/.[1]` (`.[` is an error everywhere)
and `\[1]` the relative index step, which makes `\` — not `\.` — the root.

| ID | Superseded wording | Replaced by |
|---|---|---|
| PTH2v3 | ~~Relative references use `\.a.b`; `\.` selects the active relative reference base, which a file provider projects to its current directory. *(v3, 2026-09-21, S16.9.4.)*~~ | PTH2v4 |

### S.4 Directory traversal before concrete node access (2026-10-20)

Superseded by S2.4.6v2 and S2.4.7v2 on 2026-11-18.

**The file boundary (PTH82).** A step applied to a file match enters the
file — the longest-prefix rule of PTH34, applied per match — so once a step
has entered a file the same spellings enumerate the parsed document
(**S8.2.5**). `**` never crosses the boundary by itself. Hence a force reads
exactly the files that a spelled step enters:

| Spelling | Reads | Yields |
|---|---|---|
| `\.src.**#` | nothing | the base and every path beneath it |
| `\.src.**.name#` | every file beneath src | each file's `name` member; a directory's `name` child when it exists |
| `\.src.**.*#` | every file | the top-level items of every file |
| `\.src.**.**#` | every file | every value in every file — the explicit full-content search |

*Why:* a single `**` that read every file beneath a tree would be an
unbounded implicit read (**S2.4.4**, PTH19, PTH38) and would hit the
performance wall on every recursive listing; the crossing must be spelled,
and then its cost is visible in the spelling.

**Path-domain results are sets (PTH83).** Paths have structural identity
(**S2.4.2v5**), unlike document values (SI13, [Expr_Query
§4.1](Lambda_Expr_Query.md#41-return-value--the-run-t-two-subscript-worlds-ruled-2026-09-26-user-s824v3)),
so a wildcard result in the path domain is deduplicated by path equality in
first-appearance order. Without this, `\.src.**.**` would list every file
once per matched ancestor; with it, `**.**` is idempotent on the directory
side, as XPath's `a//b//c` is on node-sets. Results inside a document stay
runs and never deduplicate.

**`#` is the line between reference and value (PTH84).** Steps before `#`
extend the plan and may enter files; steps after it act on values. So
`\.src.*#.name` forces the enumeration and reads each path value's `name`
property, mapped over the run (**S8.2.6**), touching no file, while
`\.src.*.name#` enters each file. The two spellings differ on purpose.

Implementation (2026-10-20): `**` enumeration heads with its base and has no
depth cap; a symlinked directory is listed but not entered, as globstar does,
so a link to an ancestor cannot recurse forever. Before this, the base was
excluded, recursion stopped at depth 16, and `#` refused any wildcard path.

**Query steps follow the wildcard rule (PTH85; S2.4.7).** `?T` and `.?T`
are recursive like `**`, so they get the same boundary. On a path they are
lazy path operations whose type operand joins the path's identity. Forced
over a directory, `p?T` is the proper closure `p.*.**` and `p.?T` the
inclusive closure `p.**`, each filtered to the path values that are `T`;
no file is read, so `\.src?<div>#` is `null` and `\.src?path#` lists every
path beneath `src`. Forced over a file, the query is the step that enters
it and searches the parsed value. Applied to another step's matches, it
enters each file match, so these cross the boundary:

| Spelling | Reads | Yields |
|---|---|---|
| `\.src?T#` | nothing | path values beneath `src` that are `T` |
| `\.src.**?T#`, `\.src.**.?T#` | every file beneath `src` | every `T` inside them (and matching directory paths) |
| `\.src?T.**#`, `\.src.?T.**#` | every file the query keeps | the closure of each kept file's content |
| `\.src.*?T#` | the files directly in `src` | every `T` inside them |
| `\.src.**#?T` | nothing | the path values that are `T`; after `#` the query acts on values |

*Why:* a bare `?T` that opened every file beneath a directory would be the
unbounded implicit read PTH82 rules out for `**`. Spelling a step that
produces file matches first is what makes the cost visible.

### S.5 Explicit current-machine authority (2026-11-18)

PTH27 used `file./.a.b` for the current machine. PTH27v2 added `file.localhost.a.b` in line with RFC 8089 §2. PTH27v3 ratified both spellings. PTH27v4 preserves the written spelling in source output. PTH27v5 selects a UNC share for a named Windows host, including the current host (S2.4.5v6).
