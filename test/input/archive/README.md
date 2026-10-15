# Archive input fixtures

The checked-in binaries run without archive utilities or Python packages.
Recreate them with:

```sh
python3 test/input/archive/generate.py temp/archive-vendor/libarchive-3.8.10.tar.xz
```

The generator verifies release SHA-256
`56cad765459d5ee25a3b761f90c47d99379d28297fbfb72d908880d6b875fb96`.
Its `UPSTREAM` table maps each 7z/RAR binary to an independent libarchive 3.8.10
UU-encoded regression fixture. Upstream `COPYING` is retained here. The upstream
7z fixtures were produced by archive tools independently of Lambda's reader;
the RAR fixtures exercise Stored/compressed/solid/CRC32/BLAKE2sp profiles.
`7z-solid.7z` contains 13/26/39/52-byte files sharing a solid LZMA folder.
The generator also writes a plain-header Copy folder with the same four payloads
as `7z-solid-copy.7z`; the system bsdtar independently validates this fixture. `7z-skip.7z` tests skipping a separate 65,536-byte member.
The RAR5 Unicode fixture also contains links and must be rejected by this profile.
PPMd, BCJ2, encryption, legacy solid RAR and split volumes are rejection fixtures.

Python's standard `tarfile`, `gzip`, `bz2` and `lzma` modules produce deterministic
tar fixtures and three wrappers; the `zstd` CLI produces the fourth wrapper.
`sample.tar` contains JSON, text, binary, empty entries, a directory and Unicode
names, with fixed timestamp 1700000000 and modes 0640/0755. Other fixtures cover
V7/ustar/pax/GNU long names, distinct NFC/NFD spellings, concatenated gzip streams,
explicit mixed-format nesting, empty Stored RAR5 checksum validation, unsafe
overridden names, NULs, conflicts, special
entries, checksum failures and truncation. Temporary downloads/extractions belong
under the repository's `temp/` directory.

Contracts: **S12.4.1v3/S14.3.1v3**. Native coverage is in
`test/test_zip_gtest.cpp`; Lambda input/error/retention/output scripts have matching
`.txt` goldens and join the interpreter/automatic/JIT parity inventory.
