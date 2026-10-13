# Lambda-owned TeX bindings

These `.sty` files are Lambda's own, not vendored copies: each binds a LaTeX
package's user commands for Lambda's TeX expansion engine, written from the
package's manual (D7.2.7v2; `vibe/latex/Lambda_Design_TeX_Engine.md` §4). The
engine finds them after the unmodified upstream files in the parent directory.
They may be edited like any Lambda source; CLAUDE.md rule 16 does not apply.

A binding only expands commands into constructs the digester and the
`lambda.latex` adapters already own. It never typesets, and every command must
expand without assignments at use time, because the engine expands the
arguments of `\section`, `\caption` and `\footnote` without executing them.
A command that builds a block from a file, such as csvsimple's `\csvreader`,
assigns while it reads and belongs in body text.
Each bound package also has a row in `lmd/package/latex/packages/registry.ls`.

| File | Binds |
|---|---|
| `physics.sty` | physics 1.3: bracing, vectors, operators, quick-quad text, derivatives, Dirac notation, matrix macros; options `arrowdel`, `italicdiff`, `notrig` |
| `mathtools.sty` | the mathtools 1.31 macro layer: paired delimiters, `\prescript`, `\splitfrac`, `\adjustlimits`, `\MoveEqLeft` |
| `xspace.sty` | xspace 1.13: `\xspace`, through the engine's `\lambda@xspace` lookahead primitive (a `\futurelet` would be an assignment) |
| `tocloft.sty`, `authblk.sty`, `titling.sty`, `minitoc.sty` | only the macros those packages are configured by, with their defaults, so a document's `\renewcommand` is legal and reaches the adapter; tocloft's `\newlistof` also makes its counter and passes itself on |
| `csvsimple.sty` | the csvsimple-l3 interface, in expl3: `\csvreader`, the auto tables, `\csvset`, `\csvstyle`, `\csvnames`, and the table, line, filter and respect keys |
