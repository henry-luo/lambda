# Lambda Packages — Design

**Status:** working design doc (secondary ADR) for Lambda script packages.
Consolidated 2026-10-14 from `Lambda_Package (idea).md`, `Lambda_Package.md`
and `Lambda_Package2.md`; implementation history and the retired C-text
module linkage are omitted.

**Authority.** The formal specs win: `doc/Lambda_Formal_Semantics.md` (`S#`)
and `doc/Lambda_Formal_Design.md` (`D#`). This document carries the design
and its reasoning; where a point has no formal ruling it says so (§10).

**Spec linkage.**

| Section | Rulings |
|---|---|
| §1 Model | D7 terminology, D7.2.1, D8.2.1, D1.7 |
| §2 Exports | S16.9.1, S9.1.7, D7.2.1, D7.2.2, D8.3.4v4, S12.3.7 |
| §3 Imports | S16.9.6, S16.10.1v3, S12.3.3v2 |
| §4 Resolution | S16.9.8, D7.2.6, D7.2.4, D7.2.3 |
| §5 The `lambda.*` namespace | D7.2.4, S17.2.1, S17.2.2, S16.10.1v3 |
| §6 Instantiation | D7.2.1–D7.2.3, S7.7.6, D5.4.3, D8.5.1 |
| §7 Type identity | S2.1.4 |
| §8 Cross-language packages | D8.2.1, D7.2.6, D7.1.6 |

**Scope.** Lambda script packages (`.ls` files): what they export, how
imports bind and resolve, the shipped `lambda.*` tree, and package
instantiation. Out of scope: build packaging (D7.1), Jube native modules
(D7.3–D7.5), and the JS module system beyond its import boundary with
Lambda. User-facing reference: [`doc/Lambda_Modules.md`](../doc/Lambda_Modules.md);
the shipped package catalogue: [`doc/Lambda_Packages.md`](../doc/Lambda_Packages.md).

---

## 1. Model

**Terminology.** A Lambda script module is a **package** where
disambiguation matters; Jube/native DSOs are **modules** (D7 terminology).
Every `.ls` file is a package — there is no separate package declaration.

**A package is the compilation unit** (D7.2.1). Its language profile is a
property of the unit (D8.2.1): mixed-language *programs* exist,
mixed-language *packages* do not. Its bindings are immutable at module scope,
and its instantiation is effectful even though its bindings are not.

**Packages distribute as source** (D1.7, D7.2.3). Compiled artifacts are
local derived caches keyed by build and content, never a format.

**Design goals.**

1. **Explicit dependencies** — a package states what it uses by importing
   it; nothing becomes visible ambiently.
2. **Namespace control** — an importer chooses between unqualified and
   alias-qualified access, so two packages exporting the same name can be
   used side by side.
3. **First-class type sharing** — types, object types and string patterns
   cross package boundaries as readily as functions.
4. **Predictable resolution** — an import means the same thing wherever the
   script is run from, and moving a directory of packages preserves their
   imports.
5. **One root for what Lambda ships** — built-ins, system functions and
   shipped packages live under the reserved `lambda.*` root.

---

## 2. Exports

**`pub` is a uniform prefix modifier** (S16.9.1). A top-level declaration is
private unless prefixed with `pub`:

| Declaration | Exports |
|---|---|
| `pub let x = …` | a value |
| `pub fn f() …` / `pub pn p() …` | a function / a procedure |
| `pub type T = …` | a type alias — including a string or symbol pattern (`pub type Email = \(…)`) |
| `pub type T { … }` | an object type, with its methods |
| `let`, `fn`, `pn`, `type` without `pub` | nothing — private to the package |

- Bare `pub x = 1` is not a declaration (S16.9.1); the repair is `pub let`.
- **There is no `pub var`**, by non-composition: a package has no mutable
  module-scope binding at all (S9.1.7, D7.2.1), so there is nothing mutable
  to export. A `var` at module scope is E224.
