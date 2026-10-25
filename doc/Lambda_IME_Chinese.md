# Chinese pinyin candidates

`lambda.ime.chinese` provides the M1 candidate engine: full pinyin, incomplete
last syllables, orthographic initials, mixed input, apostrophe boundaries and
configurable fuzzy pairs. Importing the package reads no dictionary
(D7.2.1/D7.2.3). Resource acquisition is an explicit procedure; candidate
queries are pure functions (S12.1.1v3).

Session reduction and Radiant integration are the following M2/M3 milestones.
This package currently returns candidates to its caller.

## Prepare the local dictionary

Run once from the repository root:

```sh
python3 utils/prepare_ime_chinese.py fetch
python3 utils/prepare_ime_chinese.py prepare --compare
python3 utils/prepare_ime_chinese.py validate
```

Python is preparation tooling. Loading and querying the prepared pack use
Lambda only. Prepared dictionaries remain local and gitignored per IME-O7.
The loader currently reads the prepared uncompressed tables; compressed-only
installation is not supported yet.

## Query

```lambda
import ime: lambda.ime.chinese

pn main() {
    let engine = ime.load("zh-Hans")^
    let found = ime.candidates(engine,"beijingdaxue")^
    print(found.items[0].text)
}
```

Run the script with `./lambda.exe run <file.ls>`. The caller loads the engine
once and reuses it. Normal loading checks the format, profile and table sizes,
then retains bytes plus `u32[]` line indexes; full dictionary validation stays
in the explicit preparation command.

| Export | Meaning |
|---|---|
| `pn load(profile = "zh-Hans", options = null)` | Load an installed pack. `options.directory` supplies an explicit pack directory. Other option names are errors. |
| `fn from_bytes(manifest, lexicon, abbrev, syllable_text, profile = "zh-Hans")` | Construct an engine from prepared bytes for another host or a deterministic fixture. Same cheap loading checks. |
| `fn candidates(engine, input, options = null)` | Return `{segments, items, truncated, scanned}`. |
| `fuzzy_pairs` | Supported pair records with `name`, `kind`, `left`, `right` and `example`. |

Each candidate has `text`, `reading`, `consumed`, `coverage`, `match` and
`weight`. `consumed` is the raw input code-point offset through the selected
prefix, including an immediately following apostrophe; `coverage` counts the
syllables. For `beijingdaxue`, 北京 consumes 7 input characters and covers 2
syllables. These positions follow S2.5.8/S17.4.1. `match` is `exact`, `fuzzy`,
`incomplete` or `abbreviation`.

`segments` exposes the lattice edges (`start`, `end`, `typed`, `value`, `kind`,
`fuzzy`); candidate routes also require a viable suffix. `scanned` reports the
number of table keys and postings inspected. `truncated` is true whenever a
path, scan, posting or candidate limit discards work. An empty candidate list
with `truncated: false` is a complete miss for that pack and input.

Input accepts lowercase ASCII letters, `ü`/`v` and apostrophes. It is bounded
at 32 letters; leading and repeated apostrophes are errors. A trailing
apostrophe is permitted. `xian` admits both `xian` and `xi'an`; `xi'an` forces
the boundary. `bjdx` and `beijdx` can find 北京大学. Orthographic initials keep
`zh`, `ch`, `sh`: 中国 is `zhg`; `zg` also admits it with `z=zh` enabled.

## Query options

Option names and values are checked (S17.8.1). Unknown names report the
supported ones. Fuzzy pairs are off by default, symmetric, and apply to whole
initials/finals. Reverse names such as `zh=z` are accepted. Expansions outside
the syllable inventory are discarded.

```lambda no-run
// no-run: engine is supplied by the caller's explicit load.
ime.candidates(engine,"zongguo",{fuzzy:["z=zh"]})^
ime.candidates(engine,"beij",{fuzzy:"all",max_candidates:9})^
```

| Option | Default | Allowed values |
|---|---:|---|
| `fuzzy` | `"none"` | `"none"`, `"all"`, or an array of supported pair names |
| `max_paths` | 512 | Integer 1–4096 |
| `max_keys` | 256 | Integer 1–32768 |
| `max_postings` | 4096 | Integer 1–65536 |
| `max_candidates` | 128 | Integer 1–4096 |
| `coverage_floor` | 2703 | Integer 0–65535; 0 disables rare-word demotion |
| `group_leaders` | 1 | Integer 1–128 |

Ranking favors coverage, match quality, frequency and stable pack order.
Words below `coverage_floor` follow the first `group_leaders` entries of the
next available shorter group, skipping empty coverage groups. Text reached by
multiple routes appears once at its best position. The defaults come from
the frozen M1 evaluation baseline, recorded in the
[implementation evidence](../vibe/impl/Lambda_Impl_IME_Chinese.md); they are
not a general candidate-quality guarantee.

`test/ime/baseline.json` freezes the initial corpus coverage/rank floors;
the full-pack evaluation runner fails if any of them regress or the fixture
identity changes.

The posting budget also bounds candidates retained before the final ranking;
each line decodes at most `max_candidates` matching postings. Consequently
budgets cap both work and temporary candidate storage, while dictionary
postings remain lazy.
