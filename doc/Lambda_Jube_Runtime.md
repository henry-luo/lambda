# Lambda Jube Runtime

Lambda has one host executable: `lambda.exe` (or `lambda` in a release
bundle). Jube is the native-module system used to add hosted languages without
recompiling a different runtime.

## Hosted Python

Python is the first hosted language. The full bundle contains:

```text
lambda
modules/lang-python/
module.json
  lang-python.dylib | lang-python.so | lang-python.dll
```

The build writes a platform-specific SHA-256 for the native library into
`module.json`; the host verifies it before loading the library.

The standard bundle contains the same host binary plus a manifest-only
`lang-python` descriptor, but omits the Python grammar and native library.
When a Python source or `py` command is selected, the host discovers the
module, validates its ABI, hosted API version, build ID, and descriptor before
activating it. A missing or incompatible module produces a generic hosted
language diagnostic; it is never parsed as Lambda source.

```sh
lambda py app.py
lambda run --lang python app.py
lambda app.py
```

The compatibility name `lambda-jube.exe` is a symlink/launcher to
`lambda.exe`; it is not an independently compiled runtime.

## Build and package commands

```sh
make build                 # standard host
make build-lang-python     # external debug Python module
make build-jube            # host + Python module + compatibility link

make release               # release host
make release-lang-python   # release Python module
make package-standard      # standard distribution staging
make package-jube          # full distribution staging
make verify-jube-package   # identical-host hash plus hosted Python smoke test
```

`release-standard/lambda` and `release-jube/lambda` must be byte-identical.
Only the full bundle carries the `lang-python` native library and resources;
the standard bundle carries its small manifest-only descriptor.

## Boundary and ownership

The host owns Jube discovery, lifecycle, capability/build negotiation,
rooting, runtime-import registration, and generic CLI/source dispatch. Python
owns Python syntax, AST policy, scope rules, object/class semantics, runtime
helpers, imports, and lowering decisions. Python must not call JavaScript
runtime symbols; cross-language module loading goes through the host's
import-time language bridge.

Lambda and JavaScript evaluator/JIT paths do not consult hosted-language
descriptors. Module discovery happens only at CLI or import fallback points.

The detailed hosted-language architecture and Python implementation sequence
are maintained in `vibe/Lambda_Design_Jube_Lang_Hosting.md` and
`vibe/Lambda_Impl_Hosted_Python.md`.
