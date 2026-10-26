# Lambda Chinese IME Package — `lambda.ime.chinese` (Proposal)

> **Status:** proposal, drafted 2026-10-25; revised the same day with the
> user's ruling that fuzzy pinyin is configurable (IME13, §5.7), and with the
> dictionary pack format specified in full as version 1 and adopted by the user
> (§6). M0's offline preparation and data validation are implemented and
> qualified as of 2026-10-25. M1's engine and its frozen evaluation baseline are
> implemented. M2's pure session reducer and explicit overlay persistence are
> implemented as of 2026-10-26; qualification is recorded in the implementation
> record. M3 Radiant transport, popup and body adapter are implemented and
> focused-qualified as of 2026-10-26; the aggregate gates retain two failures
> reproduced on the preserved pre-M3 host. M4 Traditional conversion, the
> `zh-Hant` pack and profile selection are implemented and qualified by conversion
> fixtures, reproducibility and the frozen quality report; the same two aggregate
> failures remain. M0 retained-heap attribution and the
> provisional performance targets remain open. IME-O1, IME-O2 and
> IME-O4 are resolved and IME-O7 is deferred (data stays uncommitted).
> Evidence and remaining gates: [implementation record](impl/Lambda_Impl_IME_Chinese.md).
> **Package:** `lambda.ime.chinese`.
> **Scope:** a shipped, data-driven pinyin input method that converts typed
> pinyin into Chinese **word by word**: the user picks a word for the leading
> syllables, the rest stays in composition, and the text commits when every
> syllable is consumed. It runs as a pure Lambda library anywhere Lambda runs,
> and as an in-engine input method for Radiant's editable surfaces through the
> existing composition pipeline. Simplified Chinese first, Traditional as a
> converted profile. **Whole-sentence conversion is out of scope** and left to
> a later proposal (§11.3).
> **Spec linkage:** D7.2.4v4 and D7.2.6 (the `lambda.ime.chinese` path and its
> resolution); D7.2.1 and D7.2.3 (immutable package bindings, instantiated
> once: the dictionary is loaded explicitly, never at import, IME2); S12.1.1v3
> (the pure `fn` engine and the `pn` shell, IME3); S12.1.3 (`on` handlers in
> the DOM behavior package); D7.2.5 (input policy belongs to the shipped Lambda
> DOM behavior package; Radiant owns composition transport, IME9); D7.5.3v2
> (Lambda reaches Radiant only through the `radiant-dom` module, IME-E1);
> D7.1.2v2 (resource IO through `lambda-io`); S2.5.8 and S17.4.1 (code-point
> positions for the preedit caret); S11.4.2 (`T^` results); S17.8.1 (option
> names checked at the call). No formal ruling changes.
> **Authority:** package design. Decisions IME1–IME13, native enablers
> IME-E1–IME-E3, open items IME-O1–IME-O10. The `IME` series is new; it is
> the home series for input-method packages.
> **Related:** [Lambda_Pkg_Spelling.md](Lambda_Pkg_Spelling.md) (pack
> provenance and the prepare-then-index loading discipline this package
> follows), [Radiant_Design_Form_Input.md §3.7](radiant/Radiant_Design_Form_Input.md)
> (the OS IME bridges), [`lmd/package/dom/ime.ls`](../lmd/package/dom/ime.ls)
> (the document's composition session policy),
> [Lambda_Package.md](Lambda_Package.md) (package model).

---

## 1. Purpose and goals

Radiant accepts Chinese text today only from an operating-system input
method: `NSTextInputClient` on macOS (`radiant/ime_mac.mm`) and IMM32 on
Windows (`radiant/ime_win.cpp`). There is no Linux bridge, headless runs have
no input method at all (tests drive composition through `event_sim`'s
`ime_compose` event), and a Lambda application cannot offer Chinese input on a
machine whose user has not installed one. A bundled input method closes those
gaps and gives one behavior on every platform, deterministic tests, and
candidate data that scripts can use directly (pinyin search, initials
filtering).

**Goals.**

1. **Pinyin to word candidates.** Full pinyin (`beijing`), an incomplete last
   syllable (`beij`), initials abbreviations (`bj`, `bjdx`), mixtures
   (`beijdx`), the `'` syllable separator (`xi'an`), `v` for `ü` (`lv`, `nve`),
   and fuzzy pinyin (`z`=`zh`, `n`=`l`, `an`=`ang`, …), configurable pair by
   pair (IME13).
2. **Word-by-word selection with partial consumption.** Choosing a candidate
   consumes the syllables it covers; the remainder stays in composition and is
   converted next. The composition commits when nothing is left.
3. **Good ranking without a sentence model:** word frequency, match quality,
   coverage, and the user's own choices (§5.3).
4. **Usable everywhere.** The engine is pure Lambda and works in `lambda-cli`
   and `lambda-headless` builds. In Radiant it drives the same composition
   events as an OS input method, so `<input>`, `<textarea>`, `contenteditable`
   and JavaScript pages need no changes.
5. **Clean distribution.** Only permissively licensed data is bundled with the
   MIT-licensed repository; every pack is reproducible from pinned sources and
   carries its manifest and notices (§3.3).

**Non-goals (first release).** Whole-sentence conversion and any n-gram
language model (§11.3); cloud candidates; Zhuyin/Bopomofo, double pinyin
(shuangpin), Wubi, Cangjie and other schemes (later milestones, which the
protocol of IME3 accommodates); handwriting and voice; hanzi-to-pinyin
annotation (the data could serve it later); emoji and symbol pickers beyond
full-width punctuation; replacing an OS input method the user already runs
(IME10); Japanese and Korean.

---

## 2. Prior art

| System | Model | Adopt | Avoid |
|---|---|---|---|
| **Rime / librime** | Schema-driven engine; plain-text dictionaries (`word<TAB>pinyin<TAB>weight`); syllable lattice; user dictionary learned from selections | Text dictionaries compiled into a lookup structure; abbreviation and incomplete-syllable matching; the user dictionary as a separate overlay | Its schema/YAML configuration language; LGPL/GPL data (IME5) |
| **Google Pinyin (AOSP PinyinIME)** | Compact lexicon with word, frequency and pinyin; spelling trie; word-by-word and sentence modes; abandoned since 2014 | One file carrying word, reading and frequency; partial consumption of the input by a chosen word | Its binary dictionary builder and C++ engine as dependencies |
| **libpinyin / ibus-libpinyin** | Phrase index plus an interpolated bigram model; full sentence conversion | Fuzzy-pinyin option set; per-reading phrase entries | GPL data; sentence conversion (out of scope) |
| **fcitx5 / libime** | Lexicon plus a KenLM model; Wikipedia-derived word lists | Separate base and user dictionaries | CC BY-SA word lists in a bundled pack |
| **sunpinyin** | Trigram model over a lexicon | — (sentence conversion) | — |
| **Commercial IMEs (Microsoft, Sogou)** | Behavioral reference only | Key conventions: Space takes the first candidate, digits 1–9 select, `-`/`=` page, Enter commits raw letters, Esc cancels, Shift toggles Chinese/English, full-width punctuation in Chinese mode | Cloud candidates, telemetry |

The common shape, and the one this proposal takes: **segment the input into a
syllable lattice, look the syllables up in a reading-keyed lexicon, rank by
frequency and match quality, let a selection consume a prefix, and learn from
the user in a separate overlay.** The sentence engines add a language model on
the same lattice; §11.3 records that as the extension point.

---

## 3. Data

### 3.1 Sources

Licenses, status, counts and formats were checked against the upstream
repositories on 2026-10-25; counts are entries or lines at that date. Rows
marked *not re-checked* are from earlier knowledge and only matter as
exclusions.

| Source | Supplies | License | Status | Use |
|---|---|---|---|---|
| **Unicode Unihan 18.0.0** (`Unihan_Readings.txt` in `Unihan.zip`) | `U+XXXX<TAB>property<TAB>value` lines. Readings: `kMandarin` (44,355 characters; its first value is the zh-Hans reading), `kHanyuPinyin` (34,130), `kXHC1983` (11,072); `kTGHZ2013` (8,105, the 通用规范汉字表); `kHanyuPinlu`, per-reading frequencies for 3,799 characters from 现代汉语频率词典 (`zhǎng(1879) cháng(1179)`) | Unicode-3.0 | Current (file dated 2026-07-31) | Reading cross-check, default character set, ranking cross-check |
| **mozillazg/pinyin-data** (`pinyin.txt`) | `U+4E2D: zhōng,zhòng  # 中`; 44,440 characters, 8,633 with several readings. The first reading is the curated *most customary* one, not a measured frequency | MIT | Active; built from Unihan 18.0.0 | Character readings. The repository's `cc_cedict.txt` and `zdic.txt` are not merged into `pinyin.txt` and are never taken |
| **mozillazg/phrase-pinyin-data** (`pinyin.txt`) | `银行: yín háng`; 47,114 phrases, one toned syllable per character, a line per reading; resolves polyphones (多音字) in context | MIT, with provenance flags (§3.3) | Active | Word readings. **Never `large_pinyin.txt`**: it merges CC-CEDICT (CC BY-SA) and zdic.net scrapes |
| **AOSP PinyinIME** (`jni/data/rawdict_utf16_65105_freq.txt`) | UTF-16LE with BOM; `word freq gbk_flag pinyin…`, toneless, `v` for `ü` (`银行 21172.4586769 0 yin hang`); 65,105 lines: 17,037 single characters with **per-reading** frequencies (`行` xing 35,822, hang 3,209) and 48,068 multi-character words | Apache-2.0 (repository `NOTICE`) | Abandoned: last commit 2014-05-14, dropped from the AOSP manifest at 5.0 | Base words; primary character ranking |
| **jieba** (`jieba/dict.txt`) | `中国 129470 ns`; 349,046 words with counts and part of speech; no readings | MIT | Dormant since 2020-02-15 | Word frequency |
| **HanLP 1.x** (`data/dictionary/pinyin/pinyin.txt`, `CoreNatureDictionary.txt`) | `银行=yin2,hang2`, 30,728 phrase readings; `银行<TAB>nis<TAB>6279`, 153,087 words with frequencies from People's Daily 2014 | Apache-2.0; its README asks product documentation to carry the HanLP link and license | Last commit 2024-12-28 | Alternative readings and frequencies (IME-O4) |
| **THUOCL** | `word<TAB>DF`, 11 domain lists (IT, finance, idioms, place names, people, poetry, medicine, food, law, cars, animals), about 157k words with document frequencies; no readings | MIT | Dormant since 2018 | Later: domain overlays |
| **OpenCC** (`data/dictionary/`) | `发<TAB>發 髮`, default first; `STCharacters.txt` 4,012 lines, `STPhrases.txt` 49,238, plus Traditional→Simplified and regional tables | Apache-2.0 | Active (1.4.2, 2026-08-22) | `zh-Hant` (M4) |
| **McBopomofo** | Zhuyin readings (145,665, derived from libtabe, BSD) and phrase counts, Traditional | MIT | — | Later: Zhuyin and `zh-Hant` |
| Rime `rime-luna-pinyin` + `rime-essay` | Readings; `essay.txt` frequency list (442,688 words, Traditional) | LGPL-3.0 | Active | Excluded (IME5) |
| `rime-ice` | 543,118-entry current lexicon | GPL-3.0-only | Active | Excluded (IME5) |
| CC-CEDICT | 125,319 entries with readings and English; no frequency | CC-BY-SA-4.0 | Active | Excluded (IME5) |
| sunpinyin `open-gram` | Lexicon mixing CC-CEDICT and AOSP entries | No top-level license | — | Excluded |
| libpinyin data; `fcitx5-pinyin-zhwiki` | Lexicon and bigram model; Wikipedia-title words | GPL; CC BY-SA (*not re-checked*) | — | Excluded (IME5) |

No permissive source carries word, reading and frequency in one file except
the AOSP lexicon; every other permissive combination joins a reading source
with a frequency source.

### 3.2 The bundled `zh-Hans` pack

Three tables are prepared, all keyed by toneless pinyin. Toned readings are
reduced to toneless syllables and `ü` to `v`; tone-sandhi spellings in the
sources (`一不小心: yí bù …`) therefore need no special handling.

1. **Characters.** Readings are the union of the AOSP single-character entries
   and `pinyin-data`, validated against the syllable table and Unihan.
   Per-reading weights come from the AOSP frequencies (17,037 characters), so
   `行` ranks `xing` above `hang`, cross-checked against `kHanyuPinlu` where
   both exist. A reading with no frequency evidence gets weight 0 and ranks
   after every weighted one (§6.1). The default character set is the 8,105
   characters carrying `kTGHZ2013`; rarer characters go in an optional larger
   pack (IME-O10).
2. **Words.** The base is the AOSP lexicon's 48,068 multi-character words.
   Coverage extends with `phrase-pinyin-data`'s `pinyin.txt` (or HanLP's)
   readings, weighted by jieba (or HanLP) frequencies. Frequencies are
   normalized to one scale at preparation, with the method recorded in the
   manifest. **Admission rule:** a word is admitted only when a source records
   its reading, or when every character has exactly one reading in the
   character table, so deriving the reading involves no choice. A word that
   would need a choice among readings is dropped: a guessed reading for a
   polyphone is a wrong candidate, not a missing one. The same rule decides
   which THUOCL words a later domain overlay can admit.
