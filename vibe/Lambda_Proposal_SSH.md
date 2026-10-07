# Lambda Proposal: SSH File Transfer (SCP/SFTP) through libcurl

- **Date:** 2026-10-06
- **Status:** PROPOSAL — not ruled, not implemented. Findings come from Lambda's build scripts and `build_lambda_config.json`, the libssh2 1.11.1 and libssh 0.11.3 sources and changelogs, curl master's `lib/vssh/`, and size measurements of the installed Homebrew archives. No SSH-enabled build was made.
- **Scope:** adding `sftp://` and `scp://` to the libcurl that Lambda links on macOS, Linux and Windows: which SSH library, which crypto backend, the licence and size of each choice, and the Lambda-side work. Interactive SSH (remote shell, command execution, port forwarding) is out of scope for the curl route; §3.6 names the one option that could cover it.
- **Formal authority:** no `S#`/`D#` ruling covers SSH or the set of URL schemes Lambda acquires. Related rulings: S14.3.1 (`input()` and `stream()` are symmetric over the same source specifiers, so an `sftp://` URL would become a specifier for both); D7.1.6 (`lambda-cli` carries the third-party libraries its io needs, so it would carry the SSH library too); D7.1.7v6 (`lambda-wasm` excludes all network access including curl, so it is unaffected).
- **ID series:** networking has no ledger series yet, so this proposal mints `SSH#` (`doc/Doc_Convention.md` §4). The open decisions are `SSH1`–`SSH6` (§7).

## 0. Question and answer

**Question.** Can Lambda's curl support SSH, what does it need, and can it be built on the mbedTLS that Lambda already links?

**Answer.** curl supports two file-transfer protocols that run over SSH, SCP and SFTP, and nothing else over SSH. It needs an SSH library: libssh2 or libssh (curl master no longer has a wolfSSH backend). Both libraries can use mbedTLS for their crypto, so no second crypto library is needed. The trade-off is between them. libssh2 on mbedTLS is BSD-licensed and smaller, but it has no Ed25519, curve25519 or AES-GCM, which breaks connections for users whose `known_hosts` entries or key files are Ed25519 (the OpenSSH default). libssh on mbedTLS keeps those algorithms through bundled code, but it is LGPL-2.1 and has roughly 1.6× the code. Today no Lambda platform has SSH in its curl.

## 1. Current state

### 1.1 Builds

| Platform | How curl is built and linked | SSH today |
|---|---|---|
| macOS | `setup-mac-deps.sh` (`build_curl_with_http2_for_mac`) builds curl 8.10.1 statically on mbedTLS 3 with `--without-libssh2`. `build_lambda_config.json` lists `ssh2` in the macOS `exclude_libraries`. `curl-config --protocols` reports FILE FTP FTPS HTTP HTTPS IPFS IPNS. | No |
| Linux | Dynamic `-lcurl`. `setup-linux-deps.sh` (`build_curl_without_libpsl_for_linux`) rebuilds it on OpenSSL with `--without-libssh2` when the system libcurl has PSL; otherwise the distro's libcurl is used. | No after the rebuild; distro-dependent otherwise |
| Windows | `setup-windows-deps.sh` (`build_minimal_static_libcurl`) builds curl 8.10.1 statically on Schannel with `--without-libssh2 --without-libssh`, then checks that no SSH objects landed in `libcurl.a`. | No |

The Windows library list (`platforms.windows.libraries` in `build_lambda_config.json`) still names `ssh2` (`/clang64/lib/libssh2.a`, described as "curl SSH dependency"). That libcurl references no libssh2 symbol, so the entry is unused. The macOS system `/usr/bin/curl` (8.7.1) has no SCP or SFTP either.

### 1.2 Lambda code

