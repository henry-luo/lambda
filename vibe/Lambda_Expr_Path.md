# Lambda Path Mapping

> **Status:** decided; current syntax/runtime scope implemented
>
> **Ratified:** 2026-09-21
>
> **Scope:** the source spelling, normalization, qualification, and forcing
> boundary of Lambda path values. The full unified-reference rationale and
> decision ledger are in [Lambda_Type_Path.md](Lambda_Type_Path.md).
>
> **Formal linkage:** §1–§3 implement
> [S2.4.1v2–S2.4.5v2](../doc/Lambda_Formal_Semantics.md#s24-paths) and
> [S16.9.4](../doc/Lambda_Formal_Semantics.md#s169-declarations-elements-paths);
> §4 implements [S2.4.4](../doc/Lambda_Formal_Semantics.md#s24-paths);
> parent, root, and composition use
> [S10.4](../doc/Lambda_Formal_Semantics.md#s104-parent-navigation),
> [S10.5](../doc/Lambda_Formal_Semantics.md#s105-root-navigation), and
> [S10.6.1](../doc/Lambda_Formal_Semantics.md#s106-concatenation).
> Resolver ownership follows
> [D5.4.1](../doc/Lambda_Formal_Design.md#d54-runtime-globals-and-evalcontext).

## 1. Path model

A path is an immutable, typed address plan:

```text
Path = (root, ordered operation*)
Operation = NameKey | IntKey | DynamicKey | Wildcard | Root | Parent
```

It is a lazy target handle, not the target's value. Path literals construct and
normalize this plan without reading a file, listing a directory, or making a
network request. Static paths, names, symbols, and dynamic member expressions
share the key vocabulary but retain their respective evaluation contracts
(S2.4.2v5, S2.4.3v3).

`NameKey("1")` and `IntKey(1)` are distinct: `a.'1'` and `a.1` therefore
remain different paths. A bracket step supplies a computed key without
forcing the path: `p[k]` is the same path operation as `p.k`
(S2.4.2v5).

## 2. Roots and source spelling

Every path begins with one root form and then uses ordinary dotted or indexed
steps:

| Form | Example | Meaning |
|---|---|---|
| Logical rooted | `/.a.b` | Resolve from the logical `/` root. |
| Relative | `\.a.b` | Resolve from the active relative base. |
| Relative parent | `\.~~.a` | Resolve one directory/level above that base. |
| Local file provider | `file./.a.b` | Address `/a/b` on the current machine. |
| Named provider | `file.host.a.b`, `http.host.a` | Address the named provider authority directly. |

The empty rooted and relative paths are `/` and `\`. A segment that is not a
simple name is quoted; for example, `/.var.log.'app.log'` and
`http.'api.example'.v1`. `*` selects one path segment and `**` selects a
recursive path sequence.

The following pre-ratification spellings are retired and have no compatibility
meaning: `/a`, `.a`, `..a`, `value ..`, and `value .._..`. In particular, `.`
is not a relative-path root; `\` is. This is the S16.9.4 respelling that
prevents relative paths from colliding with member syntax.

### 2.1 Root and parent operations

`~~` is the parent operation, never a root. Within a relative path it is
written `\.~~`; on an expression it is postfix:

```lambda
\.~~.config            // relative parent, then config
\.~~.~~.config         // two relative parent steps
value.~~.name           // parent of value, then name
value./.name            // root of value's hierarchy, then name
~~.name                 // exactly ~.~~.name
```

The postfix `./` operation selects the current path/provider anchor or the
root relation supplied by a value occurrence. It is not a second slash-based
path syntax. Parent/root navigation on ordinary values uses evaluation
lineage, not observable parent or root pointers stored in Lambda values
(S10.4.3, S10.5.3).

### 2.2 Normalization and composition

Path operations normalize left-to-right. A parent removes a preceding child;
at a relative root an unmatched parent is retained, while a logical or
provider root clamps. A root operation drops descendants while preserving its
logical or provider/authority anchor:

```lambda
/.a.b.~~             == /.a
/.a.b./.c            == /.c
\.a.~~.~~.b          == \.~~.b
file.host.a./.b      == file.host.b
```

`base ++ relative_suffix` applies the relative suffix through that same
normalizer. A rooted or absolute suffix is not accepted where a relative one
is required:

```lambda
/.home.user ++ \.docs.file     == /.home.user.docs.file
/.home.user ++ \.~~.shared     == /.home.shared
```

Equality, hashing, canonical printing, qualification, and composition all
observe the normalized plan, never its construction history (S2.4.2v5).

## 3. Qualification

Logical `/` is a resolver root, not an intrinsic filesystem root. Its
immutable, evaluation-owned resolver qualifies it to a provider/authority;
for example, a default local policy may map `/.a.b` to `file./.a.b`. An
explicit provider path bypasses that logical mount:

```text
/.a.b       --default local mount--> file./.a.b
file./.a.b --already explicit------> file./.a.b
```

These two paths remain structurally distinct even when they qualify to the
same target. Resolver selection is deterministic: it honors visibility,
exports, sandboxing, and capabilities, and never probes one provider then
falls back to another because a target is missing (S2.4.4, S2.4.5v2).

## 4. Forcing external targets

Qualification is pure address canonicalization. Forcing is the separate,
effectful operation that asks the selected file, URL, or other provider for
the target. This boundary preserves lazy composition and prevents address
resolution from becoming implicit I/O (S2.4.3v3, S2.4.4).

For local filesystem targets, the forcing model is:

- a directory yields child **path values**, rather than eagerly loaded child
  contents;
- a file yields the value produced by the appropriate input/content loader;
- `*` and `**` force directory enumeration into one-level or recursive child
  path sequences; and
- provider failure follows that provider's declared error/effect contract;
  it never changes the chosen path root or triggers provider fallback.

Metadata may be requested independently of content forcing. This permits a
caller to inspect a path's name, type, size, timestamp, or link status without
reading a large file. These properties are an implementation convenience, not
additional operations in the path's semantic identity.

## Appendix A. Implementation notes

This appendix is informative; the rulings above and S2.4 remain authoritative.

- The runtime represents a path as a pool-owned persistent spine: a root and
  typed operation nodes. Construction, parent/root selection, wildcard steps,
  and `++` share one normalizing path operation rather than rebuilding spines
  in individual callers.
- Forced content may be cached on the path, and lazily populated `PathMeta`
  records hold filesystem metadata. Directory and wildcard results retain
  child paths with their metadata; file content is loaded only when forced.
  Neither cache nor metadata may affect path equality, hashing, printing, or
  qualification.
- The shipped default qualifies logical paths to local `file./` paths. A named
  file authority is valid as a path value but is rejected before OS-path
  projection unless it is the current machine; general mounts and remote file
  transport remain deferred.

The detailed implementation audit and test matrix live in
[Lambda_Impl_Path (done).md](impl/Lambda_Impl_Path%20(done).md).
