# Chinese pinyin input method

`lambda.ime.chinese` provides the candidate engine and session reducer: full pinyin, incomplete
last syllables, orthographic initials, mixed input, apostrophe boundaries and
configurable fuzzy pairs. Importing the package reads no dictionary
(D7.2.1/D7.2.3). Resource acquisition is an explicit procedure; candidate
queries and session transitions are pure functions (S12.1.1v3). The caller
applies composition/candidate actions and explicitly persists learned words.
The M3 Radiant adapter supplies composition transport and a native candidate popup.

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

The optional Traditional profile is prepared separately (IME11):

```sh
python3 utils/prepare_ime_chinese.py fetch --profile zh-Hant
python3 utils/prepare_ime_chinese.py prepare --profile zh-Hant
python3 utils/prepare_ime_chinese.py validate --profile zh-Hant
```

Use `ime.load("zh-Hant")` with the same query/session API. Preparation applies
pinned OpenCC STPhrases matches before STCharacters fallback and retains every
unresolved form: `fa` can offer 發 and 髮, while `toufa` offers 頭髮. Readings
and weights come from the Simplified source; identical converted word/readings
merge by maximum weight. Abbreviations are rebuilt from the result. The engine
performs no script conversion at runtime. This is OpenCC standard Traditional;
Taiwan/Hong Kong regional vocabulary remains deferred (IME-O8).

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

## Session and actions

```lambda
import ime: lambda.ime.chinese

pn main() {
    let engine = ime.load("zh-Hans")^
    var session = ime.session(engine,{page_size:9})^
    for (key in ["b","e","i","j","i","n","g"," "]) {
        let step = ime.key(engine,session,{type:"keydown",key:key})^
        session = step.session
        print(step.actions)
    }
}
```

| Export | Meaning |
|---|---|
| `fn session(engine, settings = null)` | New Chinese-mode session with an empty learned overlay. |
| `fn key(engine, session, evt)` | Pure transition returning `{session, actions}`. |
| `fn configure(engine, session, settings)` | Replace settings, refresh candidates, preserve raw input and chosen words; return `{session, actions}`. |
| `fn settings(session)` | Current canonical settings for the host to save separately. |
| `fn with_user(session, user)` | Validate and attach an overlay of the same profile. Refresh before the next handled key. |
| `pn load_user(path)` / `pn save_user(user, path)` | Explicit JSON persistence. Save validates before an atomic replacement. |

These operations are fallible (`T^`, S11.4.2). Settings accept only `fuzzy`
(the query's pair names, `"none"`, or `"all"`) and `page_size` (integer 1–9,
default 9). Reverse pair names normalize to canonical names; unknown names or
keys are errors (S17.8.1). `configure` replaces the settings map, so omitted
fields take their defaults. A settings change emits candidates without
changing the preedit. It never changes learned entries (IME13).

The event map uses DOM names `type`, `key`, `shiftKey`, `ctrlKey`, `altKey`,
`metaKey`; other fields are ignored. Missing `type` means `keydown`. Keyup
passes through, except a Shift release completing a tap. The reducer handles
these keys:

- Lowercase letters start composition; letters and apostrophes extend it.
  Idle shifted/uppercase Latin letters pass through. Composition accepts at
  most 32 letters, with no repeated or leading separator.
- Space selects the highlighted candidate; `1`–`9` select within the current
  page. Selection consumes the candidate's exact raw prefix. Remaining input
  stays in composition; with no remaining input, the full text commits.
- Arrows move the highlight across page boundaries. `-`/`=`, PageUp/PageDown
  move between pages and clamp at the ends. Invalid selections consume the key
  and leave the composition in place.
- Backspace deletes raw input. Once raw input is empty, another Backspace
  restores the last chosen word's exact original prefix. Escape cancels.
  Enter commits chosen words followed by the raw remainder.
- A Shift tap toggles Chinese/English mode. Switching to English commits any
  existing preedit; English keys pass through. Shift chords and repeats do
  not toggle modes. Ctrl/Alt/Meta combinations pass through with composition
  preserved.
- Punctuation commits the current preedit followed by its mapped form:
  `, . ? ! \\ : ; ( ) [ ] < >` become `，。？！、：；（）【】《》`.
  Double and single quotes alternate `“”` and `‘’`; while composing, an
  apostrophe is a pinyin boundary.

Actions are ordered maps:

| `type` | Fields | Host action |
|---|---|---|
| `compose` | `text`, `caret` | Start/update preedit; caret is a code-point offset (S2.5.8/S17.4.1). |
| `candidates` | `items`, `page`, `pages`, `highlight` | Show the current page; page/highlight are zero-based, highlight is page-relative. |
| `commit` | `text` | Commit and end preedit. |
| `cancel` | — | End preedit without inserting text. |
| `pass` | — | Continue the host's default key handling. |

