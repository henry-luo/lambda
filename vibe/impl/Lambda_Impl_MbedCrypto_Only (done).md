# mbedCrypto-only native cryptography

**Date:** 2026-11-13
**Status:** Complete; native host, shared verifier and optional Node crypto
module use mbedCrypto only.
**Authority:** D9.1.1v4 / D9.1.3v2 (project ownership), D9.1.5
(Make/Premake), D11.1.2 (native IO boundary), D11.2.3–D11.2.4 (explicit trust).

## Completion record

- User requires OpenSSL removal and mbedCrypto only. Scoped lambda-input source,
  header, test and build/setup changes are authorized. Node compatibility need
  not be preserved; no supplemental Ed25519 provider is introduced.
- Main checkout `master`, Lambda base `cd3f57768`, input base `27a81a3`, plus the
  migration changes. Required sibling verifier commit: `bd1f317974c0381e7597ec7bfb1ca29aff249de0`.
  Concurrent LaTeX work remains untouched.
- Native host and shared verifier migration is implemented and verified. The
  normal `lambda.exe` is rebuilt; the separate debug host remains available at
  `temp/mbedcrypto-only/lambda.exe`. No performance claim is made.
- P-256 signs SHA-256 with deterministic RFC 6979 and canonical low-S 64-byte
  P1363 signatures. Keys: 32-byte big-endian scalar in `[1,n)` and 65-byte
  uncompressed SEC1 point. Signing-key generation uses rejection sampling.
  X25519 remains separate, follows RFC 7748 and rejects low-order/all-zero secrets.
- Shared verifier uses mbedCrypto RSA-PSS/SHA-256 (2048–8192 bits, SHA-256 MGF1,
  salt 32) and P-256. SPKI PEM/DER keys use DER ECDSA; raw SEC1 keys use low-S
  P1363. Malformed/trailing/extra keys and removed Ed25519 fail closed.
- Sync descriptors, credentials and invitations use version 2. Transport names
  and signed CRDT/cloud/document domains identify the new suite. Old credentials
  fail explicitly without replacement. Files, repository objects, snapshots and
  old cloud history remain intact. New pairing/state and a separately pinned new
  cloud space are required; automatic trust/history conversion is not implemented.
- Native build/setup dependencies are clean across platform configurations;
  macOS/Linux/Windows generation tests reject OpenSSL reintroduction. Native host
  and shared-verifier archive symbol/linkage audit pass.
- Verification: final Lambda baseline 7,474/7,474; 85 exact sync goldens per AUTO,
  interpreter and JIT; 42 focused ordinary/forced-GC crypto/identity/TUF/admission
  checks; input signature 4/4, digest 9/9, dependency 11/11; generator checks pass.
  Peer, real DataChannel, interruption recovery and LAN demo process tests pass.
- One peer rerun received unexplained SIGKILL during the concurrent aggregate;
  its standalone retry passed unchanged. Recorded as qualification residue in the
  Lambda issue ledger; no cause or general fix is claimed. LI-015 remains separate.
- User explicitly authorized removing Node OpenSSL fallbacks. The dynamic loader,
  provider symbol tables and dependent DSA/EdDSA/RSA fallback paths are removed.
  Native mbedCrypto/PSA paths remain. Runtime reporting no longer claims OpenSSL.
  Module/host binary audit finds no OpenSSL dependency or symbols; node-crypto
  has no dlopen/dlsym imports or libcrypto string. ERR_OSSL_* strings remain
  compatibility error labels only.
- Forced-GC qualification exposed pre-existing native lifetime gaps in KeyObject
  factories, RSA details/pairs, one-shot operation wrappers and signature results
  across allocating cleanup. Fixed with precise RootFrame/Rooted ownership
  (D6.3.3) and recorded in the Lambda issue ledger.
- Final full JS suite: 576/576 after the rooting fixes. Both crypto fixtures pass
  forced GC/poison through GTest; the new profile passes three normal and three
  forced-GC direct runs. The 7,474 baseline completed before the final rooting
  edits; final affected-area JS checks qualify those edits without rerunning
  unrelated suites. All native/sync algorithm and protocol inputs remain unchanged.
- One GC retry overlapped rebuild/aggregate activity and exited 137 with empty
  output. Subsequent isolated checks pass unchanged; no cause or general SIGKILL
  fix is claimed. Failed artifacts are preserved alongside successful runs.
- No required work or approval remains. Real physical-device/cross-NAT and other
  OS distribution qualification remain outside these local functional checks.

## Evidence and limits

Artifacts under `temp/mbedcrypto-only/`:

- `provider-build.log`, `node-crypto-build-final.log`, `node-crypto-roots-build.log`.
- `lambda-baseline-final.log`, `lambda-baseline-final-summary.json`: 7,474 passed.
- `js-final-gtest.log`: final 576 passed; `js-crypto-gc-gtest.log`: 2 GC cases.
- `node-profile-{normal,gc}-{0,1,2}.out/.err`: six exact profile runs.
- `node-mbed-only-gc.out/.err`: pre-fix failure; empty `node-mbed-only-gc-fixed`
  outputs and exited command retain the overlapping-run SIGKILL observation.
- `sync-auto-final.log`, `sync-interp.log`, `sync-jit.log`: 85 goldens each.
- `crypto-matrix-final.log`: 42 exact checks across tiers and GC modes.
- `input-signature-tests.log`, `input-digest-tests.log`,
  `input-dependency-tests.log`, `premake-tests.log`.
- `peer-standalone.log`, `rtc-process-final.log`, `demo-process-final.log`.
- `peer-process-final.log`: preserved failed concurrent peer run (SIGKILL).
- `native-linkage-final.json`, host/module `*-otool-final.txt`,
  `source-identity-final.json`.

Native tests use local macOS processes, including a non-loopback host interface,
not two physical devices. Other OS runtime/distribution and cross-NAT qualification
remain separate. Existing Homebrew deployment-minimum warnings remain visible.
The external OpenSSL CLI is retained solely as an independent fixture/certificate
test oracle; the native application does not link it or invoke it for crypto.
Public TUF fixtures now use named-curve P-256 SPKI. A retained Ed25519-only root
verifies fail-closed threshold handling. No private keys ship in fixtures.

## Design and implementation

Runtime primitives remain small C++ bindings; sync identities, grants, protocols,
credential policy and versioning remain Lambda Script (D11.1.2). Input owns the
shared verifier. Existing AES-GCM, HMAC, hashes and entropy continue to use the
same mbedCrypto stack as curl and RTC. No vendor sources are changed.

The credential AES-GCM wrapper retains its original format/AAD. Its inner version
is checked before signing scalars are interpreted, so historical Ed25519 seeds
cannot be relabeled as P-256. Old peer/cloud signatures never become new grants.
See [transition procedure](../../lmd/package/sync/PEER.md#crypto-suite-transition).
