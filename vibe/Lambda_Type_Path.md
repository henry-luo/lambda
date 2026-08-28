# Lambda Path and Unified Reference Semantics

**Date:** 2026-08-18
**Status:** Decided; current syntax/runtime scope implemented
**Decision IDs:** PTH1v2, PTH2v2, PTH3–PTH15, PTH16v3, PTH17–PTH29
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

This record supersedes the surface syntax plus root/parent-navigation portions of
[`Lambda_Expr_Path.md`](Lambda_Expr_Path.md) and
[`Lambda_Expr_Path_Impl.md`](Lambda_Expr_Path_Impl.md). Those files remain
useful implementation/history records, but their `/a`, `..a`, and `value ..`
examples are no longer design authority.

## 1. Decision

Lambda references plus root and parent navigation use one dotted key
hierarchy:

```lambda
/.a.b          // rooted reference; a file mount projects it to /a/b
.a.b           // reference relative to the active reference base
.~~.a.b        // parent-relative reference; file projection is ../a/b
.~~.~~.a       // two parent steps; file projection is ../../a
file./.a.b     // absolute file reference on the current machine
file.host.a.b  // absolute file reference on machine host
http.host.a.b  // absolute HTTP-provider reference

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

The old spellings `/a`, `..a`, `value ..`, and `value .._..` are retired.
There is no permanent compatibility alias.

A path's initial form is exactly one of logical rooted `/`, relative `.`, or
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
- the active relative root `.`;
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
| `/.a.b` | Static rooted reference | Qualifies `/` through the resolver and produces a lazy `path`/target handle |
| `.a.b` | Static relative reference | Retains the active relative base and produces a lazy `path`/target handle |
| `file./.a.b`, `file.host.a`, `http.host.a` | Static absolute reference | Names its provider/authority directly and produces a lazy `path`/target handle |
| `a` | Static binding reference | Resolves through lexical/import namespace selection and reads the binding |
| `'a'` | Static name-key reference value | Produces the symbol key; it is not implicitly dereferenced |
| `a.b` | Dynamic member reference | Resolves `a`, then applies `NameKey("b")` to that runtime value |
| `a.1` | Dynamic indexed member reference | Resolves `a`, then applies `IntKey(1)` |
| `a[k]` | Dynamic-key reference | Resolves both the base and key at runtime |
| `a./`, `a./.b` | Dynamic root reference | Resolves `a` as an occurrence, selects its hierarchy root, then optionally continues |
| `a.~~` | Dynamic parent reference | Resolves `a` as an occurrence, then selects its parent |

Thus *static reference* means the root/key plan is known without inspecting a
runtime base. It does not mean every form auto-dereferences or performs I/O.
Names read bindings, symbols remain key values, and paths remain lazy target
handles. These are distinct forcing policies over one address scheme.

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
consistent with **S8.2.2v2** and **D4.6.1v2**.

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
**D4.6.2v2**.

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
mount / -> file./
/.a.b   -> file./.a.b

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

The resolver belongs to the isolate's canonical `EvalContext` (**D5.4.1**) and
is immutable during one evaluation. There is no mutable process-global
namespace or cross-isolate ambient registry. Frozen provider catalogs may be
process-global under **D5.4.4**; the context-specific root/namespace bindings
may not be.

### 2.5 Absolute provider paths

Lambda paths have three root forms:

| Form | Example | Root selection |
|---|---|---|
| Rooted | `/.a.b` | Starts at logical `/`, then uses the active mount table |
| Relative | `.a.b` | Starts at the active relative reference base |
| Absolute | `file./.a.b`, `file.host.a.b`, `http.host.a.b` | Names the provider and authority directly |

An absolute path is already provider-qualified. It bypasses the logical `/`
mount. Its first token is a registered `SchemeName`; provider-specific
authority selection follows, then name, integer, quoted, wildcard, root,
parent, and dynamic steps use the common operation semantics.

The file provider has two explicit authority forms:

| File form | Authority | Projection |
|---|---|---|
| `file./.a.b` | Current machine | Local filesystem root `/a/b` |
| `file.hostname.a.b` | Machine named `hostname` | That machine's filesystem root `/a/b` |

Here `./` is one postfix root operation, not a dot separator followed by a
second rooted path. `file./` selects the file provider's current-machine root.
An identifier immediately after `file.` instead selects an explicit host, so
`file.a.b` means host `a`, path key `b`; local `/a/b` is spelled
`file./.a.b`.

The current-machine authority is resolved from the execution context and may
differ between machines. An explicit hostname is stable across contexts, but
forcing either form still obeys capabilities and provider availability
(**S2.4.5v2**).

A scheme name is reserved as a path root only when it heads a dotted absolute
path. Thus `http.host.a` is an HTTP path rather than member access on a binding
named `http`. This matches the existing AST interpretation of `http`, `https`,
and `sys`; `file` joins the same set under this decision.

Rooted and absolute path values retain different root kinds:

```lambda
/.a.b       // logical rooted path
file./.a.b  // explicit file path on the current machine
```

They are structurally distinct path values even when the active mount makes
both resolve to the same qualified target. Equality of resolved targets may
therefore agree while equality of the unforced path values does not. This
preserves context-free value equality (**S5.1.4**) without hiding the logical
root's resolver dependency.

### 2.6 Resolution is not forcing

Reference resolution has two stages:

1. **Address resolution** canonicalizes roots, aliases, mounts, and typed
   keys into a qualified reference. It is deterministic and performs no I/O.
2. **Value/target forcing** asks the selected namespace, container, file, URL,
   or other provider for a value. In-memory reads follow **S7.1**; external
   effects retain their declared error/effect contract.

This separation lets `/.a.b` resolve to `file./.a.b`,
`file.hostname.a.b`, or `http.hostname.a.b` without reading a file or issuing
a request. It also keeps the unified reference scheme from becoming an
implicit-I/O loophole in the `fn`/`pn` effect model.

## 3. Vocabulary

The syntax has three distinct concepts:

| Concept | Spellings | Meaning |
|---|---|---|
| Path root | `/`, `.`, `SchemeName` | Logical rooted, relative, or explicit absolute provider root |
| Context atom | `~`, `~~`, `^` | Current value, parent of current value, or current handled error |
| Step | `.name`, `.1`, `.'quoted'`, `.*`, `.**`, `./`, `.~~`, `[expr]` | Name/int key, wildcard, root, parent, or dynamic navigation |

`~` retains its current-value meaning under **S10.1.2–S10.1.3**. `^`
retains its handler-local current-error meaning under **S7.6.1v4** and
**S7.6.5v2**. Their ability to take ordinary dotted or indexed steps is a
surface-shape consistency, not a new shared runtime meaning.

## 4. Surface grammar

The intended grammar, abstracting the existing set of legal member names, is:

```ebnf
PathLiteral       ::= RootedPath | RelativePath | AbsolutePath