3. **Abbreviations.** For words of two or more syllables, an index keyed by
   each syllable's first letter (`b'j` for 北京, `z'g` for 中国) that keeps the
   top K words per sequence of initials, each with its full reading, so mixed
   input (`beijdx`) can filter by the syllables typed in full (§6.1).

The AOSP file's GBK-mark flag (9,785 lines set) and its own builder's
cut-off (multi-character words under frequency 60) are reviewed in M0, not
copied blindly. Which extension source makes the default pack is IME-O4,
decided from M0's coverage measurements.

Data age is the permissive sources' known weakness: the AOSP lexicon was
frozen between 2009 and 2014, jieba's dictionary by 2020, HanLP's
frequencies come from a 2014 corpus, and THUOCL from 2016–2017. Recent words
are missing until a domain overlay or the user overlay supplies them (§11.1).

### 3.3 Provenance and notices

The pack follows [Lambda_Pkg_Spelling.md §3.2](Lambda_Pkg_Spelling.md): a
manifest records each source's URL, pinned version or commit, downloaded and
generated SHA-256 hashes, the preparation tool's version and options, entry
counts, the weight-normalization method, and the original license texts. A
format change never changes a source's license. The notices that travel with
the pack:

- **Unicode-3.0:** the Unicode copyright and permission notice.
- **Apache-2.0** (AOSP; OpenCC and HanLP when used): the license text; AOSP's
  `NOTICE` (*Copyright (c) 2009, The Android Open Source Project*); and a
  statement that the prepared pack modifies the original files (§4(b)).
  HanLP also asks product documentation to carry its link and license.
