# Lambda path mapping

**Ratified 2026-11-18.** S2.4, S8.2.1v5, S10.6.1v3 and S16.9.4v2 govern the source forms below. D10.1.8 governs deferred file access. The implementation plan tracks the transition from the earlier path-value model. [Path design](Lambda_Type_Path.md) · [Implementation plan](impl/Lambda_Impl_Path_Node_Access.md)

| Expression | Result | Target work |
|---|---|---|
| `/.users.peter.doc` | The node at local `/users/peter/doc` under the default mount | Resolve the demanded node |
| `file.users.peter.doc` | The node at `/peter/doc` on host `users` | Use the named authority |
| `file./.users.peter.doc` | The same local node as `file.localhost.users.peter.doc` | Resolve the demanded node |
| `file.localhost.users.peter.doc is file` | Whether the local entry is a file | Metadata suffices |
| `file.localhost.users.peter.doc.body` | The body when the document exposes one | Read content as needed |
| `\.file.localhost.users.peter.doc` | An absolute `path` | Capture only |
| `\.body.0` | A relative `path` | Capture only |
| `p#` | The concrete target of path `p` | Same observations as direct access |
| `p ++ \.body` | A composed `path` when `p` is a path | Apply normalized relative steps |
| `node ++ \.body` | Type error for a concrete node | No traversal |
| `node[\.body.0]` | The value of `node.body[0]` | Read from the supplied receiver |

## File authorities

The first name after `file.` is the authority. `file.hostname.path` corresponds to `file://hostname/path`. RFC 8089 gives `localhost` the same meaning as the empty authority in `file:///path`. Lambda maps `file:///path` to `file./.path` and `file://localhost/path` to `file.localhost.path`. Both spellings select the same local node and capture equal addresses. Printing a captured path preserves its `file./` or `file.localhost` spelling. `local` is an ordinary hostname. On Windows, a named authority selects a UNC share even when it is the current host: `file.myhost.C.path` selects `\\\\myhost\\C\\path`, while `file.localhost.C.path` selects `C:\\path`. A UNC `localhost` server captures the current machine's named authority. The short form `/.path` uses the logical root, which maps to the local filesystem by default. [S2.4.5v6](../doc/Lambda_Formal_Semantics.md#s24-paths) · [RFC 8089 §2](https://www.rfc-editor.org/rfc/rfc8089.html#section-2)

## Reading and composing

A path has a typed root and ordered operations. Quoted names, integer indexes, parent, root, wildcard and query steps retain their meanings. The escape records the scheme only for a bare registered scheme head. `\['file'].users` captures a relative entry named `file`. A dynamic key is evaluated when the reference is built. Address construction, equality and composition require no lookup of the named target. [S2.4.1v3–S2.4.4v2](../doc/Lambda_Formal_Semantics.md#s24-paths)

With `let p = \.file.localhost.users.peter.doc` and `let d = p#`, the expressions `d.body`, `d[\.body]` and `(p ++ \.body)#` all select the same member. An empty relative selector takes zero steps. On a missing member, traversal yields the ordinary absent-read result. External operations retain the provider's permission and error contract. [S7.1.1v3](../doc/Lambda_Formal_Semantics.md#s71-reads-are-total-writes-are-checked) · [S8.2.1v5](../doc/Lambda_Formal_Semantics.md#s82-the-key-space)

Directory enumeration returns entry nodes. `\.src.**#` can enumerate a tree and `\.src?file#` can test file kinds without parsing files. A later step that asks for document content can then open the selected files. [S2.4.6v2–S2.4.7v2](../doc/Lambda_Formal_Semantics.md#s24-paths)

## Named context roots

S2.4.8 gives three common directories short names. `~cwd` selects the working directory, `~home` selects `~user.home`, and `~temp` selects the temporary directory. Each supports ordinary member access. `~user.name` yields the account name. `~proc` exposes the current process, including its environment and argument vector. `~proc.cwd` retains the provider's string type. [S17.3.1v2](../doc/Lambda_Formal_Semantics.md#s173-process-information)

A report generator illustrates why there are three distinct locations. When launched from `work/` to process `reports/annual.xml`, a helper defined in `lib/render.ls` sees the working directory through `~cwd`, the report's document root through `~doc`, and its own source file through `~script`. Opening a template preserves the report selection. The escape can carry the helper's location back to its caller. In a pipe, `~home` still selects the user's directory and `~.home` reads the current item's member. [S2.4.8](../doc/Lambda_Formal_Semantics.md#s24-paths) · [S10.1.2v5](../doc/Lambda_Formal_Semantics.md#s101-union-pipe-and-filter)
