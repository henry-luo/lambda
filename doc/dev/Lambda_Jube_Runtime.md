# Lambda Jube Runtime

> **Scope.** This is a build and packaging note for Jube native modules,
> written for developers.
>
> **Status (alpha).** No hosted language ships. The `lang-python` module and
> the host's hosted-language services were removed on 2026-10-07 (the D7.4.3
> footnote in `doc/Lambda_Formal_Design.md`). The Bash and Ruby front ends
> remain in the source tree (`lambda/module/bash`, `lambda/module/rb`) but are
> not compiled into any build: their CLI handlers sit behind the `LAMBDA_BASH`
> and `LAMBDA_RUBY` build flags, which no configuration defines.

Lambda has one host executable: `lambda.exe` (or `lambda` in a release
bundle). Jube is the native-module system used to extend that host without
recompiling a different runtime. It carries the Node.js compatibility modules
(`node-core`, `node-fs`, `node-net`, `node-crypto`) that the standard bundle
ships, and the `rdb-drivers` database module of the full bundle.

## Modules and manifests

Each module is a directory beside the host:

```text
lambda
modules/node-fs/
  module.json
  node-fs.dylib | node-fs.so | node-fs.dll
```

`module.json` names the module, its `provides` specifiers and dependencies,
and the `base_abi_version` / `hosted_api_version` it was built against; the
host loads a module only when both match its own `JUBE_ABI_VERSION` and
`JUBE_HOST_API_VERSION` (D7.3.2). It may also carry a platform-specific
SHA-256 for the native library, which the host verifies before loading.
A missing or incompatible module surfaces as Node's ordinary
`MODULE_NOT_FOUND`.

Discovery scans `JUBE_MODULE_PATH` entries in order, then `modules` beside
the executable, then the working directory's `modules`. The first manifest
for a module's specifier wins; later copies of the same module cannot replace
it. An incompatible selected image reports an error rather than trying a
lower-priority copy. Manifests from different modules claiming one specifier
still conflict, and the selected manifest must match its descriptor (D7.3.4).

The host exports only the symbols listed in `lambda/jube/jube_host_exports.txt`
to modules (D7.3.6).

The compatibility name `lambda-jube.exe` is a symlink to `lambda.exe`; it is
not an independently compiled runtime.

## Build and package commands

```sh
make build                 # standard host
make build-node-core       # external debug node-core module (likewise node-fs/-net/-crypto)
make build-jube            # host + lambda-jube compatibility link

make release               # release host
make package-standard      # standard distribution staging
make package-jube          # full distribution staging (adds rdb-drivers)
make verify-jube-package   # identical-host hash plus module smoke tests
make check-host-exports    # host exports and module imports agree with the list
```

`release-standard/lambda` and `release-jube/lambda` must be byte-identical.

## JIT compilation

Supported builds use only the MIR Direct path:

```text
Lambda AST -> transpile-mir.cpp -> MIR API -> native code
```

The retired C2MIR implementation (`transpile.cpp` and `transpile-call.cpp`) has
been removed from the Lambda source tree. The host and CLI use MIR Direct;
supported binaries do not expose `--c2mir`. The vendored MIR distribution still
contains its upstream C2MIR sources because that dependency is not modified by
Lambda.

## Boundary and ownership

The host owns Jube discovery, lifecycle, ABI negotiation, rooting, and
specifier resolution. A module reaches the host only through `jube.h` service
tables and the exported allowlist; Lambda and JavaScript evaluator/JIT paths
never consult module descriptors.

The module architecture is maintained in
`vibe/Lambda_Design_Jube_Architecture.md`; the removed hosted-language design
is kept as a record in `vibe/Lambda_Design_Jube_Lang_Hosting.md`.