- **MIT:** the copyright notices of mozillazg (2016, 2017), Sun Junyi
  (jieba, 2013) and THUNLP (THUOCL, 2018) for whichever sources are used.
  THUOCL additionally asks papers that use it to cite it.

**Provenance flags.** The stated licenses are permissive, but several
upstream origins are undocumented:

- `phrase-pinyin-data`'s `pinyin.txt` began from the hotoo/pinyin and
  python-pinyin phrase dictionaries (both MIT), whose own origins are not
  stated; a 2025 correction batch (issue #53) came from scraping an unnamed
  online dictionary.
- How the AOSP frequencies were derived is not documented.
- jieba documents no source for `dict.txt`; the author's provenance remarks
  concern its HMM tables, trained on the 1998 People's Daily and MSR corpora.
- THUOCL says its lists come partly from IME lexicons.

The manifest records these flags. Whether an undocumented upstream affects a
stated license is a legal judgment, not an engineering one; it is part of
IME-O4 and is not decided here.

---

## 4. Design decisions

**IME1 — An engine package and a host adapter.** `lambda.ime.chinese` is the
engine: segmentation, lookup, ranking, and the session state machine, with no
knowledge of Radiant. The Radiant adapter is a small layer in the DOM behavior
package that routes keys to the engine and applies its actions (§8). A
terminal UI or a test harness is another host of the same engine.

**IME2 — The dictionary is loaded explicitly.** `pn load(profile)` reads a
pack and returns an immutable engine value. Importing the package loads no
data: package bindings are immutable (D7.2.1, D7.2.3), and a multi-megabyte
read is an effect that belongs to the caller's `pn`. A host loads once per
owning document runtime and reuses the value across its controls; GC values
do not cross runtime heaps (D5.4.1).

**IME3 — The session is a value; the key reducer is pure.**
`fn key(engine, session, evt)` returns `{session, actions}`. It is a `fn`
(S12.1.1v3): deterministic, schedulable anywhere, and testable from goldens.
Hosts apply the actions (`compose`, `commit`, `cancel`, `candidates`,
`pass`). The reducer reads only `type`, `key`, `shiftKey`, `ctrlKey`,
`altKey` and `metaKey`, the DOM `KeyboardEvent` names, so the Radiant adapter
passes its event through and tests write literal maps. Other input schemes
later implement the same reducer contract.

**IME4 — Word by word, with partial consumption.** A selection consumes the
syllables its word covers. The preedit shows the converted words followed by
the remaining pinyin (`北京da'xue`); the composition commits when no pinyin is
left. There is no sentence candidate in the first release; the candidate list
reserves a `sentence` kind for §11.3.

**IME5 — Only permissive data is bundled.** The repository is MIT-licensed.
GPL and LGPL dictionaries (Rime, rime-ice, libpinyin) and CC BY-SA word lists
(CC-CEDICT, Wikipedia-derived lists) are not bundled, even where their quality
is higher. Whether users may build a pack from such sources themselves is
IME-O5.

**IME6 — Prepared packs, indexed at load.** An offline preparation tool
(`utils/prepare_ime_chinese.py`) validates and writes the pack; at run time the
engine reads bytes, builds a line-offset index, and binary-searches keys,
the discipline [Lambda_Pkg_Spelling.md §4.3.4](Lambda_Pkg_Spelling.md) adopted.
Validation happens at preparation, not at every load. Python is development
tooling only; loading and conversion never require it. The pack format is
§6's version 1, adopted 2026-10-25 (USER).

**IME7v2 — Match evidence precedes frequency** (§5.3; USER, 2026-10-26).
After explicit learned selections, complete exact readings precede complete
fuzzy readings, which precede partial/prefix/abbreviation routes. Frequency
and rare-word demotion operate within those groups; they cannot demote a
complete fuzzy reading behind an abbreviation. Qualify the deterministic rule
against the frozen evaluation sets before any quality claim (§9).

**IME8 — Learning is an overlay value.** The session carries the user's
selections as an immutable overlay that boosts and adds entries. The engine
value never changes. Persisting the overlay is the caller's explicit `pn`;
the package performs no hidden writes to a home directory (as Spelling §5.2).

**IME9 — Radiant integration through composition transport.** D7.2.5 puts
input policy in the Lambda DOM behavior package and gives Radiant the
composition transport. The adapter therefore turns engine actions into the
same composition events the OS bridges produce, so `compositionstart`,
`compositionupdate`, `compositionend`, `beforeinput` with
`insertCompositionText` / `insertFromComposition`, preedit painting, undo
grouping and `maxlength` all behave as they do for an OS input method. The
candidate list is a native popup, not a DOM subtree (§8.3).

**IME10 — Opt-in, and it stands down for the OS.** The in-engine input method
is off unless a document or application turns it on (IME-O2). While an OS
composition is active it passes every key through, so two input methods never
convert the same keystrokes.

**IME11 — Simplified first; Traditional as a converted profile.** `zh-Hans`
is the default and the only first-release pack. `zh-Hant` is prepared from it
with OpenCC phrase-then-character conversion at preparation time (M4), so the
engine always reads one lexicon.

**IME12 — Positions are code points.** The raw input is ASCII; the preedit
caret reported to Radiant is a code-point offset into the preedit text
(S2.5.8, S17.4.1), which is what `CompositionEvent.preedit_caret` carries
(`radiant/event.hpp:240`).

**IME13 — Fuzzy pinyin is configurable** (USER, 2026-10-25). Each supported
pair is a separate setting, off by default. Settings are part of the session
value, validated when given, changeable while the input method runs, and saved
by the host with its other settings, never by the package (§5.7).

---

## 5. Engine

```text
upstream data + notices
  -> utils/prepare_ime_chinese.py (pinned, validated, reproducible)
  -> pack: characters, words, abbreviations, manifest, NOTICE

pn load(profile)                      -> engine (immutable)
fn key(engine, session, evt)          -> {session, actions}
     raw input -> syllable lattice -> lookup -> ranked candidates
host applies actions                  (Radiant adapter §8, tests, terminal)
```

### 5.1 Syllable segmentation

Toneless Mandarin pinyin has about 410 syllables; the table ships with the
package and every pack reading is validated against it at preparation.

- The input is split into a **lattice** of every valid segmentation, not one
  greedy split: `xian` is `xian` (先) and `xi'an` (西安); `fangan` is
  `fang'an` (方案) and `fan'gan` (反感). Candidates from all paths are merged
  and ranked together; typing `'` forces a boundary.
- **Incomplete last syllable.** A trailing fragment that begins a syllable
  matches every syllable it prefixes: `beij` → `bei` + `j…`.
- **Initials.** Any syllable may be typed as its initial (`b`, `zh`, …), or as
  its first letter for zero-initial syllables (`a`, `o`, `e`): `bjdx` → four
  initials.
- **`ü`.** `v` stands for `ü` (`lv`, `nve`); `ju`, `qu`, `xu`, `yu` keep their
  standard spellings.
- **Fuzzy pinyin** follows the session's settings (IME13, §5.7): each enabled
  pair adds lattice edges, and a match through one ranks in the fuzzy tier
  (§5.3).
- Input longer than a bound (initially 32 letters) stops accepting keys
  rather than growing the lattice without limit.

### 5.2 Lookup

- **Full keys.** Each lattice path's leading `n` syllables (`n` from the path
  length down to 1) form a key such as `bei'jing`; one binary search returns
  that key's candidates, already ranked by weight in the pack.
