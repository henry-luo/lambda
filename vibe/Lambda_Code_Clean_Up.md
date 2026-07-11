# Lambda / Radiant Code Clean-Up Survey

**Date:** 2026-07-11
**Method:** Six parallel code-survey agents, each auditing one area against `./lib` and against sibling code. Every finding below was verified by reading the cited source (not inferred from file names). Line numbers are current-tree as of commit `95b2689fa`.

**Scope:** `lambda/` core (top-level), `lambda/input/`, `lambda/format/` + `lambda/validator/`, `radiant/` layout+CSS side, `radiant/` paint/display-list/editing side, `lambda/js/` vs Lambda core.

Categories used throughout:
- **C1** — should use `./lib` utils, but doesn't
- **C2** — can be moved to `./lib` and shared
- **C3** — duplicative/repetitive, should be simplified
- **C4** — similar/related, should be unified

---

## Executive Summary

The codebase (~590k LOC surveyed) is not uniformly duplicative — several subsystems are exemplary (markdown-family formatters are 14-line wrappers over table-driven `format-markup.cpp`; graph input parsers share `input-graph.h`; the render backends share `render_walk.cpp` via a vtable; JS and Lambda already share the libuv event loop, module registry, and JSON *parsing*). The problems cluster into **four recurring themes**:

1. **Shared helpers exist but are bypassed.** This is the single biggest pattern. Radiant layout has `layout_box_metrics()`, `layout_apply_min_max_width()`, `layout_alignment.hpp` (whose header even says "extracted from duplicated code in layout_flex and layout_grid") — yet flex/grid/intrinsic call them **zero times** and hand-roll the same math hundreds of times. Input parsers have `parse_shared_quoted_string` and `parse_hex_codepoint` with 0–1 callers while JSON/TOML/Mark clone the logic. `lib/escape.h` rule tables exist while XML/SVG escaping is hand-rolled in 6+ places. **The primary lever is migration to existing helpers, not new abstraction.**

2. **The two-transpiler / two-runtime split duplicates infrastructure verbatim.** `transpile.cpp` vs `transpile-mir.cpp` carry byte-identical analysis helpers; `js_mir_*`'s `jm_call_N`/`jm_new_reg` are line-for-line copies of `transpile-mir.cpp`'s `emit_call_N`/`new_reg`; `js_double_to_string` is a near-verbatim copy of `lambda_double_to_shortest`.

3. **Dead or orphaned duplicate implementations.** `input-kv.cpp` (a full unified INI+Properties engine, ~480 LOC) was written as the replacement for `input-ini.cpp`+`input-prop.cpp` but never added to the build; `.bak` files (`layout_table.cpp.bak` 127 KB, `render_svg.cpp.bak`, `render_img.cpp.bak`, `mark_editor.cpp.bak`, `runner.cpp.bak`) sit beside live sources; `format-utils.cpp` carries dead `markdown_escape`/`wiki_escape`/`rst_escape` functions where every branch returns NULL.

4. **Per-file static-helper copy-paste in lambda/js.** `make_string_item` is redefined `static` in **22 files**; `make_js_undefined` in 21; `is_callable` in 8.

**Estimated removable duplication:** roughly 4,000–6,000 LOC of direct clones, plus large maintainability wins from table-driving the op/builtin dispatch families.

### Top 10 highest-leverage actions

| # | Action | Area | ~LOC | Effort | Risk |
|---|--------|------|------|--------|------|
| 1 | Extract `lib/dtoa` from `lambda_double_to_shortest` ≡ `js_double_to_string` | JS↔core | 135 | Low | Low |
| 2 | Resolve INI/Properties triple implementation (wire `input-kv.cpp`, delete `input-ini`/`input-prop`) | input | 480 | Med | Med |
| 3 | Migrate radiant layout to existing `layout_box_metrics`/`layout_apply_min_max_*` helpers | radiant | 450–750 | Med | Low |
| 4 | De-clone `dl_replay_tile` vs `dl_replay` + shared glyph raster | radiant | 530 | High | Med |
| 5 | Kill the 22×/21×/8× static helper copies in lambda/js | JS | 270 | Trivial | Low |
| 6 | `MirEmitter` base shared by `transpile-mir.cpp` and `js_mir_*` | JS↔core | 300–500 | Med-High | Med |
| 7 | Shared formatter dispatch visitor + `ElementReader` attribute iterator | format | 290 | Med | Med |
| 8 | Unify quoted-string + `\uXXXX` escape parsing on `parse_shared_quoted_string` | input | 450 | Med | Med |
| 9 | Move 5 duplicated analysis helpers into `transpile_shared.cpp` | core | 120 | Low | Low |
| 10 | Replace 3 hand-rolled base64 decode tables with `base64_decode_variant` | JS | 90 | Low | Low |

---

## 1. Lambda Core (`lambda/` top-level)

Already good: `name_pool.cpp`/`shape_pool.cpp` use `lib/hashmap`; `lambda-eval.cpp` uses `str_utf8_*`; `main.cpp`/`runner.cpp` use `read_text_file`/`read_binary_file`. `lambda-data.cpp` vs `lambda-data-runtime.cpp` is a clean arena/GC split, not duplication.

### 1.1 Duplicated analysis helpers between the two transpilers — C3, HIGH
Byte-for-byte copies under a `mir_` prefix; all are pure functions of `TypeMap`/`ShapeEntry`/`AstNode` with zero backend dependency:

| transpile.cpp | transpile-mir.cpp |
|---|---|
| `find_shape_field_by_name` (5993–6003) | `mir_find_shape_field` (1470–1480) |
| `has_fixed_shape` (6013–6023) | `mir_has_fixed_shape` (1457–1467) |
| `is_direct_access_type` (6092–6105) | `mir_is_direct_access_type` (1495–1508) |
| `resolve_field_type_id` (6112–6119) | `mir_resolve_field_type` (1485–1492) |
| `detect_ndim_literal` (4685–4730) | `mir_detect_ndim_literal` (4739–4785) |

~120 LOC. **Fix:** move into `transpile_shared.cpp` (exists for exactly this; only 103 lines today), delete the `mir_` twins. Only the emit siblings legitimately differ. Effort: low.

### 1.2 Hand-rolled Item→number extractors vs canonical `it2d`/`it2i`/`it2l` — C1, HIGH
Canonical converters live in `lambda-data.cpp:322–392+`. Reimplemented: `item_to_int_value`/`item_to_float_value` (`lambda-data-runtime.cpp:665–718`), `item_to_double`/`item_to_integral_index` (`lambda-vector.cpp:34–54, 161–172`). Copies differ only in NUM_SIZED/decimal/NaN edge handling — classic drift-bug territory. ~90 LOC. **Fix:** consolidate on `it2*` (add `_checked` variants if the extra semantics are needed); decide one canonical NUM_SIZED behavior. Effort: medium.

