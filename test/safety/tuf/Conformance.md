# Lambda TUF conformance qualification

Authority: **D7.5.8**, with local pure-function state under **S12.1.2v3**.
`../run_tuf_conformance.py` runs the official upstream client suite against the
production Lambda Script package. `conformance.json` pins the upstream suite
and libfaketime commits and the expected 119-case inventory. No expected-failure
file or test exclusion is applied. Incomplete conformance returns nonzero and
keeps `full_conformance`, delivery and default rollout false.

The current result is **112 passed, 7 failed**. The failures are six unsupported
optional schemes (RSA-PSS/SHA-384 and SHA-512, ECDSA P-384, ML-DSA 44/65/87),
and a historical Sigstore repository using `spec_version`
`1.0` instead of the required SemVer form. No compatibility exception has been
added merely to pass that fixture. This is not a claim of full conformance.

The four unusual-role cases now pass. Role labels are opaque UTF-8 data in the
cache, encoded as single URI components for requests. Test metadata filenames
are separately encoded, and target-cache filenames are hashes of the logical
target. Neither location is selected by a remote raw role/path. Native host
transport also validates the canonical URI spelling before parsing. The
728-input URI differential corpus agrees with Python's standard-library codec
in the native gate, interpreter and JIT. [RFC 3986](https://www.rfc-editor.org/rfc/rfc3986.html),
[reference metadata encoding](https://github.com/theupdateframework/python-tuf/blob/develop/tuf/ngclient/updater.py).

The suite reproduced defects in cache reuse, extra malformed signatures,
impossible child thresholds and expired unchanged polling. A repeated cached
role is reauthenticated against the current delegator rather than inheriting an
earlier edge's authority. Root continuity remains independent of whether a
child role can subsequently meet its threshold. An invalid extra signature
contributes nothing while valid independent keys can still meet the threshold.
An unchanged timestamp cannot revive an expired cached envelope.

The test CLI is `conformance_client.py` and `_conformance_step.ls`. Python only
performs fixture filesystem operations, owned-loopback HTTP and process control;
all signature, schema, version, expiry, delegation and target-hash decisions run
in the production Lambda modules. The runner copies the executable, source
closure and relay into a per-run directory. Its HTTP profile refuses proxies,
credentials, remote numeric peers and redirects. It never opens real settings.
This ordinary test CLI does not prove production confinement, checkpoint
durability or publisher admission; those remain separate native-host gates.

Expiry tests use the unmodified upstream `faketime` binary/dylib and a successful
UTC interposition control. Neither a clock shim nor a changed system clock is
used. Python, OpenSSL test signers and the clock tool are development dependencies;
the deployed Lambda verifier has no Python TUF runtime dependency.

Prepare the isolated tools from the repository root, checking out the exact
commits recorded in `conformance.json` after cloning:

```sh
git clone https://github.com/theupdateframework/tuf-conformance.git temp/tuf-conformance-upstream
git clone https://github.com/wolfcw/libfaketime.git temp/libfaketime-upstream
python3 -m venv temp/tuf-conformance-env
TMPDIR="$PWD/temp/app-safety-build-tmp" temp/tuf-conformance-env/bin/python -m pip install \
  --cache-dir temp/tuf-conformance-pip-cache ./temp/tuf-conformance-upstream
TMPDIR="$PWD/temp/app-safety-build-tmp" make -C temp/libfaketime-upstream \
  PREFIX="$PWD/temp/tuf-conformance-tools" install
python3 -O test/safety/run_tuf_conformance.py
```

The runner needs owned loopback and the clock tool's shared-memory access. It
writes results, immutable runtime inputs, logs and pytest JSON below
`temp/app_safety_tuf_conformance/`. Missing tools, changed upstream checkouts,
missing tests and clock-interposition failure do not produce a passing report.

`python3 -O test/safety/test_tuf_json.py` separately checks a deterministic
688-input strict-JSON/mutation corpus in interpreter and JIT modes against an
independent encoder. It covers UTF-8, escaped/duplicate/NUL keys, surrogates,
large integers, long tokens/containers and malformed/truncated input. The parser
uses loops for flat input length and retains a 64-level nesting limit. The
eight `safety_signature`/`safety_tuf*` goldens also pass with forced collections
and freed-memory poisoning; these are focused gates, not a general fuzz proof.
Run `python3 -O test/safety/test_tuf_paths.py` for URI/native policy agreement.

Sources: [official CLI protocol](https://github.com/theupdateframework/tuf-conformance/blob/main/CLIENT-CLI.md),
[TUF client workflow](https://theupdateframework.github.io/specification/latest/#detailed-client-workflow),
[upstream clock tool](https://github.com/wolfcw/libfaketime).