- **Prefix ranges.** An incomplete last syllable becomes a key prefix
  (`bei'j`); the matching keys are contiguous in sorted order. Keys are
  grouped by syllable count so a prefix range never contains longer words.
- **Abbreviations.** A path containing initials looks up the abbreviation
  index (`4:b'j'd'x`) and keeps the words whose readings agree with every
  syllable typed in full and every initial typed (§6.4).
- Each query is bounded: a cap on keys scanned and candidates kept, with a
  `truncated` flag rather than silent loss.

### 5.3 Ranking

Candidates are ordered by these keys in turn (IME7v2):

1. **User overlay**: words the user chose, by recency and count (IME8).
2. **Match evidence**: a complete exact reading, then a complete fuzzy
   reading, then every partial, incomplete or abbreviated route. A complete
   reading consumes the entire raw input and specifies every syllable in full;
   consuming all letters as initials alone does not qualify. Use the actual
   input length, even when the path search is truncated.
3. **Coverage within each evidence group**: more syllables first. Below the
   weight floor, a longer word follows the first `group_leaders` entries of
   the next available shorter group. This demotion never crosses an evidence
   boundary.
4. **Match quality within a coverage group**: exact > fuzzy > incomplete last
   syllable > abbreviation.
5. **Weight** from the pack, then **stable pack order**.

Thus `sifei` with fuzzy s/sh enabled ranks a complete `shi'fei` (是非) before
`shi'fou` (是否), which matches only `sif` and leaves `ei`. A low dictionary
weight cannot reverse that preference. An exact `si'fei` (私费) still leads the
fuzzy spelling. For `beijingdaxue`, both complete exact homophones precede a
partial 北京; the common 北京大学 leads the rare 北境大雪 by weight.

The same text reached by multiple paths appears once, at its best position;
deduplication happens after evidence grouping. The original frozen weight
floor and group limits remain unchanged. No sentence-context model or
word-specific ranking overrides are introduced.

### 5.4 Session state machine

| State | Key | Effect |
|---|---|---|
| idle | letter `a`–`z` | start composing; `compose` + `candidates` |
| idle | Shift + letter | `pass`: an uppercase Latin letter |
| idle | mapped punctuation | `commit` its full-width form (§5.5) |
| idle | anything else | `pass` |
| composing | letter, `'` | insert at the pinyin caret; `compose` + `candidates` |
| composing | Space | take the highlighted candidate |
| composing | `1`–`9` | take that candidate on the current page |
| composing | `-` / `=`, PageUp / PageDown | previous / next page |
| compact | Left / Right | previous pinyin syllable boundary / next letter (USER, 2026-10-26) |
| compact | Down | open the expanded candidate grid |
| expanded | Left / Right / Up / Down | previous / next candidate or previous / next grid row; Up on the first row returns to the strip (USER, 2026-10-26) |
| composing | Backspace / Delete | remove the character before / after the pinyin caret; Backspace with no raw input restores the last selection |
| compact | Enter | commit the preedit as typed (converted words plus raw letters) |
| expanded | Enter / Space | select the highlighted candidate |
| expanded | Esc | return to the compact strip, preserving composition |
| compact | Esc | `cancel` |
| any | modified key (Ctrl, Alt, Meta) | `pass`; while composing, commit nothing and keep the session |

Taking a candidate consumes its syllables. If pinyin remains, the reducer
emits `compose` with the new preedit and fresh `candidates`; otherwise it
emits `commit` with the full text. The session keeps a zero-based caret in the
remaining raw pinyin. A `compose` action offsets it by the converted prefix's
code-point length (S2.5.8/S17.4.1). Compact Left/Right emit only `compose`;
they do not re-query or move the candidate highlight. Left uses the preferred
longest-exact-first viable lattice route, with character stops for invalid tails;
Right advances one code point. The native expanded grid reports its responsive
column count to the reducer. Up from its first logical row returns to the
strip with the same candidate and pinyin caret; Up from lower rows continues
normal row navigation. Repeated Up in the strip is a no-op. Other grid arrows
change only the candidate selection; raw edits close the grid. Candidate consumption subtracts the
consumed raw length from the caret and clamps it to the remaining pinyin.
Backspace at the start of nonempty pinyin and Delete at its end are consumed
without changing text. Deleting a syllable's only letter also removes an empty
separator, preserving the query's no-leading/no-repeated-separator invariant.

### 5.5 Mode and punctuation