### 1.3 ArrayNum element-read switch duplicated 3–4× — C3/C1, HIGH
Canonical `array_num_read_scalar_at` (`lambda-data-runtime.cpp:240–264`) vs `read_compact_elem` (`print.cpp:11–29`) vs `compact_elem_to_double` (`lambda-vector.cpp:180–197`). ~60 LOC. **Fix:** expose `arraynum_read_item(arr,i)` / `arraynum_read_double(arr,i)` from the runtime; delete `read_compact_elem`. Verify the intentional UINT64 heap-vs-truncate difference first. Effort: low-medium.

### 1.4 "Walk each named field in a map shape" loop open-coded ~30× — C4, MED-HIGH
`ShapeEntry* field = map_type->shape; while (field){ ... field = field->next; }` at `lambda-eval.cpp` 1310, 1329, 2111, 2127, 2177, 2197, 2306, 2333, 5958, 6147, 6241, 6367, 6468 (14 sites), 9 sites in `transpile.cpp`, plus `lambda-data*.cpp`. Formatters and validator clone it too (see §3.2). **Fix:** a `FOR_EACH_MAP_FIELD(map_type, map_data, field, value)` iterator/macro in `lambda-data.hpp` yielding `(ShapeEntry*, Item)`. Effort: medium, mechanical.

### 1.5 Ad-hoc scalar-type predicate chains ~85× — C3/C4, MED-HIGH
`tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 || tid == LMD_TYPE_FLOAT || ...` appears 42× in `transpile.cpp`, 35× in `transpile-mir.cpp`, 8× in `build_ast.cpp`, plus named variants with *subtly different membership*: `has_typed_params` (`transpile_shared.cpp:14–37`), `mir_is_native_param_type` (`transpile-mir.cpp:673–684`), `is_numeric_type` (`transpile.cpp:352`), `is_scalar_numeric` (`lambda-vector.cpp:67`). **Fix:** canonical predicates (`is_native_scalar_type`, `is_unboxable_param_type`, `is_numeric_type`) defined once near `TypeId`; replace deliberately, not blindly — memberships differ per site. Effort: medium.

### 1.6 `utf_string.cpp` copy-paste wrappers — C3, some C2, MED
Five `normalize_utf8proc_nfc/nfd/casefold/nfkc/nfkd` (`utf_string.cpp:17–84, 221–267`) identical except the option flags; five comparators `equal/less/greater/less_equal/greater_equal_comp_unicode` (133–218) identical except a one-line result map. ~150 LOC → ~50. **Fix:** one parameterized `normalize_utf8proc(str,len,out_len,opts)` + one comparator core. The normalize group is generic and a candidate for `lib/utf` (which has no normalization today). Effort: low.

### 1.7 Smaller items
- **`shape_pool.cpp` hand-rolls hashmap glue** (`shape_signature_hash`/`compare`, raw 8-arg `hashmap_new`, :21–47, 99–107) while `name_pool.cpp:18` uses `HASHMAP_DEFINE_LENSTRKEY` cleanly — C1, low effort.
- **Refcount + mem_node + release-hook lifecycle duplicated** between `name_pool.cpp` (63–94) and `shape_pool.cpp` (118–144) — C4; extract a shared `RefCountedPool` base (candidate home: `lib/mem_factory.h`). ~40 LOC.
- **`print.cpp` number/decimal/escape formatting is inline** (189–791) rather than shared with `format-utils`/`lib/escape` — C3; coordinate with §3 (findings 3.4/3.5 there).
- **`shape_builder.cpp` repeats the strcmp linear scan 4×** (add/remove/has/get, 29–114) — factor one `shape_builder_find()`. Low effort.
- **`emit_current_func_error_return` (`transpile.cpp:164–187`) re-hardcodes zero values** the `type_box_table.zero_value` column already defines — drive both backends from the table.
- **Dead/unreachable duplicate branches in `lambda-eval.cpp`** — `query_collect` has consecutive identical `else if (type_id == LMD_TYPE_ARRAY)` guards at 2135/2140 and 2207/2222; the second is unreachable and one was likely meant to be `LMD_TYPE_LIST`. **Latent bug — check and fix.**
- **`NUM_SIZED` sub-switch decode duplicated** (`lambda-data-runtime.cpp:673–685, 701–712`, `array_num_set_item` 956+) vs the `get_num_sized_as_double()` accessor path (`lambda.hpp:213`) — standardize on accessors, add `get_num_sized_as_int64()`.
- **`array()/array_int()/array_float()/array_int64()` constructor quadruples** in `lambda-data-runtime.cpp:113–592` are hand-templated per element type — X-macro/table candidate.
- **`mark_builder.cpp:44–45` and `mark_editor.cpp:15–16` re-`extern` `map_put`/`elmt_put`** ad hoc — declare once in a shared header. (Checked: `mark_builder` vs `shape_builder` do NOT meaningfully overlap — different domains.)
- **Stale `.bak` files:** `mark_editor.cpp.bak`, `runner.cpp.bak` — delete, rely on git.

---

## 2. Input Parsers (`lambda/input/`)

Already good: graph parsers share `input-graph.h` (`skip_wsc`, `read_graph_identifier`, `parse_shared_quoted_string`); VCF/ICS share `input-rfc-text.h`; JSON-family auto-typing goes through `parse_typed_value`/`parse_integer_token_exact`.

### 2.1 INI + Properties implemented twice; the unified engine is dead code — C3/C4, HIGHEST
`input-kv.cpp` (282 LOC) is a config-driven engine defining `parse_ini` + `parse_properties` (`KvConfig INI_CONFIG`/`PROP_CONFIG`, `parse_kv_document` L214) and its header comment documents it as the unified replacement — but `build_lambda_config.json` **never compiles it** (it compiles `input-ini.cpp` L195 + `input-prop.cpp` L142 instead). ~480 LOC of parallel key/value/section/comment logic. **Fix:** switch the build to `input-kv.cpp` and delete `input-ini.cpp`+`input-prop.cpp` (preferred — cleaner design), or delete the dead file. Regression-test against INI/properties corpora; watch error-message text. Effort: medium.

### 2.2 Quoted-string parsing cloned across (and within) parsers — C3, HIGH
`input-utils.hpp:parse_shared_quoted_string` (173–207) exists but only graph parsers use it. Clones: `input-json.cpp:parse_string` (13–63) **plus a second inline copy in the object-key loop** (187–207); `input-mark.cpp:parse_string`/`parse_symbol` (39–120); `input-toml.cpp` has **five** near-identical ~30-line copies (`parse_quoted_key` 223–259, `parse_literal_key` 261–290, `parse_basic_string` 292–328, `parse_literal_string` 330–360, `parse_multiline_basic_string` 362+); `input-xml.cpp:parse_string_content` (20+). ~250+ LOC. **Fix:** extend `parse_shared_quoted_string` with a policy struct (quote char, escape handler, allow-newline); route JSON/TOML/Mark/XML through it. At minimum JSON's key loop should call its own `parse_string`. Effort: medium (escape/error semantics vary per format).