RootedPath        ::= "/" PostfixStep*
RelativePath      ::= "."
                    | "." PathPart PostfixStep*
                    | "." IndexStep PostfixStep*
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

`/` and `.` are valid bare paths. `./` and `file./` are also complete paths:
the final slash is a root operation, not a trailing separator. Empty steps and
a trailing dot separator are not valid: `/.`, `.a.`, and `/.a..b` are errors.
A segment containing a dot or other non-name character remains quoted:

```lambda
/.'var'.log.'app.log'       // /var/log/app.log
.~~.data.'config.dev.json'  // ../data/config.dev.json
```

Dynamic steps retain bracket notation:

```lambda
let part = 'daily'
/.var.log[part].'app.log'
```

The absolute roots `file`, `http`, `https`, and `sys` use the same operations.
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
| Relative keys `a/b` | `.a.b` | unchanged |
| Absolute local-file keys `a/b` | `file./.a.b` | none |
| Absolute remote-file keys `a/b` on `host` | `file.host.a.b` | none |
| Absolute HTTP keys `host/a/b` | `http.host.a.b` | unchanged |
| Root of `value` | `value./` | none |
| Key `b` from root of `a` | `a./.b` | none |
| Relative `../a/b` | `.~~.a.b` | `..a.b` |
| Relative `../../a` | `.~~.~~.a` | `(..)..a`, related compound forms |
| Parent of `value` | `value.~~` | `value ..` |
| Two parents of `value` | `value.~~.~~` | `value .._..` |

The leading `.` distinguishes a relative reference from contextual
parent navigation:

```lambda
.~~.a       // relative reference; file projection is ../a
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
hierarchy anchor (**S2.4.2v4**):

1. At the logical-global root, the anchor is `/`.
2. At an absolute provider path, the anchor retains the provider and selected
   authority: `file./`, `file.hostname`, or `http.hostname`.
3. At a relative root, the anchor is not known until resolution. Static
   normalization discards preceding relative child steps but retains `./` as
   the unresolved root-selection operation.

Consequently:

```lambda
/.a.b./.c             == /.c
.a.b./.c              == ./.c
file./.a.b./.c        == file./.c
file.hostname.a./.b   == file.hostname.b
http.hostname.a./.b   == http.hostname.b
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
/.a.b.~~          == /.a
/.a.~~.b          == /.b
/.~~              == /