Tapping Shift alone (keydown then keyup with no other key) toggles Chinese
and English mode; in English mode every key passes. In Chinese mode
punctuation maps to full-width forms (`,`→`，`, `.`→`。`, `?`→`？`, `\`→`、`,
paired quotes alternate “ ”), committed directly when idle and after the
current composition when composing. The table is small and self-authored.
Switching to English with an active composition commits its existing preedit
before subsequent English keys pass through. Shift chords and key repeats do
not toggle modes. Paired quotes retain their next opening/closing side across
compositions in the session.

### 5.6 Traditional profile

`zh-Hant` (M4) converts every word at preparation with OpenCC's phrase table,
falling back to its character table. The implementation applies the longest
phrase at each source position before character fallback; outputs are not
converted a second time. A word with several Traditional forms
that the phrase table does not resolve (`发` → `發` / `髮`) yields one entry per
form, each keeping the source reading and weight. Identical converted
word/reading pairs merge by maximum source weight, and abbreviations derive
again from the converted lexicon. The manifest records the source lexicon
hash, conversion policy, expansion/merge counts and exact OpenCC pins. The
source character set is converted with phrase-exception glyphs included;
pack format remains version 1. Taiwan and Hong Kong vocabulary variants
use OpenCC's regional tables and are IME-O8.

### 5.7 Fuzzy pinyin settings

Fuzzy pinyin lets a speaker whose accent merges two sounds type either
spelling: with `z=zh` on, `zi` also finds 知 (`zhi`). Which merges a user
needs depends on their dialect, so each pair is a separate setting and all are
off by default.

| Pair | Kind | Example |
|---|---|---|
| `z=zh` | initial | `zong` ↔ `zhong` |
| `c=ch` | initial | `cang` ↔ `chang` |
| `s=sh` | initial | `si` ↔ `shi` |
| `n=l` | initial | `nan` ↔ `lan` |
| `f=h` | initial | `fu` ↔ `hu` |
| `r=l` | initial | `ru` ↔ `lu` |
| `an=ang` | final | `fan` ↔ `fang` |
| `en=eng` | final | `ren` ↔ `reng` |
| `in=ing` | final | `lin` ↔ `ling` |
| `ian=iang` | final | `xian` ↔ `xiang` |
| `uan=uang` | final | `guan` ↔ `guang` |

- **Symmetric.** A pair lets each spelling find the other; `zh=z` names the
  same pair as `z=zh`.
- **Whole parts only.** An initial pair replaces a whole initial and a final
  pair a whole final: `an=ang` does not touch `ian` or `uan`, which have pairs
  of their own.
- **Valid syllables only.** An expansion that leaves the syllable table is
  dropped (`f=h` turns `fo` into no syllable).
- **Pairs compose.** With `z=zh` and `an=ang`, `zan` reaches `zan`, `zhan`,
  `zang` and `zhang`.
- **Abbreviations too.** Initial pairs apply to typed initials, so with
  `z=zh`, `zg` finds 中国.
- **Ranking.** A fuzzy match sits in the fuzzy tier of §5.3, below exact
  matches of the same coverage.

**Settings value.** The session's settings are a map; `fuzzy` takes `"none"`
(the default), `"all"`, or a list of pair names:

```lambda expr
{fuzzy: ["z=zh", "c=ch", "s=sh", "an=ang"], page_size: 7}
```

`page_size` accepts 1–9 and defaults to **7** (USER, 2026-10-26).

The package exports the supported pairs with their kinds and examples
(`fuzzy_pairs`), so a host's settings screen lists them from the package and
never keeps its own copy. An unknown pair name or an unknown settings key is
an error naming what is supported, never silently ignored (the rule S17.8.1
applies to system-function options). Named dialect presets are not offered:
each would be a claim about a group of speakers that the package cannot
check, and a list of pairs says the same thing exactly.

**Changing settings while running.** `fn configure(engine, session, settings)`
returns `{session, actions}` like `key`. During a composition it re-queries
the current input with the new settings and emits fresh `candidates`, so a
settings change shows at once without losing what the user has typed.

**Saving settings.** `fn settings(session)` returns the current settings map
for the host to save wherever it keeps its preferences (IME-O6). Settings are
separate from the learned user overlay (IME8): a settings change never alters
learned words, and clearing learned words never resets settings.

**Cost.** Every enabled pair widens the lattice. The input-length bound
(§5.1) and the per-query caps (§5.2) still apply, and per-key latency is
measured with all pairs on (§9).

---

## 6. Pack format

**Adopted 2026-10-25 (USER): version 1.** This section specifies the format
completely, in the manner of Spelling §4.3. Its sizes are M0 measurements
(§6.7), not claims. A change to the format is a new version and needs a user
ruling: M0's measurements, including the controls of §6.7, can inform such a
ruling but never change the format by themselves.

A pack is the directory `lmd/package/ime/chinese/data/<profile>/`, read from
`sys.lambda.home# ++ "/package/ime/chinese/data/<profile>/"`, the pattern
`lmd/package/doc/math/tex_metrics.ls:12` uses for shipped resources:

| File | Content |
|---|---|
| `lexicon.txt` | Characters (one-syllable keys) and words (two to nine syllables), one key per line |
| `abbrev.txt` | First-letter keys for words of two or more syllables |
| `manifest.json` | Format, profile, counts, sizes, hashes, weight method, sources, generator (§6.5) |
| `NOTICE.txt` | The notices and license texts of §3.3 |

The two tables share one line syntax. Neither has headers, comments or
embedded metadata; everything descriptive lives in the manifest, as in
Spelling §4.3.3.

### 6.1 Line syntax

`lexicon.txt`:

```text
1:hang	行 3507	航 3391	杭 3170	巷 2602
1:xing	行 4554	性 4473	星 4310	型 4205
2:bei'jing	北京 3977	背景 3955	北境 2492
2:yin'hang	银行 4326
```

`abbrev.txt`:

```text
2:b'j	北京 bei'jing 3977	背景 bei'jing 3955	便捷 bian'jie 3610	保健 bao'jian 3540
2:z'g	中国 zhong'guo 4880	资格 zi'ge 3700	战国 zhan'guo 3300
```

The weights in these examples are illustrative; the separators between a key
and its postings, and between postings, are single TABs.

| Element | Rule |
|---|---|
| Line | A key, then one or more postings, each preceded by one TAB; ends with LF |
| Key | `N:` followed by `N` syllables joined by `'` |
| `N` | One ASCII digit, `1`–`9`, equal to the number of syllables; a single digit makes byte order agree with numeric order |
| Lexicon syllable | A toneless syllable from the package's syllable table: lowercase ASCII, `v` for `ü` |
| Abbreviation syllable | One lowercase ASCII letter: the first letter of the syllable it stands for |
| Lexicon posting | `word`, SP, `weight` |
| Abbreviation posting | `word`, SP, `reading`, SP, `weight`, where `reading` is the word's `N` full syllables joined by `'` |
| Word | Han characters only (Unicode Script=Han, which includes 〇), NFC, exactly `N` code points: one character per syllable |
| Weight | Decimal integer `0`–`65535`, no sign, no leading zeros |

**File rules.** UTF-8 without BOM; LF line endings with a final LF; no CR, no
blank lines. Lines sort strictly ascending by key bytes (S6.2.2v3), so keys are
unique within a file. A lexicon line lists each word at most once; an
abbreviation line lists each `(word, reading)` pair at most once. Postings sort
by weight descending, then by word bytes, then (abbreviations) by reading
bytes. TAB, SP, LF, `:` and `'` never occur in a word, because words are Han
only; version 1 therefore has no escape syntax and needs none.

**Meaning.**

- **Weight** orders candidates: larger is more frequent. One log-frequency
  scale is shared by both files of a pack, so a character and a word compare
  directly (§5.3). `0` means no frequency evidence (a reading known only from
  `pinyin-data`, §3.2); such postings sort after every weighted one.
- **A character with several readings appears on several lines**, each with
  that reading's weight: `行` under `1:hang` and `1:xing`.
- **Abbreviation keys use first letters**, so `zh`, `ch` and `sh` key as `z`,
  `c` and `s`. One line then serves `zhg` (filtered by exact initials) and,
  with `z=zh` enabled, `zg` (§5.7), without a key per fuzzy expansion.
- **Abbreviation invariants.** (a) The key's letters are the first letters of
  each posting's reading. (b) Every abbreviation posting `(word, reading,
  weight)` appears in the lexicon on line `N:reading` with the same weight.
  (c) `N` is at least 2; a single initial (`b`) is a lexicon prefix query
  instead (§6.4). (d) For each distinct sequence of *orthographic initials*
  among a line's readings, the line keeps at most the `K` highest-weighted
  postings, where `K` is the manifest's `abbrev_limit` (initially 64). The
  orthographic initial of a syllable is its leading `zh`, `ch` or `sh` if
  present, otherwise its leading consonant letter (including `y` and `w`),
  otherwise empty. Limiting per initials sequence rather than per line keeps
  `zh'g` words from being crowded out by `z'g` words that share the key.

### 6.2 Canonical preparation

`utils/prepare_ime_chinese.py` produces the pack deterministically: the same
pinned inputs and options yield byte-identical files.

1. Read each pinned source and decode it (the AOSP lexicon is UTF-16LE with a
   BOM).
2. Normalize words to NFC. CJK compatibility ideographs change under NFC;
   entries that become identical merge by the weight method.
3. Reduce readings: strip tones, write `ü` as `v`, split one syllable per
   character.
4. Drop, counting each reason for the manifest: readings outside the syllable
   table; readings whose syllable count differs from the word's length
   (merged erhua spellings such as `dianr`); words with a non-Han character or
   a character outside the profile's character set; words of more than nine
   syllables.
5. Apply the admission rule of §3.2.
6. Compute weights by the method recorded in the manifest.
7. Group postings by key, sort lines and postings as §6.1 requires, and write
   `lexicon.txt`.
8. Derive `abbrev.txt` from the lexicon's lines with `N` ≥ 2, never from the
   sources directly, so invariant (b) holds by construction; apply the limit of
   invariant (d).
9. Write `manifest.json` and `NOTICE.txt`; compress each table with XZ preset
   6, as Spelling §4.3.3 adopted for its resources, keeping the uncompressed
   text for inspection.
10. Run the full validation of §6.3 on the output before publishing it.

### 6.3 Validation

**At preparation**, and on demand through the package's `validate`, every
rule is checked: UTF-8, NFC, line endings and the final LF; key syntax; `N`
against the syllable count; syllable-table membership; line order and key
uniqueness; posting order and uniqueness; weight range; Han-only words whose
length equals `N` and whose characters are in the profile's set; abbreviation
invariants (a)–(d); and the manifest's counts, sizes and hashes against the
files.

**At load**, only the manifest's `format`, `format_version` and `profile`,
and each table's byte size against the manifest, which catches a truncated
file cheaply. A malformed pack is a preparation error, not a runtime
condition to repair, following Spelling §4.3.4's loading policy.

### 6.4 Loading and lookup

- **Load** reads each table's bytes (decompressing XZ when shipped
  compressed, through the loader shared with the spelling package; liblzma is
  already linked for `lambda/io/archive.cpp`) and builds a `u32[]`
  line-offset index by scanning LF, as `from_bytes` does in
  `lmd/package/doc/spelling/front_coded.ls:67`. Only the bytes and the offsets
  are retained: no per-entry map or string. Each table must be under 4 GiB.
- **Exact key**: a binary search (`lower_bound`,
  `lmd/package/doc/diff/common.ls:25`) comparing the query key directly
  against each probed line's bytes up to its first TAB, without building a
  string, as Spelling §4.3.4 does.
- **Incomplete last syllable**: the lines from the lower bound of the prefix
  (`2:bei'j`) up to the first line that no longer starts with it, scanned
  within §5.2's cap. The `N` prefix guarantees every line in the range has
  `N` syllables.