- `input_from_url` (`lambda/input/input.cpp:1579`) dispatches only `URL_SCHEME_FILE`, `URL_SCHEME_HTTP`/`URL_SCHEME_HTTPS` and `URL_SCHEME_SYS`.
- `UrlScheme` (`lib/url.h:13`) has no SFTP or SCP member. FTP has one, but `input_from_url` does not dispatch it.
- The CLI accepts only `http://` and `https://` as remote inputs (`lambda/main.cpp:244`, `:1591`, `:3734`, `:3932`, `:4043`).
- `download_http_content` (`lambda/input/input_http.cpp:131`) is the only fetch path, and it sets HTTP-only options (request headers, redirects, compression, TLS verification).

## 2. What curl needs for SSH

- **An SSH library.** curl master's `lib/vssh/` holds `libssh2.c` and `libssh.c` and no other backend (checked 2026-10-06). curl 8.10.1, the version Lambda builds, takes them through `--with-libssh2` and `--with-libssh`.
- **No coupling to curl's TLS backend.** curl's TLS backend and the SSH library's crypto backend are independent. Mixing them (curl on mbedTLS, libssh2 on OpenSSL) works, but it links two crypto libraries.
- **Host-key checking is opt-in in libcurl.** libcurl checks the server's host key only when `CURLOPT_SSH_KNOWNHOSTS`, a fingerprint option (`CURLOPT_SSH_HOST_PUBLIC_KEY_SHA256` or `_MD5`) or a key callback is set. Otherwise it logs "no knownhosts file configured" and proceeds. The `curl` command defaults to `~/.ssh/known_hosts`; libcurl does not.
- **Authentication** is configured with `CURLOPT_SSH_AUTH_TYPES`, `CURLOPT_SSH_PRIVATE_KEYFILE`/`CURLOPT_SSH_PUBLIC_KEYFILE`, `CURLOPT_KEYPASSWD`, ssh-agent, or a password in the URL.

## 3. Options

### 3.1 Option A — libssh2 on mbedTLS

**Build.** Build libssh2 1.11.1 (the current release) from source with `-DCRYPTO_BACKEND=mbedTLS` (CMake) or `--with-crypto=mbedtls` (autotools), against Homebrew `mbedtls@3`. Version 3.6.5 is installed, and `/opt/homebrew/lib/libmbed*.a`, which Lambda links, points at it. Then configure curl with `--with-libssh2=<prefix>`. Homebrew's own `libssh2.a` cannot be reused, because it is built on `openssl@3`.

**Algorithms.** From the feature macros in libssh2 1.11.1 `src/mbedtls.h` and `src/openssl.h`:

