# macOS ARM64 release size baseline

This is an informative build measurement. D9.1.1v4 assigns separate owners to Lambda, Input and Radiant; D9.4.6 defines the host export allowlist.

The evidence files below are under [`temp/release-size-baseline/`](../../temp/release-size-baseline/); this temporary directory can be cleaned independently of this report.

## Artifact and provenance

- Mapped executable: [lambda-mapped](../../temp/release-size-baseline/lambda-mapped), 30,190,544 bytes (28.79 MiB); SHA-256 `5a734f649878ba3f7241b757ee8ff875625e7a530b197d14baa32959a5e34a9d`.
- Link map: [lambda.linkmap](../../temp/release-size-baseline/lambda.linkmap); SHA-256 `6d3c6e83e653b68afc98bd7dbf89353be01552759b1bf8ae84198f26228ca407`.
- Original fresh split-release executable: [lambda](../../temp/release-size-baseline/lambda), 30,190,536 bytes; SHA-256 `d7bd65c05d5f5f00df24f23ef3f535208bf6c94871e98b4e35e289e8dbae99b5`. All 30 file-backed Mach-O sections match the mapped relink byte for byte; the file lengths and other metadata differ.
- Lambda HEAD changed during the build from `e5d5e848e021ebb6565f97c1722b89530e23a9fd` to `0a95bb4145bae64b5f9649a403ba21c3f8daf43c`. The change was entirely documentation, Lambda scripts and script tests; see [revision-change.txt](../../temp/release-size-baseline/revision-change.txt). The prior dirty files were those same changes.
- Input `1075914a6e3da80fd254399b7f1fa8a57a3b975d`; Radiant `ea6ec1498d51f6218a0ad3e8050b46975f8e3782`. Both sibling working trees were clean before and after the build.
- Build: normal split release with `-O3`, full LTO, dead stripping and release exports. Relink used the same generated release makefile with the added `-Wl,-map` flag. [archive-hashes.txt](../../temp/release-size-baseline/archive-hashes.txt) records all 29 archive paths seen in the map and their post-link SHA-256 hashes. These are provenance records, not retained copies of the archives.
- The link completed with 1,231 warnings, including Homebrew objects built for macOS 26 while targeting 15.7; this is not a distribution compatibility qualification. Both executables passed `--help` and the host-export check (284 listed, zero modules checked); the mapped executable passed `codesign --verify`. No regression suite or performance benchmark was run.

## File-backed symbols by link-map owner

The table sums sizes of live link-map symbols in file-backed sections. It excludes `__bss`, `__thread_bss`, and `__common`. Counts are final output sizes attributed by the linker, not archive file sizes or predicted savings from removing a component. LTO may merge or inline code between origins, and 1.46 MiB was assigned to the linker's LTO object rather than a source archive. Section padding, the Mach-O headers, and `__LINKEDIT` are outside these rows. Mapped file-backed symbols total 29,741,675 bytes (98.5% of the file).

| Link-map owner | Bytes | MiB |
|---|---:|---:|
| Lambda runtime and JS archive | 7,860,738 | 7.50 |
| Radiant archive | 5,144,716 | 4.91 |
| OpenSSL libcrypto | 3,010,073 | 2.87 |
| Input parser archive | 2,770,424 | 2.64 |
| Image codec archive | 2,220,239 | 2.12 |
| Input utility library, including SQLite | 1,886,757 | 1.80 |
| LTO-generated/unassigned object | 1,531,931 | 1.46 |
| Lambda–Radiant adapter | 1,126,256 | 1.07 |
| ThorVG | 1,028,843 | 0.98 |
| MIR | 395,896 | 0.38 |
| utf8proc | 341,539 | 0.33 |
| curl | 318,675 | 0.30 |
| mbedcrypto | 254,574 | 0.24 |
| archive readers | 219,601 | 0.21 |
| RE2 | 202,964 | 0.19 |
| mbedTLS | 190,148 | 0.18 |
| OpenJPEG | 178,199 | 0.17 |

See [groups.tsv](../../temp/release-size-baseline/groups.tsv) for every group and section, [file-backed-objects.tsv](../../temp/release-size-baseline/file-backed-objects.tsv) for members, [symbols.tsv](../../temp/release-size-baseline/symbols.tsv) for live symbols, and [sections.tsv](../../temp/release-size-baseline/sections.tsv) for coverage. The reusable [link-map analyzer](../../utils/analyze_macho_linkmap.py) can regenerate these reports with `python3 utils/analyze_macho_linkmap.py temp/release-size-baseline/lambda.linkmap --output-dir temp/release-size-baseline/analysis`. The raw map remains alongside the evidence. The map's 32,214,239 total symbol bytes include zero-fill memory and therefore must not be treated as file size.

## High-value observations and experiments

1. `sqlite3.c.o` accounts for 1,041,171 file-backed bytes within the input utility archive. A profile that excludes SQLite or a narrower supported SQLite build is a candidate; preserving the full profile's database contract is essential. Its member size is an upper-bound indicator, not a measured removable delta.
2. OpenSSL `libcrypto` accounts for 2.87 MiB. A single SM2 precomputation table occupies exactly 524,288 bytes. Both OpenSSL and mbedTLS are linked, and Lambda's crypto code calls both; evaluate a feature-trimmed OpenSSL build or a backend consolidation only after establishing the required algorithms, provider loading and Node compatibility. The table size is not proof that disabling it preserves functionality or saves exactly that amount.
3. Image codecs total 2.12 MiB, plus 0.17 MiB OpenJPEG. The AV1 `quant_common.o` member accounts for 204,760 bytes and TIFF fax tables are also visible. Measure a controlled reduced-codec profile, while preserving supported format behavior in the full product.
4. The runtime archive includes at least 2,781,230 bytes in members named `js_*.o` and 1,642,291 bytes in `mvp_*.o`; these name-based subsets omit shared and LTO-assigned code. Existing CLI and headless profiles should be rebuilt at the same revisions for a real end-to-end feature delta.
5. Radiant and its adapter together contribute 6.0 MiB of attributed file-backed symbols. Its audio bus member alone accounts for 336,888 bytes. Measure a headless or feature-scoped variant before inferring what can be removed, since render and layout code share dependencies.
6. Evaluate `-Os`/`-Oz` in a separate full rebuild with the same linked inputs and representative performance tests. The existing release is already built with LTO, `-dead_strip`, local symbol stripping and `-O3`; no size or performance claim is established for other optimization levels.

Do not sum the proposed opportunities as savings: symbols can be shared, inlined, retained by exports or moved by the linker. Validate each candidate using a controlled size delta, feature tests and representative performance checks. Ownership boundaries apply to changes in input and Radiant.
