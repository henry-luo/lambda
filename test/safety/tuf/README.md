# Public signed update fixtures

Authority: **D7.5.8**. `../generate_tuf_fixtures.py` creates the public fixtures
with fresh test-only keys and an independent Python OLPC canonical encoder.
OpenSSL signs Ed25519, RSA-PSS/SHA-256 and P-256/SHA-256 vectors. Only public
metadata, signatures, public keys and an inert binary artifact are checked in.
Private test keys and signing inputs stay under repository `temp/`; no
keychain, publisher identity, account settings or remote endpoint is used.

Regenerate from the repository root:

```sh
TMPDIR="$PWD/temp/app-safety-build-tmp" PYTHONDONTWRITEBYTECODE=1 \
  python3 test/safety/generate_tuf_fixtures.py --output test/safety/tuf/fixtures
```

Update the explicit bootstrap SHA-256 fixture pin in
`test/lambda/safety_tuf.ls` to the generator's reported `root_sha256`, then run
all eight `safety_signature`/`safety_tuf*` goldens in interpreter, automatic and JIT modes.
Regeneration changes signatures/keys, not expected acceptance or denial.

The scenarios exercise both authentic and malicious signed transitions:
root continuity/sequential versions, multi-key thresholds, incorrect key-ID
aliases with valid signatures, old-root expiry,
timestamp rollback/freeze, snapshot rollback/removal, consistent snapshots,
large integer custom data, artifact tampering, ordered delegation fallback,
terminating delegation, terminating ancestry cycles and path/hash-prefix restrictions.
These are focused checks; they do not replace the TUF conformance suite,
host composition, crash/extraction/activation or deployed sandbox qualification.
The independent suite, remaining failures and reproducible test adapter are
documented in [Conformance.md](Conformance.md).
Case `encoded` uses the signed role `../名#?role`. Each scenario's `routes.json`
maps public encoded URIs to inert filesystem assets; fixtures never derive
local paths from role labels or encoded separators.