### 2.3 `\uXXXX` + surrogate-pair handling triplicated — C3/C1, HIGH
`input-utils.hpp:parse_escape_char` (71–138), `input-toml.cpp:handle_escape_sequence` (32–165, near-verbatim clone plus `\U`), `input-mark.cpp` inline (58–76) — all hand-roll `strtol(hex,16)` + `0xD800..0xDBFF` combining. Meanwhile `input-utils.cpp:parse_hex_codepoint` (29–42) exists for exactly this and has **zero callers**. ~200 LOC. **Fix:** one shared `\u`/`\U` routine built on `parse_hex_codepoint` + `str_utf8_encode`. Effort: medium.

### 2.4 Line/column tracking proliferation — C4, MED (flag to stop the bleeding)
Canonical `SourceTracker` (`source_tracker.hpp`, UTF-8-aware, O(1)) is bypassed by: `input-yaml.cpp` `YamlParser{pos,line,col}` + hand-rolled `advance` (46–92) — *even though YAML holds an `InputContext*` with a live tracker*; `html5/html5_tokenizer.cpp` (:21, :1111, iterator 135–231); `css/css_engine.hpp:266–267`; `css/css_parser.hpp` (two structs, 111–112, 141–142); `markup/markup_parser.cpp`. **Fix:** migrate YAML to `ctx.tracker`; give the CSS/HTML5 byte-cursors a small shared `LineCounter`. Effort: high (YAML) — mainly enforce "no new trackers."

### 2.5 lib/str.h adoption gaps — C1
- **ctype on signed char (UB for UTF-8 bytes ≥0x80)** in ~30 files: `input-graph.h:36, 65–72`, `input-jsx.cpp:28–34`, `input-mdx.cpp`, `input-toml.cpp`. `lib/str.h` §17 documents this exact hazard and provides `str_char_is_ascii_*`. Mechanical replace.
- **Dead `input-utils` helpers duplicating lib:** `input_strncasecmp` (wrapper of `str_icmp`), `try_parse_int64`, `input_split_lines`, `input_trim_whitespace` — 0 callers each; `try_parse_double` has 1. Delete; point future needs at `str_to_int64`/`str_to_double`/`str_trim`/`StrSplitIter`.
- **Trailing-trim tail loops** open-coded in `input-kv.cpp:110,144`, `input-prop.cpp:129`, `input-ini.cpp:131`, `input-graph-d2.cpp:70`, `input-yaml.cpp:447,774` — use `str_rtrim`.
- **`input-mark.cpp:parse_binary` (145–203)** hand-checks hex/base64 char classes 3× and `#include`s `lib/base64.h` without using it — use `str_char_is_hex`, `lib/hex.h`, `lib/base64.h`.
- **`parse_typed_value` numeric sniff (`input-utils.cpp:148–177`)** — base on `str_to_int64`/`str_to_double` end-pointer checks instead of a bespoke char loop.

### 2.6 Other repetition
- **Float boxing:** ~10 sites in `input-toml.cpp` (e.g. 482–503, 604–618) and `input-mark.cpp` (236–252) do `pool_calloc(sizeof(double))`+null-check+`lambda_float_ptr_to_item` when `MarkBuilder::createFloat` exists (and `input-json.cpp:97` already uses it). Low effort.
- **Number parsing:** the "strtod → scan for float marker → integer promotion" shape recurs (`input-json.cpp:65–99`, `input-toml.cpp:472–645`, `input-mark.cpp:236–252`, YAML ×8); TOML additionally repeats its radix blocks 3× (530–584). **Fix:** shared `parse_scanned_number(ctx,&p,flags)` in input-utils. ~120 LOC.
- **Whitespace/comment skipping:** `input-yaml.cpp:94–148` (5 helpers), `input-jsx.cpp:36–40`, `input-mark.cpp:skip_comments` (16–37) re-implements the same line+block comment skip as `input-graph.h:skip_wsc`. Consolidate one marker-parameterized helper. ~120 LOC.
- **`skip_to_newline` divergence:** `input-utils.h:105–114` vs an INI-local variant with different signature (`input-ini.cpp:11–26`) vs YAML `skip_line` — extend the shared one to sync a tracker.
- **MDX vs JSX tag machinery:** `input-mdx.cpp:23–50` vs `input-jsx.cpp:42–44` duplicate component-vs-HTML classification and tag-name scanning; MDX re-finds JSX bounds instead of delegating. Extract `jsx_common.h`. ~80 LOC.
- **JSX drops entity resolution** (`input-jsx.cpp:164–186` "preserve as-is for now") that XML centralizes via `html_entity_lookup` (`input-xml.cpp:74–98`) — route JSX through `html_entities.h` (behavioral change; confirm roundtrip expectations).
- **EML vs RFC-text scaffold:** `input-eml.cpp:parse_header_value` (35–60) re-implements folded-header reading that `input-rfc-text.h` provides to VCF/ICS. ~40 LOC; verify RFC 5322 nuances.
- **`append_codepoint_utf8` twin overloads** (`input-utils.hpp:40–58`) differing only in buffer type, both appending byte-at-a-time — unify + bulk-append.
- **Dispatch chains:** `input.cpp:mime_to_parser_type` (733–795, ~35 strcmps) + `effective_type` dispatch (852–930, ~25-way if/else) + markup flavor sub-dispatch (876–901) — replace with static `{string, enum}` tables.
- **C2 candidate:** `html_entities.*` is consumed beyond input (`css/dom_node.cpp`, `css/dom_element.cpp`, `html5_tokenizer.cpp`, `markup/`) — promote to `lib/` (or `lambda/text/`) so formatters can share the reverse map. Also verify `markup/block/block_html.cpp` reuses `format/html-defs.h` raw-text classifiers rather than a private list.
- **HTML5 tokenizer UTF-8 DFA** (`html5_tokenizer.cpp:92–118`) is a legitimate perf choice — leave it; just prevent new hand-rolls (use `lib/str.h`/`lib/utf.h`).

---

## 3. Formatters + Validator (`lambda/format/`, `lambda/validator/`)

Already good: `lib/escape.h` centralizes escape tables; `format-md/org/rst/wiki/textile.cpp` are 14-line wrappers over table-driven `format-markup.cpp`; `format-kv.cpp` unifies INI/Properties output; `html-defs.cpp` centralizes tag classification.