.a.~~             == .
.a.~~.~~.b        == .~~.b
.~~.~~            // ../../
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
/.home.user ++ .docs.file       == /.home.user.docs.file
/.home.user ++ .~~.shared       == /.home.shared
file.host.home ++ .docs./.tmp   == file.host.tmp
.work.build ++ .~~.src          == .work.src
.work.build ++ .cache./.src     == ./.src
```

A rooted or absolute suffix remains an error where composition requires a
relative suffix. A bare `.` suffix is the identity.

### 6.3 Provider and OS conversion

Logical source notation and provider notation remain separate. Explicit
absolute paths project directly through their named provider:

```text
file./.a.b      <-> /a/b on the current machine
file.host.a.b   <-> /a/b on machine host
http.host.a.b   <-> http://host/a/b
https.host.a.b  <-> https://host/a/b
```

Rooted and relative paths first select their semantic base. When `/` is
mounted to a local file provider:

```text
/.a.b       -> file./.a.b -> /a/b
.a.b        <-> ./a/b
.~~.a.b     <-> ../a/b
```

If `/` is mounted to HTTP or another provider, the same logical reference is
projected through that provider instead. Platform conversion happens only at
the target boundary. Windows drive and separator handling remain target-layer
concerns and do not alter Lambda source syntax.

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
relative path. That form starts with `.`, as in `.~~`.

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
  an absolute path; the existing member grammar may remain the concrete parse
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
  cannot represent both `/.a` and `file./.a` because their path values and
  canonical printers differ;
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

## 11. Migration

This is an intentional breaking syntax change. Migration is mechanical but
must be syntax-aware because `..`, `.`, `/`, division, and `...` currently
share lexical neighborhoods.

```text
/a.b             -> /.a.b
..a.b            -> .~~.a.b
..                -> .~~
value ..          -> value.~~
value .._..       -> value.~~.~~
```

The transition should update grammar, generated parser, AST builders, MIR
Direct, interpreter, path runtime, formatter/printer, active documentation,
and tests together. A source migration tool may accept the old grammar, but
the language grammar and canonical printer should not retain both spellings.

## 12. Conformance matrix

At minimum, implementation tests must cover:

1. Bare and multi-step rooted/relative paths and canonical printing.
2. `file./` current-machine paths and `file.hostname` named-machine paths,
   including canonical printing and provider projection.
3. `http`, `https`, and `sys` absolute paths.
4. Rooted and absolute paths remaining distinct values while a mount may make
   their resolved targets coincide.
5. Quoted, wildcard, recursive-wildcard, integer, root, and dynamic steps
   after all three path root forms.
6. `value./`, chained `value./.a`, and root selection after another member or
   parent operation.
7. Root normalization for logical, relative, local-authority, and named-host
   paths, including preservation of the provider/authority anchor.
8. One and repeated `.~~` steps, including root clamping and cancellation.
9. `base ++ .~~.x` and `base ++ .a./.x` for rooted and relative bases.
10. `value.~~`, `value.~~.~~`, bare `~~`, and nested current-value scopes.
11. Bare `~~` causing mapping-pipe classification.
12. Missing contextual root/parent returning `null` and chaining totality.
13. Traversal roots/parents carried by a zipper/path with no stored container
    root or parent pointer.
14. Equality/hash/print parity between literal and dynamically composed paths.
15. Rejection of `/a`, `..a`, `value ..`, `_..`, and accidental `...`
    reinterpretation without rejecting valid `./`.
16. `/` mount qualification to file, HTTP, and prefix-mounted providers,
    including cycle rejection and no existence-based fallback.
17. Bare-name namespace qualification, symbol/name key equivalence, integer
    keys, and the distinction between `a.1` and `a.'1'`.
18. Static-reference and dynamic-member parity, including optimized versus
    generic resolution.
19. Address resolution performing no I/O; external target forcing retaining
    its ordinary effect/error contract.

## 13. Decision ledger

| ID | Decision |
|---|---|
| **PTH1v2** | Rooted references use `/.a.b`; `/` is the logical global root bound by a resolver context, not intrinsically the filesystem root. |
| **PTH2v2** | Relative references use `.a.b`; `.` selects the active relative reference base, which a file provider projects to its current directory. |
| **PTH3** | `~~` is the one parent step and is never a path root. |
| **PTH4** | Relative parent paths are `.~~.a`; repeated parents are `.~~.~~`. |
| **PTH5** | Expression parent navigation is postfix `.~~`; arbitrary chains use `value.~~.~~`. |
| **PTH6** | Bare `~~` is exactly `~.~~`, inherits `~` scope, and counts as a free `~` in pipe classification. |
| **PTH7** | Parent steps normalize left-to-right; relative-root parents are retained and logical-global/qualified roots clamp. |
| **PTH8** | Relative-path composition applies parent steps to the base; it never discards them as scheme syntax. |
| **PTH9** | Missing contextual parent is `null`; path-root clamping is the closed path-specific rule. |
| **PTH10** | Document/context parents use navigation paths or zippers, never stored parent pointers. |
| **PTH11** | Printers emit only the new forms; old spellings are retired without permanent aliases. |
| **PTH12** | The runtime represents parent traversal as a typed operation/segment, not solely `PATH_SCHEME_PARENT`; C2MIR remains frozen. |
| **PTH13** | Every hierarchical address is a typed root plus ordered operations; ordinary keys are `NameKey` or `IntKey`. |
| **PTH14** | Names and symbols share `NameKey` identity in one scope; integer keys stay typed and `a.1` differs from `a.'1'`. |
| **PTH15** | Paths, names, and symbols are static reference plans; member expressions have a runtime base and are dynamically resolved. Their forcing policies remain distinct. |
| **PTH16v3** | A bare name is statically qualified to its selected lexical/import namespace (`a` → `ns.a`); in element tag/attribute name position the complete dotted namespace-qualified name is maximal. Relative-path content is spelled `<svg \.rect>` and takes **no** boundary delimiter — S16.9.4 respelled the relative path `\.a.b` (dissolving the ambiguity that motivated the old `;`), and the S16.9.3 boundary comma is a biconditional that forbids a comma when the element has no attributes. Both `<svg; .rect>` (v2's spelling) and `<svg, \.rect>` are errors. Global resolution never bypasses visibility or exports. *(v3, 2026-08-28, with S2.4.3v3.)* |
| **PTH17** | `/` and namespace aliases are deterministically qualified by an immutable per-context mount/resolver table; longest-prefix mounts are allowed and alias cycles reject. |
| **PTH18** | The resolver is isolate/EvalContext-owned and capability-aware; no mutable process-global namespace or existence-based provider fallback participates in semantics. |
| **PTH19** | Address resolution is pure canonicalization and performs no I/O; external target forcing remains a separate effectful operation. |
| **PTH20** | Static specialization and dynamic lookup use one semantic typed-key/provider protocol and must produce identical results. |
| **PTH21** | Paths have three root forms: logical rooted `/.a`, relative `.a`, and explicit absolute `SchemeName.a` such as `file./.a` or `http.host.a`. |
| **PTH22** | An absolute path names its provider and authority directly, bypasses the logical `/` mount, and uses the same typed operations as rooted and relative paths. |
| **PTH23** | A registered scheme name at the head of a dotted chain is reserved as an absolute-path root; a dedicated grammar production is optional if AST classification is equivalent. |
| **PTH24** | Rooted and absolute paths retain distinct root kinds and structural path values even when resolution maps them to the same qualified target. |
| **PTH25** | `/` is the single root-selection operation: initial `/` selects the logical resolution root, while postfix `./` selects the root of its explicit base. |
| **PTH26** | `value./` is a normal postfix navigation step at member precedence and may be followed by the same dotted/indexed operations as any other reference. |
| **PTH27** | File absolutes spell the current machine as `file./` and a named machine as `file.hostname`; following operations address data beneath that filesystem root. |
| **PTH28** | A root operation discards descendant steps while preserving the logical or provider/authority anchor; unresolved relative root selection remains as `./`. |
| **PTH29** | Dynamic root navigation is occurrence-based and uses a navigation path or zipper; it never adds observable root/parent pointers to Lambda values. |

## 14. Open issues and non-decided options

### PTH-O1: leading relative integer key and string-key input — RESOLVED

Lambda assigns `.1` to the float literal `0.1` under **S4.3.1**; it must remain
a float. A relative path beginning with `IntKey(1)` therefore uses the indexed
form `.[1]`. The symbol spelling `.'1'` deliberately remains `NameKey("1")`
under **S2.4.2v4** and **PTH14**.

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
