# Lambda Modules

Every Lambda script file is a **module**. A module shares its declarations by marking them `pub`, and another module brings them in with `import`. The same statement imports Lambda modules, the built-in `math` and `io` modules, the packages that ship with the runtime, JavaScript modules, and XML namespace prefixes.

> **Related Documentation**:
> - [Lambda Packages](Lambda_Packages.md) — the libraries shipped under `lambda.*`
> - [Lambda Syntax — Namespaces](Lambda_Syntax.md#namespaces) — `import svg: 'uri'` prefixes for markup
> - [Lambda Procedural — No Global Variables](Lambda_Procedural.md#no-global-variables) — module-level state and effects
> - [LambdaJS](JS_DOM_Support.md) — the JavaScript engine behind `.js` modules

---

## Table of Contents

1. [Exporting with `pub`](#exporting-with-pub)
2. [Importing a Module](#importing-a-module)
3. [How Imports Resolve](#how-imports-resolve)
4. [Built-in Modules](#built-in-modules)
5. [Qualified System Names](#qualified-system-names)
6. [Packages](#packages)
7. [Module Instantiation](#module-instantiation)
8. [JavaScript Modules](#javascript-modules)
9. [Other Languages](#other-languages)
10. [Known Issues](#known-issues)

---

## Exporting with `pub`

A module's top-level declarations are private unless marked `pub`. Every kind of declaration can be exported — values, functions, procedures, type aliases and object types:

```lambda
// shapes.ls
pub let PI = 3.14159
pub fn area(r: float) => PI * r * r
pub type Circle { r: float, fn diameter() => r * 2 }
fn helper() => 42
```

| Declaration | Visibility |
|---|---|
| `pub let x = …` | Exported value |
| `pub fn f() …` / `pub pn p() …` | Exported function / procedure |
| `pub type T = …` / `pub type T { … }` | Exported type alias / object type |
| `let`, `fn`, `pn`, `type` without `pub` | Private to the module |

- `pub` modifies a declaration: `pub x = 1` is a syntax error whose message says to write `pub let`.
- There is no `pub var`: a module has no mutable state (S9.1.7, D7.2.1), so `var` at module scope is error E224.
- A module does not re-export what it imports. To pass a name on, bind it again: `pub let sq = square`.

## Importing a Module

`import` without an alias binds every `pub` name of the module directly into the importing file:

```lambda no-run
// no-run: imports shapes.ls, shown above — save both as direct.ls and shapes.ls
import .shapes

let c = <Circle r: 2.0>;
[area(1.0), c.diameter(), c is Circle]
```

```text
[3.14159, 4, true]
```

With an alias, the names are reached only through the alias, and the bare names are not bound:

```lambda no-run
// no-run: imports shapes.ls, shown above — save as aliased.ls
import s: .shapes

let c = <s.Circle r: 2.0>;
[s.area(1.0), c.diameter(), s.PI]
```

```text
[3.14159, 4, 3.14159]
```

- One statement may import several modules: `import .util, u: .util2`. No `;` is needed after an import.
- An alias is a plain name; it cannot be a keyword such as `edit` or the reserved root `lambda`.
- A name exists only after its `import` line, so put imports at the top of the file. `import` is not allowed inside a function.
- Private names are not bound: referring to `helper` from `direct.ls` gives an error value, and calling it fails at run time.
- Importing a name that is already bound — by a local declaration or by another import — is error E209, *duplicate definition*. Use an alias to keep two modules' names apart.
- A local declaration or an import may shadow a system function; the compiler warns (*'sum' shadows a system function in this module*) and calls in that module resolve to the new definition. The built-in stays reachable as `lambda.sys.sum` (S12.3.7).

Imported types work in annotations, `is` and `match` like local ones. Under an alias, write the qualified name in literals — `<s.Circle …>`; a bare `<Circle …>` would build a plain element — and bind the type to a name before using it in a type position (see [Known Issues](#known-issues)):

```lambda no-run
// no-run: needs shapes.ls beside it
import s: .shapes
let Circle = s.Circle
let c = <s.Circle r: 2.0>;
[c is Circle]      // [true]
```

## How Imports Resolve

The first token of an import path decides where it looks (S16.9.8): a **name** starts a package path, **`.` or `~~`** starts a path relative to the importing file, and **`/`** is reserved.

| Import | Resolves to |
|---|---|
| `import .a.b` | `a/b.ls` beside the importing file (after following symlinks), else `a/b.js` |
| `import ~~.a` | `../a.ls`, one directory up from the importing file; each further leading `~~.` goes up one more (`import ~~.~~.lib.util` is `../../lib/util.ls`) |
| `import lambda.<package>.m` | `<LAMBDA_HOME>/package/<package>/m.ls` — a shipped package (D7.2.4) |
| `import lambda.doc.math.m` | `<LAMBDA_HOME>/package/math/m.ls` — document-processing packages live under `lambda.doc` |
| `import math`, `import io` | The built-in modules; `import lambda.math` and `import lambda.io` are the same modules |
| `import p: 'uri'` | A namespace prefix for markup, not a module ([Namespaces](Lambda_Syntax.md#namespaces)) |
| `import a`, `import a.b` (any other name) | Error E216: bare names are reserved for packages, and there is no package `a`. The name never refers to a file in the working directory |
| `import /.a` | Error: rooted import paths are reserved |

- `~~` steps come only at the start: `import .~~.a` and `import ~~.a.~~.b` are errors. Write `import ~~.a`.
- Only `.ls` and `.js` files are tried, in that order; `.mjs` and `.ts` files are not modules, and a directory is not a module.
- `LAMBDA_HOME` is the runtime's asset directory. Without the environment variable it is `./lmd` (in a source checkout and a release bundle alike), **relative to the current working directory**. When you run `lambda` from another folder, set `LAMBDA_HOME` to the absolute path of that directory, or package imports fail with E217.
- Your own modules are always imported relatively (`.a`, `~~.a`), so an import means the same thing wherever you run the script from.

## Built-in Modules

`math` and `io` are always available with their prefix — `math.sqrt(x)`, `io.copy(a, b)` — and can also be imported in two other styles:

```lambda
// 1. No import: the module prefix
math.sqrt(16);            // 4

// 2. Global import: every name without a prefix
import math;
sqrt(16);                 // 4
(pi > 3);                 // true

// 3. Aliased import: a prefix of your choice
import m: math;
m.sqrt(16)                // 4
```

| Style | Syntax | Usage | Best for |
|---|---|---|---|
| No import | — | `math.sqrt(x)` | Clarity, avoiding name conflicts |
| Global import | `import math;` | `sqrt(x)` | Math-heavy scripts |
| Aliased import | `import m: math;` | `m.sqrt(x)` | A short prefix |

The functions of both modules are listed in [Lambda_Sys_Func.md](Lambda_Sys_Func.md#math-module) and the `io` section of the same document.

## Qualified System Names

Everything Lambda ships lives under one reserved root, `lambda` (D7.2.4, S17.2.1). `lambda.sys.*` names the system functions, `lambda.math.*` and `lambda.io.*` the built-in modules. The qualified spelling reaches a built-in even where a local name shadows it:

```lambda
let sum = 10;
[sum, lambda.sys.sum([1, 2, 3]), lambda.math.sqrt(16)]
```

```text
[10, 6, 4]
```

`lambda` itself cannot be rebound (`let lambda = 1` is error E201).

## Packages

The libraries written in Lambda that ship with the runtime are imported by their `lambda.*` path, usually with an alias:

```lambda no-run
// no-run: needs LAMBDA_HOME to point at the runtime assets
import math: lambda.doc.math.math

let ast = parse("\\frac{a+b}{c}", 'math')^
format(math.render_inline(ast), 'html')
```

[Lambda_Packages.md](Lambda_Packages.md) lists the packages and their APIs.

## Module Instantiation

- **Once per file.** A module is compiled and instantiated once per process, however many times and by whatever relative spelling it is imported; imports through a symlink reach the same instance (D7.2.3). Two *copies* of a file are two modules, and their object types are different types.
- **Top-level effects run at import time.** A module-level `let` is evaluated when the module is instantiated, so `pub let config = input('config.json')^` reads the file while the importer is being compiled. The values of the module's top-level expressions are discarded; only the entry script's `main` runs.
- **A failed initializer stops the import.** If a module's top level raises — say `input()` cannot find its file — the importing script does not run, and the error is reported (D7.2.2). Handle the error in the module to make it optional: `pub let config = input('config.json') ^ { {} }`.
- **Cycles are errors.** Two modules that import each other fail with *Circular import detected*, followed by error E217.
- **An unknown package** is error E216, *no package 'tools' for import 'tools.util'*: a bare import root must name a package. Import your own files relatively (`.tools.util`, `~~.tools.util`).
- **A missing module** is error E217, *failed to import Lambda module '.nosuch'*, naming the path it tried. A module that fails to compile is reported the same way, after its own errors.
- **Paths inside a module** — `input("data.json")`, `\.'data.json'` — resolve against the current working directory, not the module's directory.

## JavaScript Modules

`import .name` loads `name.js` when there is no `name.ls` beside the importing file. Exported functions become callable Lambda functions, and the module keeps its state between calls:

```javascript file=counter.js
let hits = 0;
export function add(a, b) { return a + b; }
export function bump() { return ++hits; }
```

```lambda no-run
// no-run: imports counter.js, shown above — save as use_js.ls
import .counter

[add(1, 2), bump(), bump()]
```

```text
[3, 1, 2]
```

A JavaScript function that returns a Promise is awaited with `wait`, inside a procedure; a rejection becomes a Lambda error (see [Lambda_Concurrency.md](Lambda_Concurrency.md#javascript-interoperability)):

```javascript file=later.js
function later(x) { return new Promise(resolve => setTimeout(() => resolve(x * 2), 5)); }
export { later };
```

```lambda no-run
// no-run: imports later.js, shown above — save as use_promise.ls; run with lambda run
import .later

pn main() {
    wait(later(21))^
}
```

```text
42
```

- An `export default` is reachable only through an alias: `import j: .mod` then `j.default()`.
- In the other direction, JavaScript can import a Lambda module: `import * as m from './lib.ls'` gives each `pub fn` as a function and each `pub pn` as a Promise-returning function; `pub let` values are not exported to JavaScript.

## Known Issues

| Issue | Workaround |
|---|---|
| A method of an imported object type that reads a module-level `let` of its own module returns `null` | Read fields or parameters in methods, or pass the value in |
| An aliased type in a type position (`let a: s.Angle`, `x is s.Circle`) is a syntax error | Bind it first: `let Circle = s.Circle`, then `x is Circle` |
| An E209 collision caused by an import is reported at a line of the imported module but printed against the importing file | Look for the duplicated name in both files |
| A duplicated alias (`import u: .a, u: .b`) is accepted and keeps the first module; a local `let u` can coexist with the alias `u` | Use distinct aliases and names |
| `export const` values of a JavaScript module do not import correctly | Export a function that returns the value |
| `export async function` is rejected by LambdaJS | Declare the function, then `export { f }` |
| JavaScript calling a Lambda `pub fn` or `pub pn` that returns a string literal or calls `print` crashes the process | Return numbers or structured values across the boundary |
| `import 'uri'` without an alias is silently ignored | Always give a namespace a prefix |
