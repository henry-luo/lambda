# Native sync dependency build

**Date:** 2026-11-13
**Status:** Implemented; macOS native verification complete. Other-OS builds pending.
**Authority:** D9.1.5 (Make/Premake), D9.1.1v4 / D9.1.3v2 (ownership),
D9.3.4 (native IO mechanisms), D11.1.2 (IO boundary).

## Current checkpoint

- Scope: use mbedTLS for libdatachannel, disable plog logging in release dependency
  builds, and prepare connectivity from each platform setup wrapper.
- Main checkout, `master`, starting after `2a1b1ec72`; HEAD advanced to `8b5ebe56e`
  during concurrent work. Unrelated LaTeX changes remain untouched.
  No sibling/vendor source changes. Prepared for commit at the user's request.
- Replaced the previous CMake helper with a Lambda-owned JSON dependency recipe,
  executed by the existing shared Premake/Make builder (D9.1.5).
- Keep libdatachannel v0.24.6 at `6b1e2e620f1e37f0eafeee702eaea0043cb305fd`
  and its pinned submodules. Sources are under `temp/sync-connectivity/`.
- Upstream requires plog headers but supports `PLOG_DISABLE_LOGGING`. Setup builds
  release archives with those statements disabled; no vendor edits are required.
- The helper selects the host's mbedTLS 3 headers/archives, validates their presence
  and version, and records the provider and recipe hash in provenance. The RTC-only
  OpenSSL SSL library is removed; existing public-key primitives retain OpenSSL.
- All three platform wrappers run RTC preparation after input's mbedTLS setup.
  Dry-run prints this step, and errors propagate before later setup work.
- `make build-sync-connectivity` fetches/prepares the required pinned submodules,
  builds the three static archives and installs into `build/deps/connectivity/`.
  It supports incremental repeat runs. The helper no longer accepts `--openssl`.
- macOS archive build, standalone Make target, four setup tests, generator
  regression checks, 12 focused RTC goldens and the three process assertions pass.
  The host source remains unchanged; only build configuration/dependencies changed.
- Initial ICE failures occurred with the separate test executable's new signing
  identifier. Re-signing that same executable with the normal `lambda.exe`
  identifier made all checks pass; no library/code or OS permission settings changed.
  This is consistent with macOS local-network identity handling. Keep failed logs.
- Existing root `lambda.exe` was not replaced during concurrent work. Verification
  used `temp/sync-connectivity/lambda-mbedtls.exe`, debug host with release RTC
  archives. `make build` relinks the normal host against the new libraries.
- Other-OS native compilation, physical devices, browser interoperability and an
  aggregate Lambda baseline were not run in this dependency-focused phase.
- Verification identities are in `temp/sync-connectivity/mbedtls-verification.json`.
  No further implementation is required for the requested local build/setup scope.

## Acceptance

- Platform setup includes pinned RTC dependencies after the mbedTLS provider is
  installed, propagates failure and supports dry-run without network/build writes.
- Generated dependency Makefiles build three static archives with C++17, mbedTLS,
  no media/WebSocket dependency features, and release logging disabled.
- Native RTC handshake, binary roundtrip, timeouts and cancellation still pass.
- No vendor patches, sibling source changes, CMake invocation or new system service.

## Build and verification

The recipe in `build_deps_config.json` compiles the pinned upstream source sets:
62 C++17 libdatachannel sources, 21 C ICE sources and 23 C SCTP sources. It probes
platform headers and socket fields instead of assuming their layout. Dependency
objects, generated Makefiles, probe receipts and a manifest are under
`temp/dependency-build/<system>-<architecture>/libdatachannel/`. Installation
provides `libdatachannel.a`, `libjuice.a`, `libusrsctp.a` and public headers.
The existing shared dependency builder remains owned by input; it was not edited.

The setup helper verifies the libdatachannel revision and clean pinned submodules,
fetches only plog/usrsctp/libjuice when needed, and avoids media/example-only
submodules on a fresh preparation. Old CMake package exports describing OpenSSL
linkage are removed from the installed prefix. `provenance.json` records mbedTLS,
release/logging/feature choices and source/recipe identities.

| Verification | Result | Receipt under `temp/sync-connectivity/` |
|---|---|---|
| `make build-sync-connectivity JOBS=4` | Installs all three archives; repeat succeeds | `mbedtls-make-target.log`, `mbedtls-prepare.log` |
| `python3 test/test_setup_deps.py` | 4 tests pass; three-platform dry-runs and stubbed live order/failure cases | `mbedtls-setup-tests.log` |
| `python3 utils/test_premake_generator.py` | Passes, including RTC mbedTLS closure / removed SSL dependency | `mbedtls-generator-tests.log` |
| Isolated native host build with `make build-lambda-split` | Passes with mbedTLS RTC archives | `mbedtls-host-build.log` |
| `sync_rtc`, `sync_connectivity_protocol`, `sync_stream_cancel`, `sync_rendezvous` | 12 exact-golden runs pass on AUTO/interpreter/JIT | `mbedtls-rtc-goldens-same-id.log`, per-case `.out/.err` |
| `test/sync_connectivity/test_connectivity.py` | 3 assertions pass: discovery, large binary/offline merge, interrupted recovery | `mbedtls-process.log`, `process-mbedtls-auto/` |
| `nm -u` and archive message inspection | mbedTLS handshake references; no OpenSSL TLS references; exact upstream DTLS log literal absent | `mbedtls-symbols.txt`, verification identity JSON |

The process gate uses `LAMBDA_SYNC_EXE` to select the separate host and
`LAMBDA_CONNECTIVITY_RUN=mbedtls-auto` for isolated evidence. Native tests require
local network/Bonjour access. The initial sandbox denied local connection setup;
native-permission testing then isolated the executable-identifier issue above.
The prior OpenSSL release host passed as a control. Diagnostic logging was enabled
only in a separate temporary recipe/archive; the installed release archive retains
`PLOG_DISABLE_LOGGING`. No temporary diagnostic source is part of the product.

Tests did not change protocol goldens, remove authentication, relax deadlines or
change transport policies to obtain a pass. The separate warm-latency target remains
tracked by [its checkpoint](Lambda_Impl_Sync_Warm_Latency.md).
