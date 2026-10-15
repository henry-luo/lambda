# Vendored archive input readers

Lambda links a memory-reader subset of [libarchive](https://github.com/libarchive/libarchive)
for [archive input Phase 3](../../vibe/Lambda_IO_Zip.md#10-phase-3-7zip-tar-and-rar-input).
All imported sources retain their upstream basenames directly in this directory.
Existing ZIP input/output keeps the Lambda-owned implementation in `lambda/io/zip_archive.cpp`.

| Provenance | Value |
|---|---|
| Release | [libarchive 3.8.10](https://github.com/libarchive/libarchive/releases/tag/v3.8.10), 2026-10-14 |
| Source archive | [libarchive-3.8.10.tar.xz](https://github.com/libarchive/libarchive/releases/download/v3.8.10/libarchive-3.8.10.tar.xz) |
| Source archive SHA-256 | `56cad765459d5ee25a3b761f90c47d99379d28297fbfb72d908880d6b875fb96` |
| Imported | 2026-10-15 |
| License | Upstream `COPYING` and each file's controlling notices; primarily BSD 2-Clause. |
| Local delta | [libarchive-embedding-policy.patch](../../patches/libarchive-embedding-policy.patch), authorized by the user on 2026-10-15 for direct changes in `lib/zip`. |

## Source closure

There are 58 C/header files and upstream `COPYING`. All except `android_lf.h`
come from the release's `libarchive/` directory; the Android compatibility header
comes from `contrib/android/include/`, and `COPYING` from the release root.
`UPSTREAM_SHA256SUMS` records pristine bytes; `SHA256SUMS` records the imported
files after applying the patch. These manifests and this document are Lambda metadata.

The closure contains 7zip/tar/legacy-RAR/RAR5/raw readers; reader/entry lifecycle,
strings/character conversion, ACL/metadata, CRC, PPMd7, timestamps, random/platform
helpers; and none/gzip/bzip2/xz/zstd filter adapters. RAR5's BLAKE2sp implementation
uses the upstream BLAKE2s/BLAKE2sp reference files and their retained license notices,
without libb2. BLAKE2 self-test vectors are not part of this production closure.

Writers, filesystem extractors, file/fd acquisition adapters, external-program
filters, CLI tools, unrelated containers and aggregate registration helpers are
omitted. Some common headers declare APIs that this subset does not implement.
The staged gzip adapter is excluded from the build: its upstream trailer handling
does not verify CRC/ISIZE. Lambda uses zlib's verified gzip mode instead.

## Embedding policy

**D7.5.2/S12.4.1v3:** Lambda acquires bounded immutable bytes and supplies only
retained memory buffers. The JSON build configuration generates
`build_temp/archive-config/include/lambda_archive_config.h` through upstream's
`PLATFORM_CONFIG_H` hook. Only the selected native readers/filters are registered;
zlib, liblzma, bzip2, zstd and iconv are linked directly. External decompressor
programs and extraction APIs are unavailable.

The authorized patch supplies these controls and correctness fixes:

- A per-reader UTF-8 application charset and optional preservation of Unicode
  spelling, independent of process locale. Entry reuse preserves its owning
  reader; otherwise `archive_entry_clear()` loses that charset context.
- A configurable fatal handler. Upstream's allocation-error `__archive_errx()`
  exits the process; Lambda unwinds to its archive boundary and returns an error.
- Header-time admission of Copy/LZMA/LZMA2 with at most one supported BCJ/Delta
  filter. BCJ2, PPMd, DEFLATE and other 7z methods remain outside this profile.
  A format-output hook accounts for solid predecessor decoding and Copy streams.
- Single-volume policy checks for RAR/RAR5; legacy solid RAR is rejected.
- RAR5 Stored entries, including empty members, advance to checksum finalization.
  CRC presence is tracked separately from its value so a CRC of zero is still checked.
- Strict pax/GNU/RAR/RAR5 names reject embedded NULs before C-string conversion;
  7z symlinks are rejected before their bodies are decoded.

**S14.3.1v3:** 7z and non-solid legacy-RAR bodies stay lazy; compressed tar and
RAR5 bytes are captured and checked inside `input()`. RAR5 uses explicit body
reads, since its skip path can decode solid predecessors without checking CRCs.

Lambda-owned `lib/archive_codec/` tracks all reader allocations and iconv handles,
including partially built C objects left by a fatal bailout. Reader/parser memory
shares `max_index_bytes`; native codec workspace is separately bounded by
`max(max_index_bytes, max_member_bytes)`. Decoded 7z metadata is limited by
`max_index_bytes`. Actual payload output shares the parent's expansion ledger,
including failed/repeated solid decoding. A C `setjmp`/`longjmp` recovery frame
restores thread-local ownership and returns an ordinary failure. Recovery actions contain only trivial C++ stack
locals; all owned scratch is recorded on the backend for explicit cleanup.
The archive subset clears `DEBUG` to prevent upstream RAR5 diagnostic output in application debug builds.

## Verification and updates

Verify the applied sources from the repository root:

```sh
(cd lib/zip && shasum -a 256 -c SHA256SUMS)
```

For an update, download the official release under `temp/`, verify the published
release hash, then copy each `UPSTREAM_SHA256SUMS` basename from its upstream
location without changing bytes. Verify the pristine manifest before applying
`patch -p1 < patches/libarchive-embedding-policy.patch` from the repository root.
Review/rebase the patch explicitly for the new release; update both manifests and
provenance. Compare the resulting sources against pristine upstream plus the
recorded patch. Run the native archive suite, Lambda tier/GC goldens and
`make test-lambda-baseline`.

Further vendor changes follow `AGENTS.md` rule 16: obtain approval and record an
auditable patch. Repository attributes preserve upstream newlines, and the
project-policy linter excludes `lib/zip` while scanning Lambda-owned adapters.