- **Exported functions have a boxed entry.** An import resolves to the boxed
  `_b` symbol of an exported function, so export makes the boxed entry
  mandatory (D7.2.2, D8.3.4v4); private functions are free to exist only in
  their unboxed specialisations.
- **A shadowing export travels only through the explicit import.** A
  package-level definition that shadows a system function exports like any
  other declaration; it reaches an importer through that importer's
  `import`, never ambiently (S12.3.7).

**Why types are exported.** Real packages publish *contracts*, not just
functions: a data shape that callers build and receive (`User`), a schema
used in `is`/`match` checks, an object type with methods that callers
instantiate. Without type export every importer would redefine the type,
violating DRY and making two "identical" types that are not the same type
(§7). String and symbol patterns are types (`type Code = \("ID-\d" "\w"*)`),
so `pub type` exports them too — no separate pattern-export form is needed.

**No re-export.** A package exports only what it declares. To pass an
imported name on, bind it again: `pub let sq = square`. A dedicated
re-export form for facade packages is an open proposal (§10.2).

---

## 3. Imports

### 3.1 Forms

```lambda
import .shapes              // unqualified: every pub name bound directly
import s: .shapes           // aliased: names reached only as s.name
import .util, u: .util2     // several imports in one statement
import math                 // a built-in module, unqualified
import m: lambda.math       // a built-in module, aliased
import svg: 'http://…'      // a markup namespace prefix, not a package
```

- **Unqualified import** binds every `pub` name of the package into the
  importing file.
- **Aliased import** binds only the alias. The package's names are reached
  as `alias.name` and are **not** bound bare, so the import adds exactly one
  name to the importer's scope. Qualified access is ordinary member access
  on the imported package (S12.3.3v2).
- An import's tail is a package path (§4) or a symbol (`'uri'`); a symbol
  tail declares a markup namespace prefix, not a package.

**Why aliasing is essential.** Type exports make name collisions routine —
two packages each exporting a `Point` cannot both be imported unqualified.
With aliases, `geo.Point` and `ui.Point` coexist, and the alias documents
at every use site where a name came from. The design intent is that an
aliased name works in **every** position its bare form would: calls, values,
element and object literals (`<s.Circle r: 2.0>`), annotations
(`let p: geo.Point`), and `is`/`match`.

### 3.2 Binding rules

- **An import binds names like a declaration does.** Importing a name that
  is already bound — by a local declaration or another import — is E209,
  *duplicate definition*; an alias is the way to keep two packages apart.
- **The alias is a binding name**, so the keyword bar applies: a keyword or
  the reserved root `lambda` cannot be an alias, and there is no quoted
  escape (`import 'edit': …` is rejected) (S16.10.1v3).
- **Imports come first.** A name exists only after its `import` line, so
  imports belong at the top of the file; `import` is not allowed inside a
  function.
- **Private names are never bound** in the importer.
- **Shadowing a system function** by import is permitted and warned, exactly
  as for a local definition; the built-in stays reachable as `lambda.sys.f`
  (S12.3.7, S17.2.2).

### 3.3 Built-in modules

