# Duplicate-code checks

`check_code_dup.py` runs Lizard's duplicate-code detector in C/C++ mode, removes
reviewed false positives described in `exclude.json`, clusters overlapping
windows into review families, and checks the maintained Lambda metrics against
`baseline.json`. Run it from the repository root.

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

The default output is a ranked summary. Use `--full` to append every remaining
Lizard location for an audit. Long reports must remain under the repository's
`temp/` directory:

```bash
python3 test/dedup/check_code_dup.py lambda --full > temp/dedup-lambda.txt
```

The summary reports raw and remaining blocks, clone-family counts, union
duplicate lines, top files and cross-file pairs, and reviewed exclusions. Raw
Lizard block count remains diagnostic because one logical family can produce
many overlapping windows.

The checked-in Lambda ratchet fails if either reviewed clone-family count or
union duplicate lines grows beyond `baseline.json`. A reduction passes without
editing the baseline; lower the checked-in values in the same reviewed cleanup
that establishes the new scan result. Selections without a configured baseline
remain report-only. Configuration errors, a missing `lizard` executable, an
abnormal Lizard failure, or ratchet growth return a nonzero status.

## Exclusions

Every exclusion in `exclude.json` must identify its affected modules and give a
non-empty reason. Exclusions are for generated code or reviewed false positives,
not for suppressing ordinary duplication.

File exclusions are passed directly to Lizard with `-x`. Paths and patterns are
relative to the repository root. The current Lambda exclusions cover the
generated `lambda/lambda-embed.h` and every vendored `lambda/tree-sitter*`
tree, including runtimes, grammar sources, generated parsers, scanners, and
language bindings.

```json
{
  "pattern": "lambda/tree-sitter*",
  "modules": ["lambda"],
  "reason": "Vendored Tree-sitter code is outside the maintained Lambda scan."
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

The maintained Lambda scan currently has one reviewed block exclusion:
`property_definitions` in `lambda/input/css/css_properties.cpp`. Its rows form a
static CSS property schema and contain no executable control flow. Similar
struct declarations, parser states, switches, loops, and adapters remain
visible until a separate narrow invariant is reviewed.

When adding an exclusion:

1. Confirm that sharing the implementation would couple independent domains or
   make generated/declarative code less maintainable.
2. Add the narrowest stable file pattern or marker regions and record the reason.
3. Run the module-specific scan and verify the exclusion count in the summary.
4. Treat missing or non-unique marker errors as stale configuration that must be
   repaired alongside the source change.
