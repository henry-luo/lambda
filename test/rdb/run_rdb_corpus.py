#!/usr/bin/env python3
"""RDB driver corpus (vibe/Lambda_IO_RDB.md section 13.11).

Runs every test/rdb/*.ls against each configured backend and compares the
output with its golden: <name>.<backend>.txt when the backend differs for a
real reason, else the shared <name>.txt. Backends come from
LAMBDA_TEST_PG_URI / LAMBDA_TEST_MYSQL_URI (utils/rdb-test-servers.sh up
prints them); an unset backend is reported as skipped, never as passed.

Each backend also runs a TLS-mode matrix (RDB11/RDB12), and every run checks
that the fixture password never reached log.txt (RDB9).

usage: test/rdb/run_rdb_corpus.py [--update]   (--update rewrites goldens)
"""
import os
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit, parse_qsl, urlencode

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "test" / "rdb"
WORK = ROOT / "temp" / "rdb-corpus"
LAMBDA = ROOT / "lambda.exe"
LOG = ROOT / "log.txt"
TIMESTAMP = re.compile(r"^\d\d:\d\d:\d\d ")

BACKENDS = [
    ("postgresql", "LAMBDA_TEST_PG_URI", "sslmode",
     {"disable": "ok", "prefer": "ok", "require": "ok", "verify-ca": "ok", "verify-full": "ok"}),
    # MySQL full authentication needs a TLS guarantee (RDB12); the fixture
    # cache is flushed first by utils/rdb-test-servers.sh `up`, so plaintext
    # modes may succeed once a TLS login has populated the server cache:
    # only the guaranteed modes are asserted
    ("mysql", "LAMBDA_TEST_MYSQL_URI", "ssl-mode",
     {"REQUIRED": "ok", "VERIFY_CA": "ok", "VERIFY_IDENTITY": "ok"}),
]


def run_script(source: str, name: str) -> str:
    WORK.mkdir(parents=True, exist_ok=True)
    script = WORK / name
    script.write_text(source)
    proc = subprocess.run([str(LAMBDA), str(script)], cwd=ROOT, capture_output=True,
                          text=True, timeout=120)
    # results are stdout; stderr carries diagnostics already recorded in log.txt
    lines = [l for l in proc.stdout.splitlines() if not TIMESTAMP.match(l)]
    return "\n".join(lines).strip() + "\n"


def password_of(uri: str) -> str:
    return urlsplit(uri).password or ""


def leaked(secret: str) -> bool:
    return bool(secret) and LOG.exists() and secret in LOG.read_text(errors="replace")


def with_mode(uri: str, key: str, mode: str) -> str:
    """the URI with its TLS mode replaced; verify modes keep their CA option"""
    parts = urlsplit(uri)
    query = [(k, v) for k, v in parse_qsl(parts.query) if k != key]
    if not mode.lower().startswith("verify"):
        query = [(k, v) for k, v in query if k not in ("sslrootcert", "ssl-ca")]
    query.append((key, mode))
    return urlunsplit(parts._replace(query=urlencode(query, safe="/")))


def main() -> int:
    update = "--update" in sys.argv
    scripts = sorted(CORPUS.glob("*.ls"))
    failures, passes, skipped = [], 0, []
    for backend, env, mode_key, modes in BACKENDS:
        uri = os.environ.get(env)
        if not uri:
            skipped.append(f"{backend} ({env} unset)")
            continue
        secret = password_of(uri)
        for script in scripts:
            out = run_script(script.read_text().replace("{{RDB_URI}}", uri),
                             f"{script.stem}.{backend}.ls")
            specific = CORPUS / f"{script.stem}.{backend}.txt"
            golden = specific if specific.exists() else CORPUS / f"{script.stem}.txt"
            if update:
                # a new divergence gets a backend-specific golden; review it
                target = golden if not golden.exists() or golden.read_text() == out or golden == specific else specific
                target.write_text(out)
                print(f"updated {target.relative_to(ROOT)}")
                continue
            label = f"{backend}/{script.stem}"
            if not golden.exists():
                failures.append(f"{label}: missing golden {golden.name}")
            elif golden.read_text() != out:
                failures.append(f"{label}: output differs from {golden.name}\n{out}")
            elif leaked(secret):
                failures.append(f"{label}: password reached log.txt")
            else:
                passes += 1
        if update:
            continue
        probe = 'let db = input("{{URI}}") or "FAILED"\nif (db == "FAILED") "FAILED" else "ok"\n'
        verify_mode = [m for m in modes if m.lower().replace("_", "-").startswith("verify")][-1]
        parts = urlsplit(uri)
        wrong_password = urlunsplit(parts._replace(
            netloc=parts.netloc.replace(f":{secret}@", ":not-the-password@")))
        # a CA that did not sign the server certificate must fail verification
        wrong_ca = "/etc/ssl/cert.pem"
        cases = [(f"tls {mode_key}={m}", with_mode(uri, mode_key, m), e) for m, e in modes.items()]
        cases.append(("wrong password", wrong_password, "FAILED"))
        cases.append((f"wrong CA ({verify_mode})", re.sub(r"(sslrootcert|ssl-ca)=[^&]*",
                      r"\1=" + wrong_ca, with_mode(uri, mode_key, verify_mode)), "FAILED"))
        for name, case_uri, expected in cases:
            out = run_script(probe.replace("{{URI}}", case_uri),
                             f"tls_probe.{backend}.ls").strip().splitlines()
            got = out[-1].strip('"') if out else ""
            label = f"{backend}/{name}"
            if got != expected:
                failures.append(f"{label}: expected {expected}, got {got}")
            elif leaked(secret):
                failures.append(f"{label}: password reached log.txt")
            else:
                passes += 1
    for f in failures:
        print(f"FAIL {f}")
    for s in skipped:
        print(f"SKIP {s}")
    if not update:
        print(f"rdb corpus: {passes} passed, {len(failures)} failed, {len(skipped)} backend(s) skipped")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