- **A single initial** (`b` typed alone): the lexicon prefix `1:b`.
- **Abbreviation**: map each typed initial to its first letter (`zh` → `z`),
  look up `N:letters` exactly, and keep the postings whose syllables typed in
  full equal the reading's and whose typed initials equal the reading's
  orthographic initials or are equivalent under an enabled fuzzy pair (§5.7).
- **Postings are decoded lazily**: only from lines a query returns, and only
  as many as it needs. A line is already sorted, so its first `k` postings are
  its top `k`; candidates from several lines merge by weight.
- Offsets are byte positions inside the file; the text a candidate reports is
  an ordinary string, measured in code points (S2.5.8).

The line-index builder is shared with spelling in
`lmd/package/io/line_index.ls`; neither package keeps its own copy (AGENTS.md
rule 13).

### 6.5 Manifest

`manifest.json` is plain JSON:

| Field | Meaning |
|---|---|
| `format`, `format_version` | `"lambda.ime.chinese.pack"`, `1` |
| `profile`, `script` | `"zh-Hans"`, `"Hans"` (`"Hant"` for `zh-Hant`) |
| `unicode_version` | The Unihan version used, e.g. `"18.0.0"` |
| `char_set` | The character-set rule and its size, e.g. `kTGHZ2013`, 8,105 |
| `syllable_table` | SHA-256 of the syllable table the pack was validated against |
| `files` | Per table: path, bytes and SHA-256 uncompressed and compressed, line count, posting count |
| `counts` | Lines and postings per `N` |
| `weights` | Method and parameters, minimum, maximum, number of weight-0 postings |
| `abbrev_limit` | `K` of invariant (d) |
| `dropped` | Entries dropped at preparation, per reason (§6.2 step 4) and by the admission rule |
| `sources` | Per source: name, URL, pinned version or commit, SPDX license, SHA-256 of the download, provenance flags (§3.3) |
| `generator` | Tool, version and options |

### 6.6 User file

`save_user` writes the learned overlay (IME8) as JSON:

```json
{
  "format": "lambda.ime.chinese.user",
  "format_version": 1,
  "profile": "zh-Hans",
  "next_seq": 1043,
  "entries": [["北京", "bei'jing", 12, 1042], ["反感", "fan'gan", 3, 977]]
}
```

Each entry is `[word, reading, count, last_seq]`. Entries are keyed by
`(word, reading)`, never by line or posting position, so learned words
survive a pack rebuild; an entry whose pair is not in the pack is a word the
user added. Recency is a selection counter (`last_seq`, `next_seq`), not a
clock time, which keeps the reducer a deterministic `fn` (S12.1.1v3). The
settings map of §5.7 is not stored here: the host saves it separately.
M2 keeps selected words pending until composition commits: cancellation and
undo never train the overlay. Loading/attaching validates NFC Han words of
1–9 code points, syllable count/membership, unique word/reading pairs and
integer counters with `last_seq < next_seq`. Counters are bounded by the
compact-int maximum 9007199254740991; exhaustion is an error. Saving validates
first and atomically replaces only the explicitly supplied path, without its
in-memory query index or any settings.

### 6.7 Alternatives and measurements