### 3.1 Type-dispatch skeleton cloned 8× — C3, HIGH
Every serializer opens with the identical `RecursionGuard` + `isNull/isBool/isInt/isFloat/isString/isArray/isMap/isElement` ladder; only leaf emits differ. Sites: `format-json.cpp:151–232`, `format-yaml.cpp:193–241`, `format-toml.cpp:39–75`, `format-text.cpp:132–160`, `format-kv.cpp:89–133`, `format-html.cpp:351–440`, `format-xml.cpp:182–346`, `format-latex.cpp:17–53`. The `"[max_depth]"` sentinel is re-spelled in each. ~200–240 LOC. **Fix:** a `FormatterContextCpp::dispatch_item(item, Handlers&)` (or CRTP visitor) owning guard + ladder; formatters supply leaf handlers. Start with json/toml/kv (closest). Effort: medium.

### 3.2 `map_type->shape` attribute-walk cloned 5× → add `ElementReader::attrs()` — C3, HIGH (best value-per-line)
`format-json.cpp:105–119`, `format-yaml.cpp:147–163`, `format-xml.cpp:263–288`, `format-html.cpp:286–323`, `format-jsx.cpp:58–113` all reach through raw `TypeMap*`/`ShapeEntry*`; `format-jsx.cpp:56–57` even documents the root cause: "ElementReader lacks a generic attribute iterator." The validator does the same at `validate.cpp:430–440`. ~90 LOC + unblocks 3.1/3.8. **Fix:** add an attribute iterator to `ElementReader` mirroring `MapReader::entries()`. Effort: low-medium. *(Same root pattern as core §1.4 — coordinate.)*

### 3.3 XML hand-rolled escape switch — C1, HIGH
`format-xml.cpp:14–80` — 66-line manual switch duplicating `ESCAPE_RULES_XML_ATTR` (declared in `escape.h`/`format-utils.h:78`), the control-char handling in `format_escaped_string_ex(...ESCAPE_CTRL_XML_NUMERIC)`, and the entity-preservation logic in `format_html_string_safe` (`format-utils.cpp:202–279`). ~70 LOC. **Fix:** use the shared escaper; factor "don't double-encode entities" into one `format_markup_string_safe()` shared by HTML+XML. Verify against XML corpus (apostrophe handling differs).

### 3.4 Three TypeId→name implementations — C4, HIGH (correctness)
Canonical `get_type_name()` (`lambda-data.cpp:99`, uses `type_info[]`) vs `validator/error_reporting.cpp:22–28 get_type_name_str()` (same lookup, re-done) vs `validator/doc_validator.cpp:470–484 type_to_string()` (hand-written switch that silently drifts). **Fix:** delete both validator copies, delegate to `get_type_name(type->type_id)`; keep the `TYPE_NUMBER`→"number" special case.

### 3.5 Number/datetime formatting bypasses shared helpers — C1, MED
- Shared `format_number()` (`format.cpp:41–95`, used by kv/toml/yaml/latex) is re-implemented inline in `format-json.cpp:163–171`, `format-text.cpp:22–36`, `format-html.cpp:33–43, 369–372`, `format-xml.cpp:108–116, 277–280`. Add a nan/inf-policy arg (JSON emits "null") and route all through it. ~40 LOC.
- Datetime→ISO8601 done 3 ways: `format-json.cpp:221–227`, `format-yaml.cpp:208–219` (StrBuf + `datetime_format_iso8601` dance) and `format-text.cpp:49–61` (a divergent manual snprintf **that drops the time component** — correctness inconsistency). Add one `write_datetime_iso8601()` helper. ~30 LOC.

### 3.6 Escaper proliferation — C1/C2/C3, MED
Three parallel escaping mechanisms now exist: the `EscapeRule[]`/`escape_append` engine in `lib/escape.c`; `format_text_with_escape`+`TextEscapeConfig` (`format-utils.cpp:91–125`) whose `markdown_escape`/`wiki_escape`/`rst_escape` helpers (24–69) are **entirely dead** (every branch returns NULL); and `format-graph.cpp:9–33`'s `GraphEscapeRules`. **Fix:** delete the dead escape_fns; express Markdown/RST/Wiki/DOT/Mermaid/D2 escaping as `EscapeRule[]` tables in `lib/escape.h`; retire the two bespoke engines. ~80 LOC.

### 3.7 Math ASCII/LaTeX dispatch ladders — C3, MED
`format-math-ascii.cpp:496–562` and `format-math-latex.cpp:645–704` — two ~30-branch `strcmp(tag,...)` ladders with near-identical tag sets; both also re-declare `format_children` identically (ascii:723, latex:710). ~70 LOC. **Fix:** extend `format-math-shared.hpp` with a `{tag, handler-slot}` table + per-format function-pointer struct (slots optional — tag sets differ slightly). Hoist `format_children`.

### 3.8 Smaller items
- **Entry-point pool ceremony ×10** (`stringbuf_new` → `mem_pool_create` → ctx → destroy → `to_string`) at `format-json.cpp:238,249`, `format-xml.cpp:349,415`, `format-yaml.cpp:254`, `format-latex.cpp:183`, `format-html.cpp:51,139`, `format-text.cpp:167`, `format-markup.cpp:1247`, several with `_to_strbuf` twins — a `ScopedFormatPool` RAII/template helper (also fixes early-return leak risk). ~40 LOC.
- **XML map path walks the map 3× and duplicates attr-value emit** (`format-xml.cpp:92–180, 260–289`) — single `xml_emit_attr_value()` + one partitioning pass. ~50 LOC.
- **HTML special-node memcmp cascade** (`format-html.cpp:160–280`, seven `#document/#doctype/#comment/...` blocks; the "emit first child verbatim" idiom ×5) — table + helper; overlaps XML's `?xml` handling (`format-xml.cpp:238–255`). ~40 LOC.
- **YAML quoting heuristic** (`format-yaml.cpp:22–69`) — extract a testable `yaml_scalar_needs_quotes()`; longer-term share the numeric-literal recognizer with the input side.
- **Validator** re-walks structures with the same raw-shape boilerplate as formatters (`validate.cpp:317, 392, 430–440, 551, 603–628`) — adopt the §3.2 iterators.
- **`print.cpp` overlap** — flagged jointly with core §1.7: `print` is effectively a Lambda-native serializer parallel to format-json; share number/datetime/escape helpers.

---

## 4. Radiant Layout + CSS (`radiant/` layout side)

**Answer to "multiple sub-layouts — any code can be shared?": yes, substantially — but the primary lever is adopting the shared helpers that already exist** (`layout_box.cpp`, `layout_alignment.hpp`, `layout_axis.hpp`, `available_space.hpp`, `lib/`), which flex in particular has largely bypassed. Checked and NOT a problem: CSS keyword→enum mapping is properly shared via `css_value.hpp` (only 3 strcmp sites in `resolve_css_style.cpp`).