`math` and `io` are always reachable by prefix without any import
(`math.sqrt(x)`). Importing them is optional and offers the same two
styles as any package: `import math` (unqualified, for math-heavy scripts)
and `import m: math` (a short prefix of the importer's choosing).

---

## 4. Resolution

### 4.1 Rules

**`.` is the only import separator** (S16.9.6): `import .a.b`, never
`.a/b` or `.a\b`, so a package path reads the same as a dotted name.

**The first token of a path picks one of three roots** (S16.9.8):

| Root | Spelling | Resolves to |
|---|---|---|
| **Name** — a package path | `import lambda.editor.x`, `import math` | `lambda` is the reserved root for everything Lambda ships (§5); a built-in or runtime-registered module is named by its root (`import math` ≡ `import lambda.math`); **every other root is reserved for the Lambda package registry** and is E216 until a package claims it |
| **`.` or `~~`** — a relative path | `import .a.b`, `import ~~.~~.a` | resolved against the importing script's directory: `.a.b` → `a/b`; each leading `~~` step adds one `../` |
| **`/`** — reserved | `import /.a` | an error, holding the rooted form for a later meaning |

- `~~` steps form the leading run and nothing else (`import .~~.a`,
  `import ~~.a.~~.b` and a module-less `import ~~` are errors), so every
  relative package has exactly one spelling.
- **One resolver serves every caller** (D7.2.6): AST import, dependency
  prebuild, and the document-transform package loader all resolve the same
  path the same way. Lookup order for a name root: the built-in modules,
  the runtime-registered host modules, then `lambda.<package>` beneath
  `<lambda-home>/package/`.
- **A relative path tries `.ls`, then `.js`** (D7.2.6). A directory is not a
  package.
- A script with no file (REPL, stdin) resolves relative imports against the
  working directory.

### 4.2 Why script-relative

Nearly every modern language resolves a relative import against the
*importing file*, not the working directory — JavaScript/TypeScript, Python
(package-relative), Ruby's `require_relative`, Dart, Zig, Rust's module tree.
The languages that used the working directory have been moving away from
it: Perl 5.26 dropped `.` from `@INC` as a security fix, PHP practice moved
to `__DIR__`-relative includes, and shell `source ./x` is a known footgun.
Script-relative resolution buys:

1. **Predictability** — a script behaves identically whatever directory it
   is run from.
2. **Composability** — nested imports work: if `a.ls` imports `lib/b.ls`
   and `b.ls` imports `.c`, `c` is found beside `b`, so a library directory
   with internal imports can be moved or vendored whole.
3. **Security** — the working directory cannot inject an unintended file.
4. **In-place execution** — a checked-in script with relative imports runs
   where it sits. (The flip side, D7.2.6: a harness that copies the script's
   text elsewhere breaks its imports.)

### 4.3 Why a bare name never falls back to a file

Bare names belong to packages. If `import a.b` could also mean `./a/b.ls`,
a package name and a local file could collide, and an import's meaning would
again depend on the working directory. Keeping the name space of import
roots for packages means a file is *always* reached relative to its
importer, and a future registry package can never be shadowed by a stray
file — nor capture one. A root that no package claims is therefore an
error (E216), never retried against the working directory or the Lambda
home (D7.2.6).

### 4.4 Canonical identity

A package is identified by its **canonical file**, not by the path spelling
that reached it: `import .utils.math` from `main.ls` and `import .math` from
`utils/helper.ls` name the same package, and so does an import through a
symlink (D7.2.3). Canonical identity is what makes "instantiated once"
(§6) and cycle detection reliable — two spellings of one file can neither
duplicate the package nor hide a cycle. Two *copies* of a file are two
packages.

### 4.5 The Lambda home

`<lambda-home>` is the runtime's asset directory (`LAMBDA_HOME`). Shipped
source maps beneath `<lambda-home>/package/`: `lambda.<package>.*` maps
beneath that package's directory, and `lambda.doc.math.*` maps to
`package/math/` (D7.2.4).

---

## 5. The `lambda.*` namespace

Ratified as S17.2.1/S17.2.2 and D7.2.4 (2026-08-27), closing SO37.

**The question.** S12.3.7 made system-function shadowing user-first, which
left no way to reach a shadowed builtin. The direction was fixed when SO37
was deferred: the escape rides the package system as a prelude import, not
new syntax.

**The scheme.** `lambda.*` is the one root for everything Lambda ships:

| Tier | Path | Notes |
|---|---|---|
| System functions | `lambda.sys.*` | prelude-imported unqualified; the qualified form is the shadow escape (S17.2.1, S17.2.2) |
| Built-in modules | `lambda.math`, `lambda.io` | the bare spelling is an alias: `import math` ≡ `import lambda.math` |
| Shipped packages | `lambda.editor`, `lambda.graph`, `lambda.chart`, … | no intermediate `package` level |
| Document packages | `lambda.doc.*` | document-processing packages, including math typesetting at `lambda.doc.math` |

**One owner of the builtin list.** `lambda.sys.f` and the unqualified `f`
address the same registry row — the prelude import is how the unqualified
names arrive, not a parallel table (S17.2.1).

**Why `lambda.sys.*` over the alternatives.** `sys.*`, `lang.*`,
`sys.func.*` and plain `lambda.*` were considered.

- Plain `lambda.*` for the functions themselves was ruled out: that root is
  already the package tree (`import lambda.editor.mod_doc`), so `lambda.len`
  would mix builtins with shipped packages, and a future `lambda.len.*`
  package would collide.
- `sys.func.*` pre-pays for a collision the conventions avoid — adjacent
  facilities get their own modules (`math`, `io`) rather than becoming
  `sys` members.
- `lambda.sys.*` keeps one root, matches the established terminology
  (*system functions*, spec §S17), and reuses the module machinery `math`
  and `io` already use.

**A path names exactly one thing.** Shortening the shipped math typesetting
package to `lambda.math` collided with the built-in `math` module
(`sqrt`/`pi`). The typesetting package moved to `lambda.doc.math`, leaving
`lambda.math` for the built-in. Hence D7.2.4's rule: no path may be both a
built-in module and a shipped package.

**The root is reserved.** `lambda` may not name a binding (S16.10.1v3), so
the escape can never itself be shadowed — the failure mode Python accepts
with a shadowable `builtins`. The cost is one reserved word: `let lambda = …`
is E201.

**The reserved core needs no qualification.** `int`, `string`, `type` are
base-type words already barred as binding names, so they can never be
shadowed and never need `lambda.sys.`.

---

## 6. Instantiation

- **Eager.** Imports are resolved and their packages compiled while the
  importer is being built, depth-first, so an importer is never compiled
  against an unloaded dependency. The static import closure may be
  prebuilt concurrently, each loader waiting only on the package it is
  about to consume (D8.5.1).
- **Once per process.** A package is compiled and instantiated once however
  many packages import it and by whatever spelling (§4.4); later importers
  share the instance (D7.2.3). Package state lives in per-context slabs,
  never at code-baked addresses (D5.4.3).
- **In import order, before the importer's body.** Each imported package
  runs its top level in import-declaration order before any code of the
  importer that could read its names.
- **Top-level effects run at import time.** A `pub let config =
  input('config.json')^` reads the file when the package is instantiated.
  The values of a package's top-level content expressions are discarded;
  only the entry script's result (or its `main`) is the program's output.
- **Initialization is a transaction barrier** (D7.2.2, S7.7.6). A failed
  initializer aborts instantiation and the import fails; a package with
  half-established top-level state never becomes importable. Optionality is
  the package's own decision, made inside its initializer with a handler:
  `pub let config = input('config.json') ^ { {} }`.
- **Cycles are errors.** A package that is re-entered while it is still
  loading is a circular import. Under eager instantiation a cycle could only
  be served by exposing a half-initialized package, which D7.2.2 forbids.
- **Imported values are the instantiated values.** Module-scope bindings are
  immutable (D7.2.1), so whether an importer holds a snapshot or a live
  view of a `pub let` is unobservable.
- **One runtime, one value currency.** All packages of a program share one
  runtime context and heap; `Item` values pass between packages freely, with
  no marshalling at the boundary.

---

## 7. Cross-package type identity

An imported type **is** the exporting package's type, not a copy: an object
type keeps its nominal identity across the boundary (S2.1.4), so
`c is Counter` holds in the importer for a `Counter` built by the exporter
and vice versa, construction with `<Counter …>` in the importer stamps the
exporter's type, and its methods dispatch to the exporter's definitions.

Consequences:

- **Identity follows the canonical file** (§4.4). The same file imported by
  two spellings is one type; two copies of a file declare two distinct
  types, even when structurally identical.
- **Each package owns its own type and constant tables.** An importer refers
  to the defining package's entries rather than re-declaring them; a
  package's code always reads its own tables, whoever calls it.
- **Package lifetime bounds type lifetime.** Type objects defined by a
  package are referenced from every importer, which is safe because a
  package lives as long as the process (§6). Hot reload, unloading, and
  multi-threaded instantiation would each break that assumption; see §10.8.

---

## 8. Cross-language packages

- **`.js` fallback.** `import .name` loads `name.js` when there is no
  `name.ls` (D7.2.6). The JS file is its own compilation unit with its own
  language profile (D8.2.1): a program may mix languages, a package may not.
- **JS → Lambda.** Exported JS functions become callable Lambda functions,
  and the JS module keeps its own state between calls. A Promise-returning
  function is awaited with `wait` inside a procedure; a rejection becomes a
  Lambda error. An `export default` is reachable only through an alias
  (`j.default()`).
- **Lambda → JS.** JavaScript can import a Lambda package
  (`import * as m from './lib.ls'`): each `pub fn` arrives as a function and
  each `pub pn` as a Promise-returning function; `pub let` values are not
  exported to JavaScript.
- **Reduced hosts.** In a build without the JS runtime (`lambda-cli`,
  D7.1.6), importing a `.js` file is an ordinary import error — nothing
  degrades silently.

---

## 9. Diagnostics

| Condition | Diagnostic |
|---|---|
| A bare root that no package claims (`import tools.util`) | **E216** — *no package 'tools' for import 'tools.util'* |
| A missing file, or a package that fails to compile | **E217** — *failed to import Lambda module '.x'*, after the package's own errors |
| A package re-entered while loading | *Circular import detected*, then E217 |
| An imported name already bound | **E209** — duplicate definition |
| A keyword or `lambda` as an alias, or `lambda` as any binding | E201 / parse error (S16.10.1v3) |
| A rooted path, a misplaced `~~`, a `/` or `\` separator | syntax error (S16.9.6, S16.9.8) |

An import failure names **the path it resolved and the importing script**,
so a reader can tell a wrong spelling from a wrong base directory without
re-deriving the resolution.

---

## 10. Open design points

None of these is ruled. Each needs a user ruling before implementation
(`doc/Doc_Convention.md` §2); the notes record how each proposal meets the
current rulings.

### 10.1 Selective import

Import only named members, optionally renamed:

```lambda
import .math.utils: factorial, fibonacci as fib
```

*Benefit:* states dependencies precisely and keeps the importer's scope
small without an alias prefix. *Conflict:* the `name: path` form is already
the alias spelling (`import s: .shapes`), so this exact syntax is
ambiguous; a selective import needs a spelling that does not reuse `:` after
the path.

### 10.2 Re-exports and facade packages

```lambda
// math/index.ls
pub import .trig: sin, cos, tan
pub import .constants: *
```

*Benefit:* a facade package can present one import surface over several
implementation packages. *Status:* today a package passes a name on only by
rebinding it (`pub let sin = trig.sin`). D7.3.4 already anticipates *script
re-export* as the way Jube modules expose each other's surfaces, so a
re-export form would serve both. Depends on the selective-import spelling
(§10.1).

### 10.3 Directory packages (index files)

A directory with an `index.ls` imported as a package (`import .math` →
`math/index.ls`). *Status:* a directory is not a package today; adopting
this changes D7.2.6's lookup (`.ls`, then `.js`) and needs a precedence rule
against a sibling `math.ls`.

### 10.4 Optional import

`import? .feature` binding `null` when the package is missing, for optional
dependencies. *Status:* today optionality lives inside the dependency's
initializer (§6). An optional import would need to distinguish *missing*
(→ `null`) from *failed to initialize* (still a transaction-barrier failure
under D7.2.2), and to say what an unqualified `import?` binds.

### 10.5 Package metadata and project manifest

A package-level metadata declaration (name, version, description) and a
project manifest (dependencies, path aliases such as `@utils`). *Status:*
these belong with the Lambda package registry that S16.9.8 reserves bare
roots for, which is not yet designed. An `@alias` root is not one of
S16.9.8's three roots and would need a ruling of its own.

### 10.6 Export list

An explicit `export a, b, c` list at the end of a package as an alternative
to per-declaration `pub`. *Status:* it would be a second export spelling
beside S16.9.1's uniform `pub` prefix.

### 10.7 Package-relative resources

Data paths inside a package — `input("data.json")`, `\.'data.json'` —
resolve against the working directory, not the package's directory, so a
package cannot portably read a file shipped beside it. No ruling defines a
package-relative resource root.

### 10.8 Type identity beyond load-once

§7 relies on packages living for the whole process on one thread. If hot
reload, unloading, or concurrent instantiation is ever adopted, cross-package
type references need a different footing. Options recorded: a global type
registry that deduplicates every package's types and remaps their indices;
structural comparison for cross-package type tests; a single long-lived type
arena independent of package lifetime.

### 10.9 Import suggestions

On E216/E217, suggest a near spelling when a similar file or package exists
(*did you mean `.utils.math`?*).

---

## Appendix A — Implementation notes

- **Back end.** Packages compile through MIR Direct only (D8); the former
  C-text module structs and link-time pointer patching are gone.
- **Resolver.** `lambda_resolve_import_module_path`
  (`lambda/runtime/ast_build.hpp`) is D7.2.6's one resolver; `resolve_import`
  reports E216 after the built-in and registered-module checks. Cycle
  detection is in `load_script` (`lambda/runtime/runner.cpp`).
- **Regression fixtures.** `test/lambda/import_pub_types.ls` (type alias,
  object type with methods, cross-package `is`),
  `test/lambda/import_error_destr.ls` (error values exported from a
  package initializer), `test/lambda/lambda_namespace.ls` and
  `test/lambda/negative/semantic/lambda_namespace_root.ls` (§5).
- **Known gaps against this design** (tracked in
  [`doc/Lambda_Modules.md` — Known Issues](../doc/Lambda_Modules.md#known-issues)):
  an aliased type in a type position (`let a: s.Angle`, `x is s.Circle`) is
  a syntax error (§3.1's intent); a method of an imported object type that
  reads a module-level `let` of its own package returns `null` (§7); a
  duplicated alias is accepted and keeps the first package (§3.2); an E209
  caused by an import is printed against the importing file.

## Appendix S — Superseded designs

| Superseded | Replaced by |
|---|---|
| ~~`import .mod as m` (namespace import)~~ | `import m: .mod` (§3.1) |
| ~~bare `import a.b` resolving against the working directory~~ | bare roots are packages; E216 when unclaimed (S16.9.8) |
| ~~relative imports resolving against the working directory~~ | resolved against the importing script's directory (D7.2.6) |
| ~~`..parent.module`~~ | `~~.parent.module` (S16.9.8) |
| ~~`import "./utils/x.ls"` quoted paths; `/` and `\` separators~~ | dotted paths only (S16.9.6) |
| ~~`import 'chart'` symbol import of built-in modules~~ | `import lambda.chart`; a symbol tail is a markup namespace (§3.1) |
| ~~`std.*` standard-library tree and a separate `std/prelude.ls`~~ | the `lambda.*` root and the `lambda.sys` prelude (§5) |
| ~~`lambda.package.<name>`; math typesetting at `lambda.package.math`~~ | `lambda.<name>`; `lambda.doc.math` (D7.2.4) |
| ~~`pub x = 1` bare export~~ | `pub let x = 1` (S16.9.1) |
| ~~`pub string p = …` / `pub symbol p = …` pattern exports~~ | patterns are types: `pub type P = \(…)` (§2) |
| ~~module-scope `let a^err = e` error destructuring~~ | retired language-wide (S7.6.5v2); handle with `e ^ { … }` (§6) |