Composition emits `compose`, then `candidates`; commit/cancel emits its action
then an empty candidate frame. Navigation and configuration emit only a
candidate frame. The session retains all query items plus the `truncated`
flag; each frame contains only the current page.

## Learned words

Choices stay pending until commit (including Enter, punctuation, or switching
to English). Undoing/canceling them never trains the overlay (IME8). Committed
choices increment a selection counter and count. Overlay matches lead by
recency, then count; base candidates retain M1 ranking. Learned entries can
add Han words absent from the pack and match exact, fuzzy, prefix and initial
paths through the same query kernel. The immutable engine remains reusable.

The on-disk format is independent of pack positions:

```json
{"format":"lambda.ime.chinese.user","format_version":1,"profile":"zh-Hans",
 "next_seq":2,"entries":[["北京","bei'jing",1,1]]}
```

Entries are `[word, reading, count, last_seq]`. Words must contain 1–9 NFC Han
code points, with one valid syllable per character. Duplicate word/reading
pairs, invalid counters and `last_seq >= next_seq` are rejected. Counters
range up to 9007199254740991; exhausted counters return an error. Han
membership uses the checksum-pinned Unicode 18 data already used by preparation;
`python3 utils/generate_ime_han.py` checks its generated, licensed matcher.

`load_user`/`with_user` build a bounded-query index of the entries. `save_user`
writes only the public file fields, with no index or settings. The caller
chooses the path and creates its directory; there are no implicit writes or
home-directory preferences. Load the saved settings separately and pass them
to `session` or `configure`.

## Radiant

The input method is off by default (IME10). Prepare the local dictionary, then
open the interactive example:

```sh
./lambda.exe view test/demo/ime/chinese.ls
```

The example includes input, textarea and contenteditable surfaces, live fuzzy
settings, window/document switches, and 简体/繁體 profile buttons. Prepare both packs
before selecting 繁體. Use a Latin OS input source to type
with this engine. While an OS composition is active, bundled conversion
passes keys through; an OS start cancels a pending bundled composition first.

For HTML, enable the document with `<body data-ime="chinese">`. A missing
`data-ime` inherits the window setting; `data-ime="off"` explicitly disables
it. A Lambda host changes the window setting with
`radiant.set_chinese_ime(owner, true)` or `dom.set_chinese_ime(owner, true)`;
`owner` is a live DOM element in that window. It survives document navigation
(IME-O2), while the per-document override takes precedence.

Optional body attributes configure the initial session:

| Attribute | Values |
|---|---|
| `data-ime-fuzzy` | `none`, `all`, or comma-separated supported pair names |
| `data-ime-page-size` | Integer 1–9; default 9 |
| `data-ime-profile` | `zh-Hans` (default) or `zh-Hant` |
| `data-ime-directory` | Explicit prepared-pack directory; omitted uses the selected installed profile |

The adapter imports the engine directly (IME-O1) and loads only on first enable
for an eligible focused surface. It keeps the engine and session in the owning
document runtime (D5.4.1), then reuses them across that document's controls.
Changing profile/directory loads the requested engine and starts a fresh session;
any previous provisional range is canceled first. Existing committed document
text is preserved. Save a learned overlay explicitly before switching if it
should be kept; overlays are profile-specific and never converted implicitly.
Readonly/disabled controls pass through. Ordinary pointer/focus changes cancel
preedit; a toolbar may prevent its mousedown default to keep composition open.

`lambda.dom.ime` exposes host operations:

| Export | Effect |
|---|---|
| `pn enable(body, enabled)` | Set the document override and update the visible input method. |
| `pn configure(body, settings)` | Validate/replace settings and refresh candidates immediately, preserving preedit. |
| `fn session(body)` | Read the current engine session, or null before first enable. |
| `pn with_user(body, user)` | Validate and replace its learned overlay. |

These host procedures propagate errors (`^`). Call `configure` for immediate
changes; direct attribute changes take effect at the next eligible key/focus
turn. Save `session(body).user` with the engine's explicit `save_user`, and
save `ime.settings(session(body))` separately. No preference files are written
by the adapter.

The popup is native UI: numbered rows, a highlighted candidate and a page
indicator. A row click selects it without moving editing focus. Clicking its
Chinese/English header toggles mode. Author CSS and DOM queries never see the
popup (IME-E2); accessibility integration remains future work.

Native `dom.compose(target, phase, text, caret)` queues `begin`, `update`,
`commit`, or `cancel` through the existing composition controller. It delivers
after the requesting handler has unwound, including from host-loop callbacks
(IME-E1). Pending requests retain copied text and validate their target before
delivery. The same DOM composition/beforeinput/input pipeline supplies
preedit, maxlength and history; a completed composition is one undo entry.
The caret remains a code-point offset (S2.5.8/S17.4.1). Input policy stays in
the Lambda package (D7.2.5); native code retains its opaque session through
precise roots (D5.3.3).
