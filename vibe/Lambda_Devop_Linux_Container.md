# Lambda Linux Development with Apple Container

**Status:** Active development procedure  
**Host:** Apple Silicon macOS 26+  
**Guest:** Native ARM64 Linux (`aarch64`)  
**Workspace:** `/Users/henryluo/Projects/lambda-linux`

## Purpose

This workspace validates Lambda's Linux/ARM64 build and runtime behavior from
the macOS development machine. Apple Container provides a Linux kernel and
userspace, so the build uses Linux compilers, headers, libraries, and linker
behavior instead of macOS/Homebrew artifacts.

This is a Linux compatibility environment, not an exact reproduction of the
managed AWS Lambda service. AWS-specific runtime, IAM, filesystem, and service
integration behavior still needs a real AWS or Lambda-compatible deployment
test.

## Workspace and image

Keep Linux build artifacts in the parallel checkout. Do not build Linux from
the macOS checkout because static archives and generated objects are
architecture- and platform-specific.

```text
/Users/henryluo/Projects/lambda/        macOS development checkout
/Users/henryluo/Projects/lambda-linux/  Linux/ARM64 checkout
```

The Linux image is defined by
`.github/workflows/Dockerfile.linux` and is tagged:

```text
lambda-linux-arm64:latest
```

The normal persistent development container is:

```text
lambda-linux-dev
```

The repository is bind-mounted at `/lambda`, so source edits made on the host
are immediately visible inside the container. Build outputs also remain in
the Linux checkout.

## Build or refresh the image

Run these commands from the Linux checkout on the macOS host:

```bash
cd /Users/henryluo/Projects/lambda-linux
container build \
  --platform linux/arm64 \
  --file .github/workflows/Dockerfile.linux \
  --tag lambda-linux-arm64:latest \
  .
```

The Dockerfile installs the Linux toolchain and system development packages.
`setup-linux-deps.sh` builds or installs project-specific dependencies such as
GoogleTest, ThorVG, rpmalloc, RE2, mbedTLS, and the Linux Tree-sitter
archives. Rebuild the image after changing the Dockerfile or dependency setup:

```bash
container build \
  --no-cache \
  --platform linux/arm64 \
  --file .github/workflows/Dockerfile.linux \
  --tag lambda-linux-arm64:latest \
  .
```

Check the image and container state with:

```bash
container image list
container list --all
```

## Start the development container

Create the persistent container once:

```bash
container run \
  --detach \
  --name lambda-linux-dev \
  --cpus 8 \
  --memory 16G \
  --volume /Users/henryluo/Projects/lambda-linux:/lambda \
  --workdir /lambda \
  lambda-linux-arm64:latest \
  sleep infinity
```

If it already exists but is stopped:

```bash
container start lambda-linux-dev
```

Open an interactive shell:

```bash
container exec --interactive --tty lambda-linux-dev bash
```

Verify the guest before building:

```bash
container exec lambda-linux-dev bash -lc 'uname -m'
```

Expected output is `aarch64`.

## Regenerate and build

Regenerate Premake whenever `build_lambda_config.json` or
`utils/generate_premake.py` changes. Do not manually edit generated Lua or
Makefiles.

```bash
container exec lambda-linux-dev bash -lc \
  'python3 utils/generate_premake.py --output premake5.lin.lua'

container exec lambda-linux-dev bash -lc \
  'premake5 gmake --file=premake5.lin.lua'

container exec lambda-linux-dev bash -lc \
  'make -C build/premake config=debug_native lambda CC=clang CXX=clang++'
```

For a release build:

```bash
container exec lambda-linux-dev bash -lc 'make release'
```

Use the release build for performance measurements. The ordinary debug build
is for fast diagnostics; use `make build-debug-asan` when AddressSanitizer
coverage is required.

## Run tests

Run a focused native test first when validating linker or target dependency
changes:

```bash
container exec lambda-linux-dev bash -lc \
  'make -C build/premake config=debug_native test_item_repr_gtest CC=clang CXX=clang++'
```

Run the full baseline:

```bash
container exec lambda-linux-dev bash -lc 'make test-lambda-baseline'
```

The ordinary Linux debug build does not use AddressSanitizer. The explicit
`debug_asan` profile produces `lambda-debug-asan.exe`; keep sanitizer-specific
runs separate from the normal `test-lambda-baseline` path so that the baseline
continues to exercise the fast debug host.

## Current validation snapshot

Validated on 2026-08-06 with `lambda-linux-arm64:latest` and
`lambda-linux-dev`:

- ARM64 Linux CLI smoke test passed.
- Linux build and focused native test linking passed.
- Input baseline: **2,104/2,104 passed**.
- Functional baseline with the test-only sanitizer options: **3,514/3,590
  passed**.
- Core MIR emission, forced-GC, JS coercion, TypeScript, scalar comparison,
  error-system, structured, and input suites passed.

The remaining failures are not linker failures. They are currently limited to
one MIR size-budget delta, Linux `libm` floating-point golden differences,
`node:module`/DOM-library cases, and graph/render/procedural fixtures that do
not complete in this headless container. Do not update golden files or MIR
budgets solely to make this environment green; first establish whether the
Linux result is the intended platform behavior.

## Troubleshooting

### Source changes are not reflected

Confirm the mount and working directory:

```bash
container exec lambda-linux-dev bash -lc 'pwd; git status --short'
```

After build-configuration or generator changes, regenerate Premake and rerun
the target build. After Dockerfile or dependency changes, rebuild the image;
the existing container does not automatically adopt a new image.

### Linker errors mention macOS or Homebrew archives

Do not copy `mac-deps` archives into the Linux build. Rerun the Linux
dependency setup or rebuild the image so Linux ELF archives are installed.
Use the configured Linux paths in `build_lambda_config.json`.

### Temporary files

Follow the repository rule: use `./temp/` inside the checkout for temporary
files and test artifacts. Do not write project temporary files to `/tmp`.

### Stop or remove the container

Stopping preserves the container and its installed state:

```bash
container stop lambda-linux-dev
```

Create a new container from the image if the container itself becomes
corrupted. Preserve any wanted files in the bind-mounted Linux checkout before
removing a container.