### 4.1 Box-metric sums hand-rolled ~160+ times despite `layout_box_metrics()` — C3/C2, HIGHEST
The helper (`layout_box.cpp:5–67`) computes exactly `padding_h/border_h/pad_border_h/...` — but is called in only 4 places, all in `layout_table.cpp`; block/flex/grid/positioned/inline/intrinsic call it **0 times**. Grep: 84 `padding.left + padding.right`, 79 `border->width.left + …right`, 19 margin sums, each usually with the repeated `if (bound->border)` guard the helper encapsulates. Sites incl. `layout_block.cpp:3012, 3177, 3211, 3226, 5694, 6389`, `layout_flex.cpp:110, 222, 418, 631, 772, 935, 1013`, `intrinsic_sizing.cpp:2445, 2451, 2533, 6123`. ~300–500 LOC. **Fix:** mechanical migration. Low risk, medium effort, big readability win.

### 4.2 Min/max clamping reimplemented despite `layout_apply_min_max_width/height()` — C3, HIGH
Helper at `layout_box.cpp:79–131` (incl. min-overrides-max precedence + border-box floor). `layout_flex.cpp` and `layout_grid.cpp` call it **0 times** yet contain dozens of inline clamps (`layout_flex.cpp:2361–2362, 3073–3076, 3221, 5356, 5373`, `layout_table.cpp:462, 9340–9349`, `intrinsic_sizing.cpp:6405`, +82 inline `given_max_*` comparisons). ~150–250 LOC. **Fix:** migrate; add a `layout_apply_min_max_axis()` wrapper for flex's axis-generic needs. Do flex last (FLT_MAX/auto-min subtleties).

### 4.3 Seven+ `is_out_of_flow` clones + 96 raw inline checks — C3/C4, HIGH
Defs at `layout.cpp:1085, 1582`, `layout_inline.cpp:52`, `layout_multicol.cpp:262`, `layout_block.cpp:1787, 2739`, `layout_text.cpp:1500`, `layout_table.cpp:2257`, `stacking_order.cpp:19` — all test `ABSOLUTE || FIXED` (some also float). Plus parallel float-detection helpers (`intrinsic_sizing.cpp:1947`, `layout_table.cpp:5427`, `layout_block.cpp:5234`). **Fix:** canonical `is_absolutely_positioned(View*)`, `is_out_of_flow(View*)`, `is_floated(View*)` in `view.hpp`/`layout_box.hpp`; expose both with/without-float variants. ~120 LOC incl. inline sites. Low risk, high clarity payoff.

### 4.4 Flex never migrated to `layout_alignment.hpp` — C4, HIGH
`layout_alignment.hpp:11` says "Extracted from duplicated code in layout_flex.cpp and layout_grid.cpp" — grid-multipass uses it (2 calls), flex uses it **0 times** and re-implements the full justify-content switch (`layout_flex.cpp:4599–4658`) plus a second cross-axis block. ~60+ LOC. Caveat: flex uses int truncation for spacing vs float in the shared version — sub-pixel behavior may shift; re-baseline layout tests.

### 4.5 Four recursive first-baseline walkers — C3, MED-HIGH
`layout_alignment.cpp:209` (shared, grid-only user), `grid_baseline.hpp:131–165`, `layout_flex.cpp:3494–3563`, `layout_table.cpp:905, 973` — same recursion, same `font_size * 0.8` ascent, same skip-positioned + `padding.top + border.top`. ~150 LOC. **Fix:** one `compute_first_baseline(View*, options)` in `layout_alignment.cpp`; table keeps its cross-cell aggregation. Baseline is subtle — medium risk.

### 4.6 Percentage re-resolution: grid and flex built parallel machines — C3/C4, MED-HIGH
`layout_grid_multipass.cpp:57–254` (full candidate/cascade machine: `GridPaddingCandidate`, `grid_re_resolve_item_percentage_*`) vs flex's ad-hoc version (`layout_flex_measurement.cpp:368, 2577, 2592`, `layout_flex.cpp:202, 1963–2002`). Also both engines duplicate "containing content width for child percentages" near line-for-line (`flex_item_content_width_for_child_percentages` `layout_flex_measurement.cpp:368–427` vs `grid_container_content_width_for_item_percentages` `layout_grid_multipass.cpp:256–305`). ~320 LOC. **Fix:** new `layout_percentages.cpp` with `layout_reresolve_percentage_box(item, inline_base)` + `layout_resolve_containing_content_width/height()`; both engines call it. Also likely closes flex feature gaps on logical properties.

### 4.7 Box-shorthand re-parsing — C1/C4, MED
`intrinsic_sizing.cpp:507–560` re-implements CSS `padding: a/ab/abc/abcd` side-selection and `intrinsic_border_width_from_shorthand_value` (364–453); `layout_grid_multipass.cpp:64–72` and flex-measurement have variants — all duplicating what `resolve_css_style.cpp` does when populating `bound->padding`. ~200 LOC. **Fix:** extract `css_expand_box_shorthand_side(value, side)`; better, have intrinsic sizing read resolved `bound` values where it can.

