# Duplicate-code checks

`check_code_dup.py` runs Lizard's duplicate-code detector in C/C++ mode, removes
reviewed false positives described in `exclude.json`, and prints the remaining
duplicate blocks. Run it from the repository root.

## Prerequisites

The script requires Python 3 and `lizard` on `PATH`:

```bash
python3 -m pip install lizard
```

## Usage

Use the Make targets for the common scans:

```bash
make check-code-dup       # scan lib, lambda, and radiant
make check-lambda-dup     # scan lambda only
make check-radiant-dup    # scan radiant only
```

The script also accepts one or more module names. With no module argument it
scans all three modules.

```bash
python3 test/dedup/check_code_dup.py
python3 test/dedup/check_code_dup.py radiant
python3 test/dedup/check_code_dup.py lib lambda
```

Long reports may be kept under the repository's `temp/` directory:

```bash
python3 test/dedup/check_code_dup.py radiant > temp/dedup-radiant.txt
```

The summary reports remaining blocks, excluded known false positives, and the
number of file exclusion rules passed to Lizard. Remaining duplicate blocks do
not currently make the command fail: this is a measurement and review tool, not
a threshold gate. Configuration errors, a missing `lizard` executable, or an
abnormal Lizard failure return a nonzero status.

## Exclusions

Every exclusion in `exclude.json` must identify its affected modules and give a
non-empty reason. Exclusions are for generated code or reviewed false positives,
not for suppressing ordinary duplication.

File exclusions are passed directly to Lizard with `-x`. Paths and patterns are
relative to the repository root. The current Lambda exclusions cover
`lambda/lambda-embed.h` and generated `parser.c` files.

```json
{
  "pattern": "lambda/*/parser.c",
  "modules": ["lambda"],
  "reason": "Tree-sitter parser tables are generated code."
}
```

Block exclusions use source markers instead of fragile line numbers. Each
marker must uniquely identify its source location, and the entire Lizard block
must fit within the declared regions.

```json
{
  "id": "independent_name_switches",
  "modules": ["radiant"],
  "reason": "The switches map unrelated enum domains that Lizard normalizes to the same shape.",
  "regions": [
    {
      "file": "radiant/first.cpp",
      "start": "const char* first_name(",
      "end": "static void next_first_function("
    },
    {
      "file": "radiant/second.cpp",
      "start": "const char* second_name(",
      "end": "static void next_second_function("
    }
  ]
}
```

By default, a matching duplicate must span at least two declared regions. The
optional `"allow_within_region": true` is reserved for a declarative table whose
repeated row shape is clearer than a macro or runtime builder.

When adding an exclusion:

1. Confirm that sharing the implementation would couple independent domains or
   make generated/declarative code less maintainable.
2. Add the narrowest stable file pattern or marker regions and record the reason.
3. Run the module-specific scan and verify the exclusion count in the summary.
4. Treat missing or non-unique marker errors as stale configuration that must be
   repaired alongside the source change.