- **Front-coded keys** (Spelling §4.3's format) are not used. Keys are short
  ASCII next to postings of three-byte Han characters and weights, XZ already
  removes shared-prefix redundancy for distribution, and one line per key
  gives one binary search per lookup over one resource. M0 measures the
  front-coded control to confirm this.
- **Entry IDs instead of readings** in `abbrev.txt` would be smaller, but they
  tie the two tables and the user file to one build. Readings keep each file
  checkable on its own and user files valid across rebuilds. M0 measures the
  size cost.
- **One file per syllable count** gives the same length-pure prefix ranges as
  the `N` prefix, at the cost of nine indexes; not used.
- **Binary Mark** remains deferred, as for spelling
  ([K29c](Lambda_Design_Concurrency.md)).

M0 records, for `zh-Hans`: lines and postings per table and per `N`; bytes
uncompressed and with XZ; dropped entries per reason; load time and retained
memory (bytes plus offsets); and exact, prefix and abbreviation lookup
latency, on a release build. No size is claimed before then.

---

## 7. Package API

All names are provisional. Fallible operations return `T^` (S11.4.2); option
maps are checked (S17.8.1).

| Function | Contract |
|---|---|
| `pn load(profile = "zh-Hans", options = null)` | Read an installed pack and return an engine; an unknown profile is an error listing the installed ones |
| `fn candidates(engine, input, options = null)` | The core query: `{segments, items, truncated, scanned}` for a raw pinyin string; each item is `{text, reading, consumed, coverage, match, weight}`. `consumed` is the raw code-point offset through the selected prefix (including an immediately following separator); `coverage` is its syllable count, per S2.5.8/S17.4.1. Implemented M1 options are documented in `doc/Lambda_IME_Chinese.md` |
| `fn session(engine, settings = null)` | A new idle session in Chinese mode with an empty user overlay; `settings` as in §5.7 |
| `fn key(engine, session, evt)` | The reducer of IME3: `{session, actions}` |
| `fn configure(engine, session, settings)` | Replace the session's settings: `{session, actions}`, refreshing an open composition's candidates (§5.7) |
| `fn settings(session)` | The current settings map, for the host to save |
| `fuzzy_pairs` | The supported fuzzy pairs with kind and example (§5.7) |
| `fn with_user(session, user)` | A session carrying a previously saved user overlay |
| `pn load_user(path)` / `pn save_user(user, path)` | Explicit persistence of `session.user` in the user file of §6.6 (IME8) |
| `pn validate(profile)` | Full pack validation (§6.3): a report of every violation; for preparation and tests, not for normal loading |

Actions:

| Action | Fields | Host effect |
|---|---|---|
| `compose` | `text`, `caret` | start or update the preedit |
| `candidates` | `items`, `page`, `pages`, `highlight` | show or update the candidate list; an empty list hides it |
| `commit` | `text` | end the composition, inserting `text` |
| `cancel` | — | end the composition, inserting nothing |
| `pass` | — | the key was not consumed; default handling continues |

```lambda no-run
// no-run: requires the locally prepared zh-Hans dictionary.
import ime: lambda.ime.chinese

pn main() {
    let engine = ime.load("zh-Hans")^
    let found = ime.candidates(engine, "beijingdaxue")^
    print(found.items[0].text)          // 北京大学
    var s = ime.session(engine)^
    for (k in ["b", "j", " "]) {
        let step = ime.key(engine, s, {type: "keydown", key: k})^
        s = step.session
        print(step.actions)
    }
}
```

---

## 8. Radiant integration

### 8.1 What exists

| Piece | Where |
|---|---|
| Composition event type with UTF-8 text and a code-point caret | `CompositionEvent`, `radiant/event.hpp:240` |
| OS bridges feeding it | `setMarkedText:` / `insertText:` in `radiant/ime_mac.mm:109`, `:174`; IMM32 in `radiant/ime_win.cpp` |
| A non-OS source feeding the same path | `SIM_EVENT_IME_COMPOSE` builds an `RdtEvent` and calls `handle_event`, `radiant/event_sim.cpp:4478` |
| DOM composition events, `beforeinput`, preedit painting | the unified editing controller (`editing_controller_handle_composition`, `radiant/event.hpp:1563`) |
| Composition session policy in Lambda | `lmd/package/dom/ime.ls` (`begin`, `update`, `end`) |
| Document-scoped key policy | `view <body>` and its `on keydown` → `keymap.run_shortcut`, `lmd/package/dom/form.ls:556`, `:563` |
| A prevented keydown also suppresses its character event | `pending_text_input_prevented`, `radiant/event.cpp:16032` |
| Caret rectangle for the focused editing surface | `radiant_editing_focused_caret_rect`, `radiant/event.cpp:6505` |
| A native, document-scoped popup | `context_menu_open`, `radiant/event.hpp:2082`; rows reach Lambda as an index (ES34) |

### 8.2 Key routing

When the input method is on and the focus is an editing surface, the body
template's `keydown` handler asks the engine first. A consumed key returns
`'prevent-default'`, which also stops its character event, so no letter leaks
into the document. A `pass` falls through to `keymap.run_shortcut` and the
rest of today's chain unchanged. The engine and session live in the body
view's state, beside `ime_composing`: the same document-scoped cardinality
argument ES18 made for the composition session (`lmd/package/dom/ime.ls`
header). How `lambda.dom` reaches the engine, by direct import or through a
registered input method, is IME-O1 (resolved: direct import). The implementation
is in `lambda.dom.ime`; its opaque engine/session value is rooted in the
owning document runtime (D5.3.3/D5.4.1). `<body data-ime="chinese">` enables a
document, `off` overrides a window enable, and a missing attribute inherits
`radiant.set_chinese_ime(owner, enabled)` (IME-O2). The host API and live
settings operations are documented in `doc/Lambda_IME_Chinese.md`.

### 8.3 Native enablers

**IME-E1 — Script-initiated composition.** A `radiant-dom` primitive,
provisionally `dom.compose(target, phase, text, caret)` with phases `begin`,
`update`, `commit` and `cancel` (the phases `event_sim` already uses), builds
a `CompositionEvent` and routes it through `handle_event`, exactly as
`SIM_EVENT_IME_COMPOSE` does. It is exported like `fn_radiant_set_ime_preedit`
(`lambda/module/radiant/radiant_module.cpp:3631`), per D7.5.3v2. **The event is
queued and dispatched after the current event returns**, never nested inside
the `keydown` dispatch that requested it: a nested dispatch would re-enter the
editing controller and the DOM behavior package mid-handler.

**IME-E2 — Candidate popup.** A native popup kind beside the context menu:
document-scoped, anchored below the caret rectangle from
`radiant_editing_focused_caret_rect`, laid out as a horizontal strip,
showing seven numbered options by default and a trailing mode/page indicator
(USER, 2026-10-26). Labels retain the editor's family with a reduced size of
85%, capped at 16 CSS pixels before zoom. Native font snapshots own their
cached handles and refresh from the selected editor before use (D4.5.1v4).
Measured strip widths wrap on narrow viewports. Down opens a scrollable grid
of all returned candidates; Up on its first row collapses to the strip,
preserving selection and preedit. Native layout reports its column count, while
Lambda owns expansion, navigation and selection policy (D7.2.5). Lambda supplies the rows; a click
reaches the behavior package as an index, the ES34 pattern. It is not a DOM
subtree, so author CSS, `MutationObserver` and selectors never see it.
Exposing it to assistive technology is a later step.

**IME-E3 — Mode indicator and switch.** A visible Chinese/English indicator
and the API that turns the input method on for a document or window (IME-O2).
The demo's selected buttons and live status expose the effective scope,
spelling and character profile. Choosing a window button clears its document
override; choosing Use window setting preserves the current window default.
The fuzzy master button toggles all selected sound pairs; an adjacent `v`
opens a per-pair configuration panel derived from `fuzzy_pairs` (IME13). All
11 pairs start enabled in the demo via `data-ime-fuzzy="all"` (USER,
2026-10-26). Off retains the selections;
checkbox changes while on refresh candidates and preserve preedit/page size.
An empty selection switches the master off. Done, Escape and an outside click
dismiss the bounded, scrollable panel. While it is open, `lambda.dom.ime`
suspends the native indicator and candidates without canceling composition.
All presentation paths honor that suspension, including live configuration
and focus. Dismissal restores a reducer frame snapshot, preserving the current
mode, page and highlight; an ineligible editor or active OS composition keeps
it hidden (D7.2.5, IME10). These are demo controls; the IME-O2 API
precedence and S17.8.1 settings contract remain unchanged.

### 8.4 Coexistence with OS input methods

An OS composition sets `state->editing.composition.active` before any
in-engine session could start; the adapter checks it and passes every key
(IME10). When both are enabled the OS input method wins, and the
documentation tells the user to switch the OS input source to a Latin layout
to use the bundled one.

---

## 9. Evaluation and acceptance

| Gate | Evidence |
|---|---|
| Data | Byte-identical rebuild from pinned sources; `validate` passes every rule of §6.3; manifest hashes and complete notices; no admitted word with a guessed reading; negative packs (unsorted lines, a broken abbreviation invariant, a truncated table) rejected |
| Segmentation | Exhaustive tests over the syllable table; ambiguity cases (`xian`, `fangan`, `jiangan`), separators, `v`, initials, mixed input, the length bound |
| Fuzzy settings | Each pair alone and all pairs together, including dropped invalid expansions and abbreviations; `"none"` and `"all"`; unknown pair names and settings keys rejected; `configure` during a composition refreshes candidates and keeps the input |
| Candidate quality | A frozen held-out list of words with their readings, from a corpus whose license permits committing the fixture; report the rank of the expected word for full pinyin and for initials: top-1, top-3 and first-page (top-9) rates, and simulated keystrokes per character. Thresholds are recorded from the first baseline before any tuning; no quality score is claimed here |
| Session | Golden key sequences → action lists: paging, Backspace undoing a selection, Enter, Esc, Shift toggle, punctuation, user-overlay promotion |
| Radiant | UI automation tests (`test/ui/ime_chinese_*.json`) on `<input>`, `<textarea>` and `contenteditable`: preedit shown, commit inserted, one undo step per commit, `maxlength`, cancel, OS-composition stand-down, popup click |
| Performance | Release build only (AGENTS.md rule 10), named host: pack load time, peak memory, per-key latency p50/p95. Provisional targets: load under 200 ms, per-key p95 under 5 ms; revised from evidence |

Golden-driven `.ls` fixtures live under `test/lambda/ime/`, each with its
`.txt`; the runner is registered in `test/README.md` when it exists.

---

## 10. Milestones

| Milestone | Deliverable | Exit gate |
|---|---|---|
| **M0 Data** | `utils/prepare_ime_chinese.py`; the `zh-Hans` pack in format v1 (§6), with manifest and notices, under `lmd/package/ime/chinese/data/zh-Hans/`; source choice settled (IME-O4) | Data gate of §9; the measurements of §6.7; coverage of the held-out list |
| **M1 Engine** | `ime/chinese.ls` with `syllables`, `lattice`, `pack`, `lookup`, `rank` modules; `load` and `candidates`; fuzzy-pair expansion and `fuzzy_pairs` | Segmentation and fuzzy-settings gates; candidate-quality baseline recorded |
| **M2 Session** | The reducer, punctuation, mode toggle, paging, settings with `configure` and `settings`, user overlay and its persistence | Session goldens in both execution tiers |
| **M3 Radiant** | IME-E1–IME-E3; the body-level adapter in `lambda.dom` | Radiant gate; `make test-radiant-baseline`; **first release** |
| **M4 Traditional** | The `zh-Hant` pack from OpenCC; implemented 2026-10-26 | 32 conversion fixtures; [frozen quality report](../test/ime/quality_zh_hant.md) |
| **Later** | Domain overlays (THUOCL), double pinyin, Zhuyin, popup accessibility | One at a time, each with its own gate |
| **Future** | Whole-sentence conversion (§11.3) | A separate proposal |

Package-only milestones run their focused fixtures in the interpreter and JIT
tiers. M3 changes Radiant and the DOM behavior package, so it also reads
`radiant/README.md` and runs the Radiant baseline. This proposal itself needs
document checks only.

---

## 11. Risks

### 11.1 Quality

- **Dated vocabulary.** The permissive sources stop between 2014 and 2020
  (§3.2), so recent words may be missing or low-ranked. Mitigate with measured
  coverage in M0, domain overlays later, and the user overlay; state the gap
  honestly against actively maintained GPL lexicons such as rime-ice.
- **Provenance.** Several permissive sources have undocumented upstream
  origins (§3.3). The pack records them; bundling waits on IME-O4.
- **Mixed frequency scales.** Sources count differently (corpus counts,
  normalized frequencies). Normalization is a preparation-time decision with
  its method recorded, and its effect is visible in the quality baseline.
- **Polyphones.** Admitting only words whose readings are recorded or
  choice-free (§3.2) trades coverage for correctness; M0 measures the
  coverage cost.

### 11.2 Engineering

- **Per-key latency in the interpreter tier.** The reducer runs on every
  keystroke; hot paths should promote to MIR JIT under D8.1.1v18's tiering.
  Measure both tiers; the release gate is the JIT tier on a release build.
- **Load cost.** Indexing a few megabytes in Lambda is a measurable startup
  cost; the adapter loads in its owning document runtime on first eligible
  enable/focus and explicit profile/directory changes, never at import
  (IME2, D5.4.1).
- **Re-entrant dispatch.** IME-E1 must queue its events (§8.3); a nested
  dispatch from inside `keydown` is the failure mode to test for.
- **Two input methods at once.** Covered by IME10 and §8.4, and tested.

### 11.3 Whole-sentence conversion (future)

Sentence conversion picks the best path through the same lattice with a
language model (bigram or trigram) and offers it as the first candidate of
kind `sentence`. It needs a permissively licensed model, which does not
exist ready-made among the sources above: it would be trained from a corpus
whose license allows distributing the derived counts, a question for that
proposal. Nothing in this design blocks it: the lattice (§5.1), the candidate
kinds (IME4) and the reducer contract (IME3) are the extension points.

---

## 12. Open items

| # | Question | Recommendation |
|---|---|---|
| IME-O1 | How does `lambda.dom` reach the engine: import `lambda.ime.chinese` directly, or call through an input method registered on the document? | **Resolved (USER, 2026-10-25):** direct import while there is one engine; generalize when a second scheme arrives |
| IME-O2 | How is the input method turned on: per document, per window, per application; by attribute, `radiant` API or setting? And the default toggle key? | **Resolved (USER, 2026-10-25):** a window-level `radiant` setting plus a per-document override; Shift tap toggles mode |
| IME-O3 | Candidate popup as a native popup kind (IME-E2) or a DOM overlay? | Native (ES34): a DOM overlay would mutate the author's document |
| IME-O4 | Which sources extend the AOSP base: `phrase-pinyin-data` `pinyin.txt` with jieba frequencies, HanLP's readings and frequencies, or both? Are the provenance flags of §3.3 acceptable for a bundled pack? | **Provenance resolved (USER, 2026-10-25):** the flagged sources may be used, with the flags recorded in the manifest and NOTICE. **Extension selected by M0 measurements (2026-10-25): both.** HanLP adds 14,588 admissible word readings over phrase + jieba for 168,088 additional compressed bytes; see the implementation record. This is coverage/size evidence, not a ranking-quality claim |
| IME-O5 | May users build optional packs from GPL, LGPL or CC BY-SA sources with the preparation tool, outside the bundle? | Allow it as user-side tooling; never ship the result |
| IME-O6 | Where is the user overlay saved by default? | The host application's settings location; no package default |
| IME-O7 | Are prepared packs committed to the repository or generated at build time from the pinned sources? | **Deferred (USER, 2026-10-25):** no data files are committed for now; `lmd/package/ime/chinese/data/` is gitignored and packs are prepared locally. The user decides distribution later |
| IME-O8 | Traditional regional variants (Taiwan, Hong Kong) | After M4, with OpenCC's regional tables |
| IME-O9 | Does Enter commit the remaining pinyin as raw letters (keeping words already chosen), or convert it first? | Raw letters (the common convention); converted text is a Space away |
| IME-O10 | Should the bundled character set extend beyond the 8,105 通用规范汉字 by default? | No; ship a separate larger pack |

---

## Appendix A. Proposed module map

| Module | Role |
|---|---|
| `lmd/package/ime/chinese.ls` | Entry: `load`, `candidates`, `session`, `key`, user overlay |
| `ime/chinese/syllables.ls` | The syllable table, initials, `v`/`ü`, fuzzy pairs |
| `ime/chinese/lattice.ls` | Segmentation into the lattice (§5.1) |
| `ime/chinese/pack.ls` | Pack reading, line-offset indexing and `validate` (§6) |
| `ime/chinese/lookup.ls` | Full-key, prefix and abbreviation queries (§5.2) |
| `ime/chinese/rank.ls` | The ranking rule (§5.3) |
| `ime/chinese/session.ls` | The reducer, paging, punctuation, mode (§5.4–§5.5) |
| `ime/chinese/data/zh-Hans/` | Prepared pack, manifest, NOTICE |
| `utils/prepare_ime_chinese.py` | Offline preparation (development tooling only) |
| `lmd/package/dom/ime.ls` | Extended with the key adapter (M3, IME-O1) |