### 4.8 Other items
- **Hand-rolled growable arrays** (C1): `layout_flex.cpp:586–588, 2091, 3955–3971`, `layout_grid.cpp:25–31, 1057, 1179`, `grid_utils.cpp:43–48, 168, 320` — each with its own `count/allocated/realloc×2` loop ≡ `lib/arraylist`. Perf-sensitive: measure, or share one `grow_ptr_array()` helper. ~120 LOC.
- **Flex scratch arrays** — 7 parallel `mem_calloc(line->item_count,…)` per resolve pass (`layout_flex.cpp:4076–4092, 4315–4318`); use one `FlexItemScratch` struct array and consider `lib/scratch_arena.h` (purpose-built).
- **Auto-margin centering math** repeated (`layout_block.cpp:3498, 6462, 6986, 7078`, `layout_flex.cpp:4553–4557, 4667–4671, 4990`, `layout_flex_multipass.cpp:906, 918`) — `layout_center_in_free_space()` + `count_auto_margins()`. ~60 LOC.
- **`layout_axis.hpp` under-adopted** (C2/C3): flex has ~129 manual `is_horizontal` branches duplicating "row→width/x, column→height/y" that `layout_axis_size/pos/set_*` abstract. Incremental adoption in hot paths.
- **Box-sizing unwinding** — 59 inline `box_sizing == CSS_VALUE_BORDER_BOX` subtract sites despite `layout_content_width_from_border_box()` (`layout_box.cpp:45–67`; flex-measurement 384–423 uses it correctly, most others don't). ~120 LOC, overlaps 4.1.
- **Multiple containing-block resolvers** (C4): `layout.cpp:50`, `layout_positioned.cpp:512`, `view_pool.cpp:32`, `layout_multicol.cpp:662` — unify into `layout_containing_block.cpp`; verify abs vs fixed vs ICB nuances. ~80 LOC.
- **`clamp_non_negative`/local clamps** (C1): `layout_containing_block.cpp:9`, `render_filter.cpp:39`, and `view.hpp`'s own `clamp/sign/lerp` (clash acknowledged in `math_utils.h:4–5`) — reconcile with `lib_math` (see also §5.6).
- **Child-iteration + skip loops**: 67 walks in `layout_block.cpp`, 52 in `layout_table.cpp`, 20 in `intrinsic_sizing.cpp`; `collect_and_prepare_flex_items` (`layout_flex.cpp:2101`) and `collect_grid_items` (`layout_grid.cpp:673`) are parallel — start with a shared `collect_in_flow_children()`.
- **Delete `.bak` files:** `layout_table.cpp.bak` (127 KB), `render_svg.cpp.bak`, `render_img.cpp.bak`.
- **Lower confidence, worth a focused diff:** `resolve_htm_style.cpp` (2172 lines) presentational-attribute mapping vs `resolve_css_style.cpp` length resolution.

---

## 5. Radiant Paint / Display-List / Editing (`radiant/` non-layout side)

Checked and NOT duplicated: view-tree render traversal is properly shared (`render_walk.cpp:240–374` via the `RenderBackend` vtable in `render_backend.h:23`); `rc_*` painter facade is thin delegation; `paint_ir` uses `lib/strbuf`; `graph_theme.cpp:29` already migrated to `lib/color.h` — good precedents.

### 5.1 `dl_replay_tile` is a near-complete clone of `dl_replay` — C3, HIGHEST
`tile_pool.cpp:526–1092` (~566 LOC) vs `display_list_replay.cpp:48–273` (~226 LOC): same item iteration, same skip/cull pre-pass, same backdrop stack + shadow-clip save buffer, same op-switch dispatching identical `rdt_fill_*` calls (compare gradient cases `display_list_replay.cpp:113–129` vs `tile_pool.cpp:652–674`). Tile re-inlines the backdrop stack (`tile_pool.cpp:531–538`) that `DisplayReplayBackdropStack` already encapsulates. ~350 LOC. **Fix:** one replay core parameterized by a coordinate/surface-target strategy (full-surface vs tile-local offset); reuse `DisplayReplayBackdropStack`/`DisplayReplayShadowClip`. Hot path — medium risk, high effort, high payoff.

### 5.2 Glyph rasterization cloned between tile and replay — C3, HIGH
`tile_pool.cpp:338–525` vs `display_list_replay_glyph.cpp:6–256`. `tile_blend_glyph_coverage_pixel` (357–386) is **byte-identical** to `dl_blend_glyph_coverage_pixel` (52–81); mono/gray/LCD samplers and transform/emoji/plain loops are the same logic with only coordinate translation differing. ~180 LOC. **Fix:** shared `glyph_raster.hpp` (sampler + blend); parameterize `draw_glyph` by a dst-pixel addressing lambda.

### 5.3 Paint-op boilerplate across 6 parallel op families — C3, HIGH (structural)
The same op set is enumerated independently in: `paint_ir.cpp`'s 4 giant switches (`paint_ir_validate:129`, `paint_ir_lower_raster_internal:952`, `paint_op_name:1364`, `paint_ir_lower_svg_unchecked:1530` — 108 `case PAINT_*` labels), the record mirror (`display_list_record_vector/raster/effects.cpp`), the replay dispatch, and the tile dispatch. Adding one primitive touches ~6 switch sites. **Fix:** X-macro / op-descriptor table (name, struct, bounds-fn, lower-raster-fn, lower-svg-fn) generating validate/name/lower uniformly. High effort, large maintainability win.

### 5.4 lib reuse gaps — C1, quick wins
- **XML/SVG escaping hand-rolled** in `paint_ir.cpp:1205–1230`, `render_svg.cpp:497–501, 1590–1592`, `render_svg_inline.cpp:5157–5161`, plus inline blocks in `cmd_layout.cpp`, `state_store.cpp` — `escape_append` + `ESCAPE_RULES_XML_ATTR`/`HTML_TEXT` already exist (and `render_svg_inline` uses them in *some* spots). ~60 LOC.
- **Hex color parsing** re-done in `render_svg_inline.cpp:647–710` — `lib/color.h:38 color_parse_hex` does exactly this and its own comment notes it was "hand-rolled in 4+ places." ~45 LOC.
- **Local min/max/clamp sets ×8** (`display_list_replay_backdrop.cpp:6–11`, `_effects.cpp:7–12`, `_shadow.cpp:5–10`, `display_list_record_vector.cpp:11–16`, `_raster.cpp:10–14`, `_effects.cpp:11–16`, `render_composite.cpp:6`, `render_filter.cpp:34`) — `LMB_MIN/MAX/CLAMP`, `clamp_byte`, `clamp_unit` exist. ~32 LOC.
- **3×3 matrix multiply** re-implemented in `transform.hpp:49–63` (lambda) + inline expansions in `render_state.cpp`, `render_pdf.cpp`, `render_painter.cpp`, `render_svg_inline.cpp`, `rdt_vector_tvg.cpp` — `rdt_matrix_multiply` (`rdt_vector.hpp:294`) exists.
- **Value-type growable arrays** (`display_list_storage.cpp:18–24`, `paint_ir.cpp:464–465`, `browsing_session.cpp:35–37`, `webview_manager.cpp:45`, `css_animation.cpp`) — one `mem_grow_array()` helper or typed `GrowVec<T>` with `MEM_CAT_*` tagging.

### 5.5 Shared-helper extractions — C2/C4
- **4-corner transform → bbox** duplicated (`display_list_record_vector.cpp:61–95`, `display_list_replay_glyph.cpp:95–122`, `transform.hpp:127–178`, tile glyph path) — add `rdt_matrix_transform_point/rect_bounds` to `rdt_vector.hpp`. ~90 LOC.
- **Dirty-list bbox union ×3** (`display_list_replay_state.cpp:13–29`, `render_output.cpp:258–268`, `retained_display_list.cpp:493+`) — `dirty_tracker_bounding_box()`. ~45 LOC.
- **Surface-region clamp + row-memcpy backup ×3+** (`_backdrop.cpp:23–67`, `_shadow.cpp:18–48`, `_effects.cpp:39–77`, + tile equivalents) and the **identical clip-shape restore-mask loop** (`_effects.cpp:64–73` ≡ `_shadow.cpp:62–71`) — `surface_region_save()`/`surface_region_restore_masked()`. ~110 LOC.
- **`dl_default_*_clip()` + set-clipped-bounds triplicated** (`display_list_record_effects.cpp:6–50`, `_raster.cpp:5–36`, `_vector.cpp:6–9`) — shared `dl_bounds.hpp` + one `DL_UNBOUNDED_CLIP`. ~50 LOC.
- **Bilinear sampling** duplicated (`display_list_replay_glyph.cpp:173–234` vs `render_raster.cpp`/`tile_pool.cpp` blits) — `bilinear_sample_rgba()`. ~50 LOC.
- **Blend-mode math** — extend the shared `render_composite_blend_pixel` (already used by backdrop replay) to the SVG/effects compositors (`render_svg_inline.cpp:232`, `render_effects.cpp:80`).
- **Rect/point struct zoo** (C4): `Rect{x,y,w,h}` + `Bound{l,t,r,b}` (`view.hpp:340,345`), `RenderPixelBounds`, `DirtyRect`, `Point2D`, ad-hoc `int region[4]` arrays — consolidate on `Rect`/`Bound` + a named `IRect`, explicit converters.

### 5.6 `view.hpp` math templates vs `lib_math` — C1/C2
`view.hpp:36–86` defines `max/min/abs/clamp/sign/lerp`; `lib/math_utils.h:15–33` defines the same and documents the clash. Alias one into the other. Widely included — medium risk.

### 5.7 Flags for dedicated follow-up passes
- **Two SVG serialization paths:** `paint_ir_lower_svg_unchecked` (`paint_ir.cpp:1489–1900+`, with its own cap/join name maps 1246–1260 and path visitor 1307) vs `render_svg.cpp` (81 KB) / `render_svg_inline.cpp` (235 KB). Decide whether `render_svg.cpp` can retire in favor of PaintIR lowering, or at least share the path visitor + name tables.
- **`event.cpp` (444 KB) vs `event_sim.cpp` (260 KB):** the simulator appears to reconstruct hit-testing/caret/selection logic the live dispatch already implements — have `event_sim` call the same low-level entry points. Needs its own audit.

---

## 6. Lambda Core ↔ JS Runtime (`lambda/js/`)

Already unified (the model to follow): libuv event loop (`lib/uv_loop.h`, both sides), module registry (`module_registry.cpp` with `lang` param), JSON **parsing** (`js_json_parse` → `parse_json_to_item_strict`), `lib/strbuf`, `lib/base64`+`lib/hex` in crypto encode paths, `js_https.cpp` as a thin 705-LOC delegating wrapper over js_http/js_tls (the anti-duplication exemplar). Number parsing (both `strtod`-based) and math builtins (both thin libc dispatch) are fine as-is.

### 6.1 `js_double_to_string` ≡ `lambda_double_to_shortest` — C4/C2, TOP PRIORITY
`lambda/js/js_runtime_value.cpp:333–470` vs `lambda/lambda-decimal.cpp:62–150`: the *same function* — identical shortest-round-trip search (`for prec 1..21 { snprintf("%.*e"); sscanf; roundtrip check }`), identical digit extraction, identical ES §7.1.12.1 case analysis. Only NaN/Inf spellings and JS-side buffer guards differ. Both on hot paths. ~135 LOC. **Fix:** extract `lib/dtoa.c` → `dbl_to_shortest_digits(double, char[32], int* k, int* e, bool* neg)` + two thin flavor formatters. Mechanical, low risk.

### 6.2 Three hand-rolled base64 *decode* tables — C1, HIGH
`js_buffer.cpp:502–540`, `js_buffer.cpp:1630–1656` (base64url variant), `js_globals.cpp:14203–14290` (atob) — all three files `#include lib/base64.h` and use it for *encode*, but hand-roll decode with inline 256-entry tables, duplicating `base64_decode_variant` (`lib/base64.h:58`) that `js_crypto.cpp:1423` already uses correctly. ~90 LOC + 3 subtly-different padding/whitespace surfaces. Mechanical fix.

### 6.3 Static helper copy-paste across files — C3, HIGH (trivial effort)
- `static Item make_string_item(...)` redefined in **22 files** (js_querystring, js_cssom, js_string_decoder, js_https, js_fs, js_util, js_net, js_crypto, js_fetch, js_http, js_path, js_os, js_globals, js_dns, js_zlib, js_readline, js_child_process, js_buffer, js_tls, js_url_module, js_events, js_stream). ~175 LOC.
- `make_js_undefined` ×21 files; `is_callable` ×8; plus scattered `is_missing_value`, `append_bytes`. ~95 LOC.
**Fix:** promote `js_make_string()`, `js_undefined()`, `js_is_callable()` to `js_runtime.h`.

### 6.4 MIR emission layer duplicated between transpilers — C4, HIGH (structural)
`transpile-mir.cpp:434–490` (`new_reg`, `emit_insn`, `emit_label`, `emit_null_item_reg`, `emit_call_0/1/2`, `ensure_import`) vs `js_mir_calls_boxing_types.cpp:168+`/`js_mir_internal.hpp:96–204` (`jm_new_reg`, `jm_emit`, `jm_call_0..6`, `jm_ensure_import`). `emit_call_1` and `jm_call_1` are line-for-line identical except the struct type and one profiling line. ~300–500 LOC. **Fix:** a `MirEmitter` base (MIR ctx, current func, reg counter, import cache) embedded by both `MirTranspiler` and `JsMirTranspiler`; AST lowering stays separate. Medium-high effort.

### 6.5 Date civil-calendar math — C4/C2 (+ perf bug in lib)
`js_globals.cpp:1637–1644` implements the O(1) Hinnant `days_from_civil` (146097-era); `lib/datetime.c:64–86 days_since_epoch` is a **naive O(year) loop**; JS also hand-rolls `MakeDay/MakeTime/MakeDate` (1582–1610) paralleling `datetime_to_unix` (`lib/datetime.c:89`). ~60 LOC. **Fix:** move Hinnant `days_from_civil`/`civil_from_days` into `lib/datetime.c` (or `lib/calendar.h`); both consume it. Shared unit is the integer day/second math, not the struct (JS uses raw doubles).

### 6.6 Other unification candidates
- **JSON stringify** (parse is already shared): `js_globals.cpp:13345–13860` duplicates the string escaper + number path of `format-json.cpp`. The tree walk can't merge (toJSON/replacer semantics) but share `json_escape_string(StrBuf*,...)` + the §6.1 dtoa. ~60 LOC.
- **RE2 wrapper glue:** `re2_wrapper.cpp` (802 LOC) vs `js_regex_wrapper.cpp` (2862) + `js_bt_regex.cpp` (1043) — syntax translation legitimately differs, but compile+cache+options+capture-extraction lifecycle is duplicated. Extract `lib/re2_glue`. ~150–250 LOC.
- **Percent-encoding ×4:** `js_querystring.cpp:88–181`, `js_globals.cpp:14486–14560` (encodeURI*), `js_url_module.cpp`, lowering in `js_mir_expression_lowering.cpp`, plus `lib/url.c`'s own codec — one `url_percent_encode(out,in,len,keep[256])`/`decode` in `lib/url.h`; callers supply keep-sets. ~80 LOC.
- **UTF-16 surrogate math open-coded** (`js_buffer.cpp:283–284, 620–624, 950–954`, `js_globals.cpp:162, 5569–5601, 5670–5677`, js_dom/js_formdata/js_cssom/js_string_decoder) — `lib/utf.h` has `utf16_decode_pair/encode`, `utf8_encode/decode`, `utf_is_surrogate`. The *string storage model* (UTF-16-semantic JS vs UTF-8 lib) can't unify, but these per-codepoint conversions can. ~60–120 LOC.
- **`js_path` normalize/join** (`js_path.cpp:71–260`) is pure string path manipulation (distinct from `lambda/path.c`'s Url/Target abstraction); `js_require` partially re-derives it (`js_mir_entrypoints_require.cpp:508–524, 1305–1432`) — extract `lib/path_str.c`. ~100 LOC.
- **Builtin registration fragility (internal):** flat name/arity/id tables (`js_runtime_builtin_registry.cpp:150+`) + giant `switch(builtin_id)` (`js_runtime.cpp:11421+`) + `strncmp` chains in `js_mir_expression_lowering.cpp` (e.g. 2720–2721, 9735–9760) — adding a builtin means editing ≥3 places. X-macro/codegen table.
- **`util.inspect` vs `print.cpp`:** parallel Item-graph debug printers (cycle detection, depth limits) — formats genuinely differ (Node-style); architectural note only, don't force-merge.

---

## 7. Proposed New / Extended `lib/` Modules

| Module | Contents | Consumers |
|--------|----------|-----------|
| `lib/dtoa.{h,c}` | `dbl_to_shortest_digits()` + Lambda/ES flavor formatters | lambda-decimal, print.cpp, format-json/number, js_runtime_value, js JSON.stringify |
| `lib/datetime.c` (extend) | Hinnant `days_from_civil`/`civil_from_days`; fix O(year) `days_since_epoch` | lib/datetime itself, js Date |
| `lib/url.{h,c}` (extend) | `url_percent_encode/decode` with keep-set param | js_querystring, encodeURI*, js_url_module, lib/url |
| `lib/path_str.{h,c}` | normalize/join/dirname/basename/extname (POSIX+Win) | js_path, js_require |
| `lib/re2_glue` | RE2 compile+cache+options+capture extraction | re2_wrapper, js_regex_wrapper |
| `lib/escape.h` (extend) | Markdown/RST/Wiki/DOT/Mermaid/D2 `EscapeRule[]` tables | format-utils, format-graph, retire two bespoke escapers |
| `lib/utf` (extend) | utf8proc normalization wrappers from `utf_string.cpp` | lambda core, potential js use |
| `lib/` or `lambda/text/` | `html_entities` (WHATWG table + reverse map) | input, css/dom, html5 tokenizer, formatters |
| `lib/math_utils.h` (extend) | `nonneg()`; reconcile `view.hpp` clamp/sign/lerp clash | radiant layout + paint |

Radiant-internal (not lib, but new shared homes): `layout_percentages.cpp`, `glyph_raster.hpp`, `dl_bounds.hpp`, `surface_region_save/restore`, paint-op X-macro descriptor table, `rdt_matrix_transform_point/rect_bounds` in `rdt_vector.hpp`.

---

## 8. Suggested Sequencing

**Phase 0 — zero-risk deletions (hours)**
Delete `.bak` files (`layout_table.cpp.bak`, `render_svg.cpp.bak`, `render_img.cpp.bak`, `mark_editor.cpp.bak`, `runner.cpp.bak`); delete dead `input-utils` helpers (§2.5) and dead `markdown/wiki/rst_escape` (§3.6); fix the unreachable `LMD_TYPE_ARRAY` branches in `lambda-eval.cpp` (§1.7 — possible latent bug); delete the two validator TypeId→name copies (§3.4).

**Phase 1 — mechanical low-risk wins (days)**
`lib/dtoa` extraction (§6.1); base64 decode tables (§6.2); js static-helper dedup (§6.3); move 5 transpiler analysis helpers to `transpile_shared.cpp` (§1.1); `arraynum_read_item` (§1.3); `utf_string.cpp` collapse (§1.6); `createFloat` adoption + `str_rtrim`/ctype fixes in input (§2.5, 2.6); radiant lib-reuse batch (escape/color/clamp/matrix, §5.4); `is_out_of_flow`/`is_floated` canonicalization (§4.3); formatter pool-ceremony helper + datetime helper (§3.5, 3.8).

**Phase 2 — mechanical but broad migrations (1–2 weeks, needs layout re-baselining)**
Radiant box-metrics + min/max-clamp + box-sizing migration (§4.1, 4.2, 4.8); resolve the INI/Properties triple (§2.1); XML escaping onto lib (§3.3); number formatting through `format_number` (§3.5); percent-encoding + UTF-16 primitives in js (§6.6).

**Phase 3 — shared-module extractions (weeks, deliberate)**
`ElementReader::attrs()` + formatter dispatch visitor (§3.1, 3.2) + map-field iterator in core (§1.4); quoted-string/escape unification in input (§2.2, 2.3); `layout_percentages.cpp` + baseline walker + flex→layout_alignment (§4.4–4.7); glyph raster + surface-region helpers (§5.2, 5.5); `lib/calendar` + JSON-stringify escaper (§6.5, 6.6).

**Phase 4 — structural (dedicated efforts, own design pass each)**
`MirEmitter` shared base (§6.4); replay-core unification `dl_replay`/`dl_replay_tile` (§5.1); paint-op X-macro table (§5.3); scalar-type predicate canonicalization (§1.5); builtin-table codegen (§6.6); the two flagged deep audits — SVG dual path (§5.7) and `event.cpp` vs `event_sim.cpp` (§5.7).

**Testing gates per phase:** `make test-lambda-baseline` must stay 100% for Phases with lambda-engine changes; `make test-radiant-baseline` + `make layout suite=baseline` for radiant changes; node baseline (`make node-baseline`) should not regress for lambda/js changes. Phase 2's radiant migrations and §4.4's int→float spacing change may shift sub-pixel output — re-baseline deliberately, one engine at a time.