| Feature | mbedTLS backend | OpenSSL backend |
|---|---|---|
| RSA keys, including rsa-sha2-256/512 | yes | yes |
| ECDSA keys; ECDH key exchange on nistp256/384/521 | yes (needs `MBEDTLS_ECDSA_C`, which Homebrew's build enables) | yes |
| Ed25519 keys | **no** | yes (OpenSSL ≥ 1.1.1) |
| curve25519-sha256 key exchange | **no** (its functions are declared only under `LIBSSH2_ED25519` in `src/crypto.h`) | yes |
| AES-CTR, AES-CBC, 3DES | yes | yes |
| AES-GCM | **no** | yes |
| chacha20-poly1305 | no (no libssh2 backend implements it) | no |
| HMAC-SHA2-256/512 | yes | yes |

**What breaks.**

- Connections to a stock OpenSSH server work, because it also offers ECDSA and RSA host keys, ECDH key exchange on the NIST curves, and AES-CTR.
- A server restricted to curve25519, Ed25519, chacha20 or AES-GCM (a common hardened `sshd` setup) shares no algorithm with this build, and the handshake fails.
- `~/.ssh/id_ed25519` cannot be used for public-key login. OpenSSH's `ssh-keygen` has generated Ed25519 keys by default since 9.5, so many users would need an extra RSA or ECDSA key.
- **The `known_hosts` trap.** When `CURLOPT_SSH_KNOWNHOSTS` is set and no fingerprint option is, curl's `ssh_force_knownhost_key_type` (`lib/vssh/libssh2.c`) pins the host-key algorithm to the type of the `known_hosts` entry it finds for the host. For an `ssh-ed25519` entry it asks libssh2 for `ssh-ed25519`. libssh2 without Ed25519 rejects that method, and curl fails the transfer ("libssh2 method 'ssh-ed25519' failed"). The OpenSSH client prefers Ed25519 host keys, so most existing `known_hosts` files hold exactly such an entry. This comes from reading curl master; it was not reproduced. The workarounds are to pin fingerprints with `CURLOPT_SSH_HOST_PUBLIC_KEY_SHA256` (which skips the pinning) or to have Lambda keep its own `known_hosts`.

**mbedTLS version risk.** mbedTLS 3.6.0 removed `mbedtls_pk_load_file` from its public API; libssh2 needs it to load ECDSA private keys. libssh2 1.11.1 redeclares the now-internal function on its own side, noting that it "won't [be] removed in mbedTLS 3.6 LTS" (libssh2 `NEWS`, 2024-07-14). This works on the 3.6 LTS line Lambda uses. A later mbedTLS that drops the symbol would break the build.

**Licence.** BSD-3-Clause. Binary distributions must carry the copyright notice and licence text, and the authors' names may not be used to endorse Lambda.

### 3.2 Option B — libssh on mbedTLS

**Build.** Build libssh 0.11.x (0.11.3 is installed; Homebrew's current formula is 0.12.2) with CMake `-DWITH_MBEDTLS=ON`, then configure curl with `--with-libssh=<prefix>`. libssh added mbedTLS 3 support in 0.10.0 and mbedTLS 3.6 support in 0.11.0 (libssh `CHANGELOG`).

**Algorithms.** libssh fills mbedTLS's gaps with bundled code (libssh 0.11.3 `src/CMakeLists.txt` and `src/libmbedcrypto.c`):

- Ed25519 keys are always compiled from the bundled `external/ed25519.c`, `fe25519.c`, `ge25519.c` and `sc25519.c` on the mbedTLS backend.
- curve25519 key exchange comes from the bundled `external/curve25519_ref.c` whenever OpenSSL is absent.
- AES-GCM comes from mbedTLS when `MBEDTLS_GCM_C` is set, which it is in Homebrew's build.
- chacha20-poly1305 comes from mbedTLS when `MBEDTLS_CHACHA20_C` and `MBEDTLS_POLY1305_C` are set (both are), and otherwise from the bundled `external/chacha.c` and `poly1305.c`.
- RSA, ECDSA, AES-CTR/CBC and 3DES come from mbedTLS as usual.

Option B therefore works with Ed25519 `known_hosts` entries, Ed25519 key files and hardened servers. libssh also reads `known_hosts` itself, so the libssh2-specific pinning in §3.1 does not apply.

**Licence.** LGPL-2.1-or-later, plus an exception that permits linking with OpenSSL (irrelevant on mbedTLS). Statically linking libssh into `lambda.exe` triggers LGPL-2.1 §6: binary distributions must ship the LGPL text and libssh's source (or a written offer for it), and users must be able to relink Lambda against a modified libssh. Linking libssh as a shared library instead satisfies §6(b), at the cost of a runtime library dependency. Lambda already has a worked precedent: RDB8 (`vibe/Lambda_IO_RDB.md` §13.8) ships MariaDB Connector/C (LGPL-2.1) with a `LICENSES/` directory, a generated `SOURCES.md` that pins versions and gives relink instructions, an archive-override relink path in the make target, and a licence gate (`utils/verify_rdb_module_licenses.py`). The difference is placement: RDB8 confines the LGPL code to the separately loaded `rdb-drivers` Jube module, whereas curl, and with it libssh, links into the main executable. The relink path would have to cover `lambda.exe` itself (SSH2). This reading is engineering, not legal advice.

### 3.3 Option C — libssh2 on OpenSSL

Use Homebrew's `libssh2.a` as built (on `openssl@3` 3.6.2) and link OpenSSL's `libcrypto` next to mbedTLS. This gives libssh2's full algorithm set (Ed25519, curve25519, AES-GCM) under permissive licences only: BSD-3-Clause for libssh2 and Apache-2.0 for OpenSSL 3. The cost is a second crypto library. OpenSSL's `libcrypto.a` is 8.6 MB with 3.4 MB of code; that is an upper bound, because the linker drops unreferenced objects, but the linked share was not measured. It also means two crypto stacks to track for security fixes. Moving curl's TLS to OpenSSL as well, so that mbedTLS could be dropped, would undo the mbedTLS choice and is out of scope.

### 3.4 Option D — wolfSSH (rejected)

Not viable. curl master no longer has a wolfSSH backend, and wolfSSH needs wolfSSL, which is GPL-3.0-or-later (Homebrew `wolfssl` 5.9.4) unless commercially licensed. Linking it would put the distributed `lambda.exe` under GPLv3.

### 3.5 Option E — no library; call the system SSH tools

Lambda can already run `scp`, `sftp` or `ssh` through `cmd()` (`doc/Lambda_Sys_Func.md`). This costs no size or licence obligation and gets OpenSSH's full algorithm and configuration support (`~/.ssh/config`, ssh-agent, jump hosts). However, it depends on OpenSSH being installed, it does not work through `input()` or `stream()`, and the scripts that use it shell out instead of naming a URL. It is the baseline the other options have to beat.

### 3.6 Option F — an SSH Jube module on libssh, without curl

A separately loaded module, shaped like `rdb-drivers`, that uses libssh directly. It keeps the LGPL code out of `lambda.exe` exactly as RDB8 does, and it is the only option that could also offer remote command execution, which curl cannot. The cost is writing the SFTP/SCP client code that curl would otherwise provide, plus a module API. For `input("sftp://…")` to work, the host's `input()` would have to reach a module-provided scheme; whether the D7.4 module contracts allow such a hook was not checked.

## 4. Size

Measured on 2026-10-06 on macOS arm64 from the installed Homebrew static archives. "Code" is the sum of `__TEXT` over the archive's members (from `size`). Static linking pulls in whole object files, so code size is closer to what lands in the binary than the archive file size, which also counts symbol tables.

| Library | Version | Crypto backend of the measured build | Archive file | Code |
|---|---|---|---|---|
| libssh2 | 1.11.1 | OpenSSL | 347,544 B | 183,322 B |
| libssh | 0.11.3 | OpenSSL | 790,448 B | 318,973 B |
| OpenSSL `libcrypto` | 3.6.2 | — | 8,571,216 B | 3,393,138 B |
| OpenSSL `libssl` | 3.6.2 | — | 1,462,304 B | 568,462 B |
| mbedTLS (crypto + tls + x509), already linked | 3.6.5 | — | 1,264,992 B | 584,672 B |
| libcurl, already linked (macOS) | 8.10.1 | mbedTLS | 783,112 B | 352,938 B |

Adjustments for the options:

- No mbedTLS build of either SSH library was measured. The OpenSSL-specific code is small in both: libssh2's backend unit `crypto.o` has 21,550 B of code, and libssh's `libcrypto.c.o` plus `pki_crypto.c.o` have 20,417 B. The mbedTLS units replace these.
- libssh's archive includes server-side units (`server.c.o`, `bind.c.o`, `bind_config.c.o`, `sftpserver.c.o`; 29,485 B of code) that a client-only curl link drops, which leaves about 289 KB. Option B adds the bundled Ed25519 and curve25519 code on top of that; it was not measured.
- Options A and B add almost no crypto code, because the mbedTLS primitives they use (bignum, RSA, ECDSA/ECDH, AES, SHA-2) are already linked for HTTPS.
- Every curl-based option also adds curl's own SSH glue (`lib/vssh/libssh2.c` or `lib/vssh/libssh.c`); it was not measured.

Estimated code added to `lambda.exe`:

| Option | Estimated code added |
|---|---|
| A — libssh2 on mbedTLS | about 180 KB, plus curl's SSH glue |
| B — libssh on mbedTLS | about 290 KB, plus bundled Ed25519/curve25519 and curl's SSH glue |
| C — libssh2 on OpenSSL | about 180 KB, plus up to about 3.4 MB of `libcrypto` |
| E — system tools | none |
| F — Jube module | none in `lambda.exe`; about 290 KB plus client code in the module |

## 5. Summary

| | A: libssh2 + mbedTLS | B: libssh + mbedTLS | C: libssh2 + OpenSSL | E: system tools |
|---|---|---|---|---|
| SSH library licence | BSD-3-Clause | LGPL-2.1-or-later | BSD-3-Clause | n/a |
| Crypto library licence | Apache-2.0, already linked | Apache-2.0, already linked | Apache-2.0, newly linked | n/a |
| Distribution duty | notice and licence text | LGPL text, source, relink path | notices for both libraries | none |
| Code added | about 180 KB | about 290 KB or more | about 180 KB plus up to 3.4 MB | none |
| Ed25519 keys and host keys | no | yes | yes | yes |
| curve25519 / AES-GCM / chacha20 | no / no / no | yes / yes / yes | yes / yes / no | yes / yes / yes |
| Works with a typical `~/.ssh` | often not (§3.1) | yes | yes | yes |
| Reachable through `input()` / `stream()` | yes | yes | yes | no |

Upstream mbedTLS 3.x is dual-licensed Apache-2.0 OR GPL-2.0-or-later; Homebrew records it as Apache-2.0, which is the licence Lambda already uses it under. curl itself is under the curl licence (MIT-style) in every option.

## 6. Lambda-side work (any of A–C)

1. **Build.** On macOS: add a libssh2 or libssh build step to `setup-mac-deps.sh` against `mbedtls@3`, reconfigure curl with `--with-libssh2=` or `--with-libssh=`, and replace the macOS `ssh2` exclusion in `build_lambda_config.json` with an entry for the new archive. On Linux: decide whether to keep the system libcurl or build one with SSH (SSH3). On Windows: rebuild the Schannel libcurl with an SSH library. libssh2 also has a WinCNG backend that needs no extra crypto library on Windows (its algorithm set was not checked), and the currently unused `ssh2` entry would then be used.
2. **URL scheme.** Add SFTP and SCP to `UrlScheme` (`lib/url.h`) and its parser, dispatch them in `input_from_url`, and widen the `http(s)` checks in `lambda/main.cpp`.
3. **Fetch path.** `download_http_content` mixes the curl transfer with HTTP-only options. Split out the shared transfer core instead of copying it for SSH (CLAUDE.md rule 13).
4. **Host keys (SSH4).** Always set `CURLOPT_SSH_KNOWNHOSTS` (default `~/.ssh/known_hosts`) and fail on an unknown or changed host key. Never fall back to libcurl's unchecked default.
5. **Credentials (SSH6).** Prefer key files and ssh-agent. A password in the URL must never reach `log.txt`. `download_http_content` already logs the URL on failure, which is the same defect RDB9 found for database URIs (`vibe/Lambda_IO_RDB.md` §13.9).
6. **Tests.** Add a gtest against a local `sshd` on a high port with a generated host key and user key, skipped when `sshd` is unavailable.

## 7. Open decisions

- **SSH1 — library and backend.** Choose A, B, C, E or F. Recommendation: B if SSH should work with users' existing OpenSSH setup, since Ed25519 is the default for both keys and host keys; A only if LGPL is unacceptable and Lambda manages host-key fingerprints itself; E as the zero-cost interim.
- **SSH2 — LGPL placement (B only).** Link libssh statically into `lambda.exe` with an RDB8-style licence and relink package, link it as a shared library, or isolate it in a module (Option F).
- **SSH3 — platform parity.** Use the same library and backend on all three platforms, or accept differences (for example the Linux system libcurl, or WinCNG on Windows).
- **SSH4 — host-key policy.** Accept `known_hosts` only, or also accept fingerprints pinned per call.
- **SSH5 — surface.** Start with read-only access (`input()`, `stream()`), or also support upload and output over SFTP.
- **SSH6 — credentials.** Decide which authentication methods to allow, and whether passwords in URLs are accepted or rejected.
