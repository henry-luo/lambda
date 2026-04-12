# Unifying ASCII Math and LaTeX Math Parsers

## Summary

This document describes the completed unification of the ASCII math and LaTeX math input paths so both produce `MathASTNode` trees via a single tree-sitter grammar, and the two-phase simplification of the `latex_math` grammar that reduced the generated `parser.c` by **35% in size** and **18% in states**.

### Implementation Status

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 1 | Grammar simplification — merge rules (30 → 23 `_atom` alternatives) | ✅ Complete |
| Phase 2 | Add ASCII math support to grammar (+3 tokens, +1 structural rule) | ✅ Complete |
| Phase 3 | ASCII flavor in AST builder (`MathFlavor` enum, `build_word()`, etc.) | ✅ Complete |
| Phase 4 | Further grammar reduction — push commands to generic fallback (23 → 19 `_atom`) | ✅ Complete |
| Phase 5 | Validation — all baselines green (448 Lambda + 2149 Radiant + 1714 Input) | ✅ Complete |

---

## Current State

### Two Separate Math Parsers

| Aspect | ASCII Math (`input-math-ascii.cpp`) | LaTeX Math (tree-sitter + `tex_math_ast_builder.cpp`) |
|--------|--------------------------------------|-------------------------------------------------------|
| **Parser type** | Hand-written tokenizer + recursive descent | Tree-sitter grammar → CST → AST builder |
| **Output** | Lambda Elements (`<add>`, `<pow>`, `<sin>`) | `MathASTNode*` tree (ORD, FRAC, SCRIPTS…) |
| **Downstream** | Lambda data pipeline | TeX typesetter → TexNode → rendering |
| **Symbol coverage** | ~85 constants | ~250+ (Greek + SYMBOL_TABLE + BIG_OP_TABLE) |
| **Structures** | Flat binary trees only | FRAC, SQRT, SCRIPTS, DELIMITED, ARRAY, ACCENT, OVERUNDER, … |
| **Implicit multiply** | Yes (`2x` → `2 * x`) | No |
| **Environments** | None | Full matrix/cases/aligned support |
| **Entry point** | `input_ascii_math()` | `parse_math_string_to_ast()` |

### Generated Parser Metrics (`parser.c`)

| Metric | Original | After Phase 1+2 | After Phase 4 | Reduction |
|--------|---------|-----------------|---------------|----------|
| File size | 1.8 MB | 2.38 MB* | **1.56 MB** | **-35%** vs Phase 1+2 |
| Total lines | 68,300 | 81,535* | **54,958** | **-33%** vs Phase 1+2 |
| STATE_COUNT | 1,340 | 1,349* | **1,100** | **-18%** vs Phase 1+2 |
| LARGE_STATE_COUNT | — | 241 | **219** | -9% |
| SYMBOL_COUNT | 135 | 116 | **99** | -15% |
| TOKEN_COUNT | — | 68 | **58** | -15% |
| FIELD_COUNT | 40 | 33 | **21** | **-36%** |
| `_atom` alternatives | 30 | 26† | **19** | **-37%** vs original |

\* Phase 1+2 intermediate values include ASCII math additions (word, ascii_operator, quoted_text, paren_script) which partially offset the Phase 1 structural merges.

† 23 from Phase 1 merges + 3 ASCII math additions = 26.

For reference, the LaTeX text grammar (`tree-sitter-latex`) has: 525 KB, 19K lines, 489 states, 12 large states.

---

## Part 1: Grammar Simplification

### Principle

The grammar should capture **structure** (fractions, scripts, groups, environments), not **semantics** (which command is which operator, what unit a dimension is). Move semantic classification to the AST builder where table lookups are cheap.

### 1.1 Merge `fraction` + `binomial` + `genfrac` → `frac_like`

All three are "command + N group args + 2 frac args". The AST builder already distinguishes them by reading the command text.

**Before** (3 rules, 3 `_atom` alternatives):
```js
fraction: $ => seq(field('cmd', $._frac_cmd), field('numer', $._frac_arg), field('denom', $._frac_arg)),
binomial: $ => seq(field('cmd', $._binom_cmd), field('top', $._frac_arg), field('bottom', $._frac_arg)),
genfrac:  $ => seq('\\genfrac', ...4 groups..., field('numer', $._frac_arg), field('denom', $._frac_arg)),
```

**After** (1 rule, 1 `_atom` alternative):
```js
_frac_like_cmd: $ => token(/\\([dtc]?(frac|binom)|genfrac)/),

frac_like: $ => seq(
  field('cmd', $._frac_like_cmd),
  repeat(field('prefix', $.group)),    // 0 for frac/binom, 4 for genfrac
  field('first', $._frac_arg),
  field('second', $._frac_arg),
),
```

AST builder reads `cmd` text: `\frac` → FRAC, `\binom` → FRAC(thickness=0, delims=`()`), `\genfrac` → extract prefix groups for delims/thickness/style.

**Estimated savings:** ~50 states.

### 1.2 Merge all spacing → `spacing_command`

Currently three separate rules with a 14-way `dimension_unit` choice:

**Before** (3 rules, 3 `_atom` alternatives):
```js
space_command:  $ => $._space_cmd,                         // \, \; \quad etc.
hspace_command: $ => seq($._hspace_cmd, '{', sign?, number, dimension_unit, '}'),
skip_command:   $ => seq($._skip_cmd, sign?, number, dimension_unit),
dimension_unit: $ => choice('pt','mm','cm','in','ex','em','bp','pc','dd','cc','sp','mu','\\fill'),
```

**After** (1 rule, 1 `_atom` alternative):
```js
_spacing_cmd: $ => token(/\\([,:;!]|qquad|quad|hspace\*?|hskip|kern|mskip|mkern)/),

spacing_command: $ => prec.right(seq(
  field('cmd', $._spacing_cmd),
  optional(field('arg', choice($.group, $._dim_value))),
)),

// Single token for bare dimensions: "3.5em", "-10pt", "2\fill"
_dim_value: $ => token(/[+-]?\d+\.?\d*(\\fill|[a-z]{2})/),
```

`\quad` → no arg. `\hspace{3em}` → group arg (AST builder parses "3em" from text). `\kern3pt` → `_dim_value` arg. Eliminates the `dimension_unit` rule entirely.

**Estimated savings:** ~100 states (biggest single win).

### 1.3 Merge `box_command` + `phantom_command` → `boxlike_command`

Identical structure: `cmd + optional(brack_group) + group`.

**After:**
```js
_boxlike_cmd: $ => token(/\\(mathllap|mathrlap|mathclap|boxed|bbox|fbox|llap|rlap|clap|hphantom|vphantom|phantom|smash)/),

boxlike_command: $ => seq(
  field('cmd', $._boxlike_cmd),
  optional(field('options', $.brack_group)),
  field('content', $.group),
),
```

**Estimated savings:** ~25 states.

### 1.4 Merge `overunder_command` + `extensible_arrow` → `annotated_command`

Both attach annotations (groups / brack_groups) to a command.

**After:**
```js
_annotated_cmd: $ => token(/\\(xLeftrightarrow|xleftrightarrow|xhookrightarrow|xhookleftarrow|xRightarrow|xrightarrow|xLeftarrow|xleftarrow|xmapsto|overset|underset|stackrel)/),

annotated_command: $ => prec.right(seq(
  field('cmd', $._annotated_cmd),
  optional(field('opt_arg', $.brack_group)),
  field('first', $.group),
  optional(field('second', $.group)),
)),
```

`\overset{a}{b}` → first=a, second=b. `\xrightarrow[below]{above}` → opt_arg=below, first=above. AST builder dispatches on command text.

**Estimated savings:** ~25 states.

### 1.5 Merge `text_command` + `style_command` → `textstyle_command`

**After:**
```js
_textstyle_cmd: $ => token(/\\(scriptscriptstyle|operatorname|displaystyle|mathnormal|scriptstyle|textstyle|mathfrak|mathscr|mathcal|mathrm|mathit|mathbf|mathsf|mathtt|mathbb|textrm|textit|textbf|textsf|texttt|text|mbox|hbox)/),

textstyle_command: $ => prec.right(seq(
  field('cmd', $._textstyle_cmd),
  optional(field('arg', choice($.text_group, $.group))),
)),
```

AST builder checks whether the command is `\text*` (text-mode content) or `\math*` (style wrapper).

**Estimated savings:** ~25 states.

### 1.6 Collapse `delimiter` char literals → regex token

**Before** (11 alternatives):
```js
delimiter: $ => choice('(', ')', '[', ']', '\\{', '\\}', '|', '\\|', '.', $._delimiter_cmd, $._updown_arrow_cmd),
```

**After** (3 alternatives):
```js
_delim_char: $ => token(/[()[\]|.]|\\[{}|]/),  // 9 ASCII delimiters → 1 regex

delimiter: $ => choice($._delim_char, $._delimiter_cmd, $._updown_arrow_cmd),
```

**Estimated savings:** ~20 states.

### 1.7 Simplify `env_columns`

**Before:**
```js
env_columns: $ => seq('{', /[lcr|@{pmb\d.]+/, '}'),
```

**After:**
```js
env_columns: $ => seq('{', /[^{}]+/, '}'),  // capture raw text; AST builder validates
```

**Estimated savings:** ~5 states.

### Summary of Simplifications

| Change | `_atom` Alternatives | Estimated States Saved |
|--------|:-------------------:|:----------------------:|
| frac/binom/genfrac → `frac_like` | 30 → 28 | ~50 |
| spacing unification | 28 → 26 | ~100 |
| box/phantom merge | 26 → 25 | ~25 |
| overunder/arrow merge | 25 → 24 | ~25 |
| text/style merge | 24 → 23 | ~25 |
| delimiter char collapse | — | ~20 |
| env_columns simplify | — | ~5 |
| **Total** | **30 → 23** | **~250** |

Projected: **1,340 → ~1,090 states**, **68K → ~55K lines** in `parser.c` (~20% reduction).

### Simplified `_atom` Rule (After Phase 1)

This was the intermediate state before further Phase 4 reductions:

```js
_atom: $ => choice(
  $.symbol,              // [a-zA-Z@]
  $.number,              // digits
  $.symbol_command,      // \infty, \exists, \cdots, ...
  $.operator,            // +, -, *, /, \pm, \times, ...
  $.relation,            // =, <, >, \leq, \rightarrow, ...
  $.punctuation,         // , ; : . ( ) [ ] | ...
  $.group,               // { ... }
  $.frac_like,           // \frac, \binom, \genfrac     ← merged
  $.radical,             // \sqrt
  $.delimiter_group,     // \left ... \right
  $.sized_delimiter,     // \big, \Big, ...
  $.annotated_command,   // \overset, \xrightarrow       ← merged
  $.accent,              // \hat, \bar, \overline, ...
  $.boxlike_command,     // \fbox, \phantom, \smash       ← merged
  $.color_command,       // \color, \textcolor
  $.rule_command,        // \rule
  $.big_operator,        // \sum, \int, \lim, ...
  $.mathop_command,      // \mathop{...}
  $.matrix_command,      // \matrix{...}
  $.environment,         // \begin{...} ... \end{...}
  $.textstyle_command,   // \text, \mathrm, \displaystyle ← merged
  $.spacing_command,     // \, \quad \hspace \kern        ← merged
  $.command,             // fallback
),
```

23 alternatives (down from 30). Each eliminated alternative removes one branch from the LR parse table at every state where `_atom` is viable.

---

## Part 1.5: Phase 4 — Further Grammar Reduction (Command Fallback Strategy)

### Principle

Many command-based atom types (`symbol_command`, `big_operator`, `annotated_command`, `boxlike_command`, `color_command`, `rule_command`, `mathop_command`) share the same structural pattern: a regex-matched command token followed by group/brack_group arguments. The generic `command` rule already handles this pattern. By removing these dedicated rules and letting their commands fall through to `command`, the grammar gets smaller while the AST builder dispatches by command name.

### What Was Removed

**8 regex command tokens eliminated:**

| Token | Commands | Now Handled By |
|-------|----------|---------------|
| `_symbol_cmd` | `\infty`, `\exists`, `\forall`, `\cdots`, … | `command_name` → `command` |
| `_operator_cmd` | `\pm`, `\mp`, `\times`, `\cdot`, … | `command_name` → `command` |
| `_relation_cmd` | `\leq`, `\geq`, `\equiv`, `\subset`, … | `command_name` → `command` |
| `_arrow_cmd` | `\rightarrow`, `\Rightarrow`, `\mapsto`, … | `command_name` → `command` |
| `_big_operator_cmd` | `\sum`, `\prod`, `\int`, `\lim`, … | `command_name` → `command` |
| `_annotated_cmd` | `\overset`, `\xrightarrow`, `\stackrel`, … | `command_name` → `command` |
| `_boxlike_cmd` | `\phantom`, `\fbox`, `\boxed`, `\smash`, … | `command_name` → `command` |
| `_color_cmd` | `\textcolor`, `\color`, `\colorbox` | `command_name` → `command` |

**7 atom alternatives eliminated:**

| Rule Removed | Reason |
|-------------|--------|
| `symbol_command` | Leaf command — `build_command()` handles via table lookups |
| `big_operator` | Duplicated sub/sup logic already in `subsup`; `build_command()` + `build_subsup()` handle limits |
| `annotated_command` | `build_command()` dispatches by name to `build_annotated_command()` |
| `boxlike_command` | `build_command()` dispatches by name to `build_boxlike_command()` |
| `color_command` | `build_command()` dispatches by name to `build_color_command()` |
| `rule_command` | `build_command()` dispatches by name to `build_rule_command()` |
| `mathop_command` | `build_command()` dispatches by name to `build_mathop_command()` |

**`operator` and `relation` simplified:**
- `operator`: was `choice('+', '-', '*', '/', _operator_cmd)` → now `choice('+', '-', '*', '/')`
- `relation`: was `choice('=', '<', '>', '!', _relation_cmd, _arrow_cmd, _updown_arrow_cmd)` → now `choice('=', '<', '>', '!', _updown_arrow_cmd)`
- `_updown_arrow_cmd` kept in `relation` because it's shared with the `delimiter` rule

### Final `_atom` Rule (19 Alternatives)

```js
_atom: $ => choice(
  $.symbol,              // [a-zA-Z@]
  $.word,                // bare multi-letter identifier (ASCII math)
  $.number,              // digits
  $.operator,            // +, -, *, /
  $.relation,            // =, <, >, !, \uparrow, \downarrow
  $.ascii_operator,      // <=, ->, xx, +-, etc. (ASCII math)
  $.punctuation,         // , ; : . ( ) [ ] | ...
  $.quoted_text,         // "quoted text" (ASCII math)
  $.group,               // { ... }
  $.frac_like,           // \frac, \binom, \genfrac
  $.radical,             // \sqrt
  $.delimiter_group,     // \left ... \right
  $.sized_delimiter,     // \big, \Big, ...
  $.accent,              // \hat, \bar, \overline, ...
  $.matrix_command,      // \matrix{...}
  $.environment,         // \begin{...} ... \end{...}
  $.textstyle_command,   // \text, \mathrm, \displaystyle
  $.spacing_command,     // \, \quad \hspace \kern
  $.command,             // fallback (\sum, \overset, \phantom, \color, etc.)
),
```

19 alternatives (down from 30 original, 23 after Phase 1). The generic `command` now handles ~100+ LaTeX commands that previously needed dedicated grammar rules.

### AST Builder: Command-Level Dispatch

The enhanced `build_command()` function extracts the command name from the `name` field and dispatches structural commands before falling through to table-based classification:

```cpp
MathASTNode* MathASTBuilder::build_command(TSNode node) {
    // Get command name from 'name' field
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    // ... extract cmd, cmd_len ...

    // --- Structural command dispatch ---
    if (is_annotated_cmd(cmd, cmd_len)) return build_annotated_command(node);
    if (is_boxlike_cmd(cmd, cmd_len))   return build_boxlike_command(node);
    if (is_color_cmd(cmd, cmd_len))     return build_color_command(node);
    if (cmd is "rule")                  return build_rule_command(node);
    if (cmd is "mathop")                return build_mathop_command(node);

    // --- Leaf command: table lookups ---
    if (lookup_greek(cmd))    → ORD atom
    if (lookup_symbol(cmd))   → BIN/REL/PUNCT/ORD atom
    if (lookup_big_op(cmd))   → OP atom (with FLAG_LIMITS)

    // --- Unknown command → ORD ---
    return make_math_ord(arena, 0, cmd);
}
```

The structural handler functions (`build_annotated_command`, `build_boxlike_command`, etc.) now use `collect_cmd_args()` to extract positional arguments from the generic command node, instead of accessing named fields from their former dedicated grammar rules.

### Big Operator Limits Handling

With `big_operator` removed from the grammar, `\sum_{i=0}^{n}` now parses as `subsup(base: command(\sum), sub: ..., sup: ...)`. The `build_subsup()` function detects FLAG_LIMITS on the base node:

```cpp
MathASTNode* build_subsup(TSNode node) {
    // ... build base, sub, sup ...

    // Check if base has FLAG_LIMITS (e.g., \sum from big operator table)
    bool base_wants_limits = (base->flags & FLAG_LIMITS) && !has_nolimits;
    if (base_wants_limits || has_limits) {
        return make_math_overunder(arena, base, super, sub, base->atom.command);
    }
    return make_math_scripts(arena, base, super, sub);
}
```

This correctly produces OVERUNDER for `\sum_{i=0}^{n}` and SCRIPTS for `\int_0^1`.

### Remaining Regex Tokens

After Phase 4, these regex tokens are still needed (they serve structural purposes the generic `command` cannot replicate):

| Token | Purpose |
|-------|---------|
| `_delimiter_cmd` | Shared between `delimiter` (in `\left`/`\right`) and `sized_delimiter` |
| `_accent_cmd` | `accent` rule needs to parse `\hat x` without braces |
| `_sized_delim_cmd` | `sized_delimiter` needs `\big(` with optional following delimiter |
| `_textstyle_cmd` | `textstyle_command` needs optional `text_group` (not plain `group`) |
| `_frac_like_cmd` | `frac_like` needs `repeat1(field('arg', ...))` |
| `_matrix_cmd` | `matrix_command` uses bare `{` ... `}` (not `group`) |
| `_spacing_cmd` | `spacing_command` takes optional `_dim_value` (not just groups) |
| `_updown_arrow_cmd` | Shared between `relation` and `delimiter` |
| `_infix_frac_cmd` | `infix_frac` needs field-based numer/denom parsing |
| `_limits_mod` | `limits_modifier` in `subsup` |
| `_ascii_multi_op` | Multi-char ASCII operators |
| `_env_name_token` | Environment name validation |

---

## Part 2: Unifying ASCII Math into the LaTeX Math Grammar

### Key Insight: ASCII Math is Nearly a Subset of LaTeX Math

Comparing every ASCII math construct against the LaTeX grammar reveals that **most ASCII math syntax parses correctly under the existing LaTeX grammar already** — the difference is purely in interpretation, not structure.

| ASCII Math | LaTeX Equivalent | Grammar Behavior |
|------------|------------------|------------------|
| `x + y` | `x + y` | **Identical** — symbols + operators |
| `x^2` | `x^2` | **Identical** — subsup rule |
| `x_i^n` | `x_i^n` | **Identical** — subsup rule |
| `alpha` | `\alpha` | **Different** — bare word vs `\`-command |
| `sin(x)` | `\sin(x)` | **Different** — bare word vs `\`-command |
| `sum_(i=0)^n` | `\sum_{i=0}^{n}` | **Different** — bare word + `()` vs `\`-command + `{}` |
| `sqrt(x)` | `\sqrt{x}` | **Different** — bare word + `()` vs `\`-command + `{}` |
| `2x` (implicit mul) | `2x` | **Already works** — `number symbol` sequence |
| `a/b` (infix frac) | `a/b` | **Already works** — `symbol operator symbol` |
| `<=`, `>=`, `!=` | `\leq`, `\geq`, `\neq` | **Different** — multi-char tokens vs `\`-commands |
| `->`, `<->` | `\to`, `\leftrightarrow` | **Different** — multi-char tokens vs `\`-commands |
| `xx`, `-:`, `+-` | `\times`, `\div`, `\pm` | **Different** — multi-char tokens vs `\`-commands |
| `(a+b)` | `(a+b)` | **Identical** — punctuation atoms |
| `{a+b}` | `{a+b}` | **Identical** — group |
| `|x|` | `\|x\|` | **Close** — single `|` is already punctuation |
| `"text"` | `\text{text}` | **Different** — quoted string vs `\`-command |

**The only differences are:**
1. **Bare multi-letter words** (`sin`, `alpha`, `sqrt`) — LaTeX uses `\sin`, `\alpha`, `\sqrt`
2. **Multi-char ASCII operators** (`<=`, `->`, `xx`, `+-`) — LaTeX uses `\leq`, `\to`, `\times`, `\pm`
3. **Quoted text** (`"text"`) — LaTeX uses `\text{text}`

### Strategy: One Grammar, Minimal Extensions, AST Builder Interprets

Rather than a separate grammar, extend the LaTeX math grammar with just **two additions** — a `word` token and multi-char ASCII operator tokens — and let the AST builder interpret based on flavor.

```
Math string ──→ tree-sitter-math ──→ CST ──→ MathASTBuilder (flavor) ──→ MathASTNode*
                                                                              │
                                                                              ▼
                                                                       TeX Typesetter
```

The grammar stays close to a **tokenizer** — it classifies tokens and captures structure (groups, scripts, environments). The AST builder handles all semantic decisions per-flavor.

### 2.1 Grammar Changes: Just Two Additions

#### Addition 1: `word` token — bare multi-letter identifiers

```js
// New: multi-letter bare word (no backslash) — lower precedence than commands
word: $ => token(prec(-2, /[a-zA-Z]{2,}/)),
```

This captures `sin`, `alpha`, `sqrt`, `sum`, `lim`, etc. as a single token. It has lower precedence than `command_name` (`\xxx`) so LaTeX commands always win.

In the grammar, add `$.word` to the `_atom` choices:

```js
_atom: $ => choice(
  $.symbol,          // single letter: [a-zA-Z@]
  $.word,            // multi-letter bare word (NEW)
  $.number,
  // ... rest unchanged ...
),
```

**Impact on grammar:** +1 `_atom` alternative, +1 leaf token. Negligible state cost (~10 states) since it's a simple leaf with no structural rules.

**How the AST builder uses it:**
- **LaTeX flavor**: `word` "sin" → sequence of ORD atoms `s`, `i`, `n` (standard LaTeX behavior where `sin` without `\` is three italic variables)
- **ASCII flavor**: `word` "sin" → lookup in function/symbol table → `OP` atom for sin; `word` "alpha" → `ORD` atom (α)

#### Addition 2: Multi-char ASCII operators/relations

```js
// New: multi-char ASCII operators that don't conflict with LaTeX
// (LaTeX never uses <=, ->, xx as raw tokens — it uses \leq, \to, \times)
_ascii_multi_op: $ => token(prec(2, /<=|>=|!=|-=|~=|~~|->|<->|<=>|\|->|=>|<-|xx|-:|\+-|-\+|\*\*/)),
```

Add to `operator` and `relation`:

```js
operator: $ => choice(
  '+', '-', '*', '/',
  $._operator_cmd,
  $._ascii_multi_op,   // NEW — captures xx, -:, +-, etc.
),

relation: $ => choice(
  '=', '<', '>', '!',
  $._relation_cmd,
  $._arrow_cmd,
  $._updown_arrow_cmd,
  $._ascii_multi_op,   // NEW — captures <=, >=, ->, etc.
),
```

Wait — `<=` conflicts: `<` is already a relation, `=` is already a relation, so `<=` in LaTeX would parse as `< =` (two separate relations). Using `prec(2)` on the multi-char token means `<=` is consumed as one token when present, which is correct because:
- In LaTeX input, `<=` almost never appears (you'd write `\leq`)
- In ASCII input, `<=` should be one token (≤)

But to avoid breaking LaTeX parsing of `< =` edge cases, make the multi-char tokens a **separate rule** rather than merging into `operator`/`relation`:

```js
// ASCII multi-char operator — its own _atom alternative
ascii_operator: $ => $._ascii_multi_op,
```

Add `$.ascii_operator` to `_atom`. The AST builder maps it to the appropriate atom type (BIN or REL) based on the actual text.

**Impact on grammar:** +1 `_atom` alternative, +1 regex token. ~10–15 states.

#### Addition 3 (optional): Quoted text

```js
// Quoted text for ASCII math: "some text"
quoted_text: $ => seq('"', /[^"]*/, '"'),
```

Add to `_atom`. AST builder maps to `TEXT` node.

**Impact:** +1 alternative, ~5 states.

### 2.2 What This Looks Like in Practice

The grammar doesn't change its name — it's still `latex_math` (or rename to just `math`). It:
- Continues to parse all LaTeX math perfectly (commands, environments, etc.)
- Now also tokenizes ASCII math input (bare words, multi-char operators, quoted text)
- Produces a CST that contains both LaTeX-style and ASCII-style nodes

**Example: ASCII input `sum_(i=0)^n x_i^2`**

Under the unified grammar, this parses as:
```
(math
  (subsup
    base: (word)           ;; "sum" — AST builder: ASCII→OP, LaTeX→ORD×3
    (word)                 ;; "_" consumed by subsup
    sub: (group            ;; "(i=0)" parsed as punctuation-wrapped expressions
      (symbol) (relation) (number))
    sup: (symbol))         ;; "n"
  (subsup
    base: (symbol)         ;; "x"
    sub: (symbol)          ;; "i"
    sup: (number)))        ;; "2"
```

Wait — `(i=0)` uses parentheses, not braces. In the current grammar, `(` is punctuation, not a grouping construct for script args. The `_script_arg` rule expects `group` (braces), `symbol`, `number`, or `command`.

**This is where the grammar's tokenizer-like approach shines**: the script arg after `_` in `sum_(i=0)^n` would NOT match `_script_arg` because `(` isn't a valid script arg. Instead, it parses as:
- `sum` → `word` atom
- `_` → subsup trigger on preceding atom
- fails to match `_script_arg` with `(` → subsup doesn't complete → falls back to `word` as plain atom

**Solution**: extend `_script_arg` to include parenthesized groups:

```js
_script_arg: $ => choice(
  $.group,           // {…}
  $.paren_script,    // (…) — NEW, for ASCII-style bounds
  $.symbol,
  $.number,
  $.word,            // NEW — bare words in script position
  $.command,
),

// Parenthesized script argument (for ASCII math: sum_(i=0))
paren_script: $ => seq('(', repeat($._expression), ')'),
```

**But wait** — adding `paren_script` also works for LaTeX since `\sum_{(i=0)}` is unusual but valid in LaTeX (though typically you'd use `\sum_{i=0}`). This is safe.

### 2.3 Full Unified Grammar Diff (Minimal Changes)

Against the simplified grammar from Part 1, only these additions are needed:

```diff
 // Atoms
 _atom: $ => choice(
   $.symbol,
+  $.word,              // bare multi-letter identifiers (ASCII math)
   $.number,
   $.symbol_command,
   $.operator,
   $.relation,
+  $.ascii_operator,    // <=, ->, xx, +-, etc. (ASCII math)
   $.punctuation,
+  $.quoted_text,       // "quoted text" (ASCII math)
   $.group,
   // ... rest of _atom unchanged ...
 ),

+// Multi-letter bare word token
+word: $ => token(prec(-2, /[a-zA-Z]{2,}/)),

+// Multi-character ASCII operators/relations
+_ascii_multi_op: $ => token(prec(2, /<=|>=|!=|-=|~=|~~|->|<->|<=>|\|->|=>|<-|xx|-:|\+-|-\+|\*\*/)),
+ascii_operator: $ => $._ascii_multi_op,

+// Quoted text (ASCII math)
+quoted_text: $ => seq('"', /[^"]*/, '"'),

 // Extend _script_arg to accept parenthesized groups and words
 _script_arg: $ => choice(
   $.group,
+  $.paren_script,
   $.symbol,
   $.number,
+  $.word,
   $.command,
 ),

+// Parenthesized script argument
+paren_script: $ => seq('(', repeat($._expression), ')'),
```

**Total additions: 6 new rules/tokens, +3 `_atom` alternatives** (23+3 = 26 after Phase 1+2, later reduced to 19 by Phase 4).

**Actual `parser.c` result after Phase 1+2:** 2.38 MB, 81,535 lines, 1,349 states. The ASCII math additions partially offset Phase 1 savings: the grammar had more alternatives (26) before Phase 4 reduced it to 19.

**After Phase 4:** 1.56 MB, 54,958 lines, 1,100 states — a net **35% size reduction** from the Phase 1+2 intermediate.

### 2.4 AST Builder: Flavor-Aware Interpretation

The `MathASTBuilder` takes a `MathFlavor` parameter:

```cpp
enum MathFlavor { MATH_FLAVOR_LATEX, MATH_FLAVOR_ASCII };

MathASTNode* parse_math_to_ast(const char* src, int len, Arena* arena, MathFlavor flavor) {
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_latex_math());  // same grammar!
    TSTree* tree = ts_parser_parse_string(parser, nullptr, src, len);
    TSNode root = ts_tree_root_node(tree);

    MathASTBuilder builder(src, len, arena, flavor);
    MathASTNode* ast = builder.build(root);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return ast;
}
```

**Flavor affects only these dispatch cases:**

| CST Node | LaTeX Interpretation | ASCII Interpretation |
|----------|---------------------|---------------------|
| `word` "sin" | ROW of ORD atoms: `s` `i` `n` (italic) | OP atom (sin function) |
| `word` "alpha" | ROW of ORD atoms: `a` `l` `p` `h` `a` | ORD atom (α, U+03B1) |
| `word` "sqrt" | ROW of ORD atoms: `s` `q` `r` `t` | recognized by builder as unary function |
| `word` "lim" | ROW of ORD atoms: `l` `i` `m` | OP atom (lim, with limits) |
| `ascii_operator` "<=", "->" | *(shouldn't appear in LaTeX)* | REL atom (≤), REL atom (→) |
| `ascii_operator` "xx", "+-" | *(shouldn't appear in LaTeX)* | BIN atom (×), BIN atom (±) |
| `quoted_text` "hello" | *(shouldn't appear in LaTeX)* | TEXT atom |
| `paren_script` | DELIMITED group | script argument (bounds) |

The builder's `build_word()` method:

```cpp
MathASTNode* build_word(TSNode node) {
    const char* text = read_node_text(node);
    int len = ts_node_end_byte(node) - ts_node_start_byte(node);

    if (flavor == MATH_FLAVOR_ASCII) {
        // Lookup in shared tables
        if (auto* greek = lookup_greek(text, len))
            return make_math_ord(arena, greek->codepoint, text);
        if (auto* func = lookup_function(text, len))
            return make_math_op(arena, func->codepoint, text);
        if (auto* sym = lookup_symbol(text, len))
            return make_atom(arena, sym->atom_type, sym->codepoint, text);
        // Unknown multi-letter word → TEXT (like \text{word})
        return make_math_text(arena, text, len);
    } else {
        // LaTeX: bare multi-letter word = sequence of italic variables
        MathASTNode* row = make_math_row(arena);
        for (int i = 0; i < len; i++) {
            auto* ord = make_math_ord(arena, text[i], nullptr);
            append_to_row(row, ord);
        }
        return row;
    }
}
```

### 2.5 Handling ASCII Math Functions: `sqrt(x)`, `sum_(i=0)^n`

When `word` "sqrt" is followed by a `paren_script` in the CST, the AST builder recognizes this pattern:

```cpp
MathASTNode* build_subsup_or_funcall(TSNode node) {
    // Check if base is a word that's a known function in ASCII mode
    TSNode base = ts_node_child_by_field_name(node, "base");
    if (flavor == MATH_FLAVOR_ASCII && is_word_node(base)) {
        const char* name = read_node_text(base);
        if (is_sqrt_function(name)) {
            // word "sqrt" + paren_script → SQRT node
            return build_ascii_sqrt(node);
        }
        if (is_big_operator(name)) {
            // word "sum" + sub/sup → OVERUNDER or SCRIPTS
            return build_ascii_big_op(node);
        }
    }
    return build_subsup(node);  // default: standard subscript/superscript
}
```

This means the grammar stays dumb (just tokenizes), and the AST builder carries all the intelligence for both flavors.

### 2.6 Implicit Multiplication

In ASCII math, `2x` means `2 × x`. In the current grammar, `2x` already parses as `number symbol` — two separate atoms in a `math` row. The AST builder can detect adjacency:

```cpp
// In build_math() — when processing a sequence of children
if (flavor == MATH_FLAVOR_ASCII) {
    // Insert implicit multiplication between adjacent ORD/number atoms
    // that have no operator between them
    insert_implicit_mul_nodes(row);
}
```

**No grammar change needed.** Adjacent atoms are already separate nodes in the CST. The AST builder inserts `BIN` (invisible times) nodes between them when appropriate in ASCII mode.

### 2.7 Why One Grammar Beats Two

| Concern | Two Grammars | One Grammar |
|---------|-------------|-------------|
| Build system | 2 parsers to build, 2 `parser.c` files (~55K + ~10K = ~65K lines) | 1 parser (~57K lines) |
| AST builder | 2 separate builders (~3300 + ~600 = ~3900 lines) | 1 builder with ~50 lines of flavor branching |
| Maintenance | Duplicate logic for subsup, groups, environments | Shared rules, single source of truth |
| Feature parity | Must manually keep in sync | Automatic — same grammar rules |
| Testing | 2 test suites for parsing | 1 parse test suite + flavor-specific AST tests |
| Parser size total | ~65K lines | ~57K lines |
| New code needed | ~750 lines (grammar + builder) | ~100 lines (3 tokens + builder flavor branches) |

**The one-grammar approach is strictly better**: less code, less maintenance, smaller total parser, automatic structural feature sharing.

### 2.8 Retire `input-math-ascii.cpp`

The current hand-written ASCII math parser (810 lines, recursive descent) is replaced by:
1. The unified grammar (parses ASCII math as-is)
2. ~50 lines of ASCII-flavor branches in `MathASTBuilder`

**Migration path:**
1. Add 3 tokens to grammar (`word`, `ascii_operator`, `quoted_text`) + extend `_script_arg`
2. Add `MathFlavor` parameter to `MathASTBuilder`
3. Implement `build_word()` with ASCII lookup tables
4. Add implicit multiplication insertion in `build_math()`
5. Run existing ASCII math tests against new path
6. Remove `input-math-ascii.cpp` once parity confirmed

### 2.9 Entry Point Unification

```cpp
Item input_math(Input* input, const char* src, const char* flavor_str) {
    MathFlavor flavor = MATH_FLAVOR_LATEX;
    if (str_eq(flavor_str, "ascii") || str_eq(flavor_str, "asciimath"))
        flavor = MATH_FLAVOR_ASCII;

    MathASTNode* ast = parse_math_to_ast(src, strlen(src), &input->arena, flavor);
    return math_ast_to_item(input, ast);
}
```

Both flavors share the same grammar, same parser, same AST builder (with minimal branching), same typesetter → same rendering quality.

---

## Part 3: AST Builder Changes

Two categories of changes: (A) grammar simplification merges from Part 1, and (B) ASCII flavor support from Part 2.

### 3.1 Add `MathFlavor` to Builder

```cpp
enum MathFlavor { MATH_FLAVOR_LATEX, MATH_FLAVOR_ASCII };

class MathASTBuilder {
    MathFlavor flavor;
    // ...existing fields...
public:
    MathASTBuilder(const char* src, int len, Arena* arena, MathFlavor flavor)
        : src(src), len(len), arena(arena), flavor(flavor) {}
};
```

### 3.2 New: `build_word()` — The Key ASCII/LaTeX Branch Point

```cpp
MathASTNode* build_word(TSNode node) {
    const char* text = src + ts_node_start_byte(node);
    int len = ts_node_end_byte(node) - ts_node_start_byte(node);

    if (flavor == MATH_FLAVOR_ASCII) {
        // Table-driven lookup: Greek → ORD, function → OP, symbol → typed atom
        if (auto* entry = lookup_greek(text, len))
            return make_math_ord(arena, entry->codepoint, text);
        if (auto* entry = lookup_big_op(text, len))
            return make_math_op(arena, entry->codepoint, text);
        if (auto* entry = lookup_function(text, len))
            return make_math_op(arena, entry->codepoint, text);
        if (auto* entry = lookup_symbol(text, len))
            return make_atom(arena, entry->atom_type, entry->codepoint, text);
        // Unknown word → text
        return make_math_text(arena, text, len);
    } else {
        // LaTeX mode: bare multi-letter word = italic variable sequence (s,i,n not sin)
        MathASTNode* row = make_math_row(arena);
        for (int i = 0; i < len; i++) {
            append_to_row(row, make_math_ord(arena, text[i], nullptr));
        }
        return row;
    }
}
```

### 3.3 New: `build_ascii_operator()`

```cpp
MathASTNode* build_ascii_operator(TSNode node) {
    const char* text = src + ts_node_start_byte(node);
    int len = ts_node_end_byte(node) - ts_node_start_byte(node);

    // Table mapping: "<=" → ≤ (REL), "->" → → (REL), "xx" → × (BIN), "+-" → ± (BIN)
    static const struct { const char* ascii; int cp; MathNodeType type; } ASCII_OP_TABLE[] = {
        {"<=", 0x2264, REL}, {">=", 0x2265, REL}, {"!=", 0x2260, REL},
        {"-=", 0x2261, REL}, {"~=", 0x2245, REL}, {"~~", 0x2248, REL},
        {"->", 0x2192, REL}, {"<-", 0x2190, REL}, {"<->", 0x2194, REL},
        {"=>", 0x21D2, REL}, {"<=>", 0x21D4, REL}, {"|->", 0x21A6, REL},
        {"xx", 0x00D7, BIN}, {"-:", 0x00F7, BIN}, {"+-", 0x00B1, BIN},
        {"-+", 0x2213, BIN}, {"**", 0x2217, BIN},
        {nullptr, 0, ORD}
    };
    for (auto* e = ASCII_OP_TABLE; e->ascii; e++) {
        if (strncmp(text, e->ascii, len) == 0 && strlen(e->ascii) == (size_t)len)
            return make_atom(arena, e->type, e->cp, e->ascii);
    }
    return make_math_ord(arena, '?', text); // fallback
}
```

### 3.4 Implicit Multiplication Insertion

In `build_math()`, after collecting children into a ROW:

```cpp
if (flavor == MATH_FLAVOR_ASCII) {
    // Insert invisible-times between adjacent atoms where implicit mul applies:
    // ORD·ORD, NUMBER·ORD, CLOSE·OPEN, ORD·OPEN, CLOSE·ORD
    insert_implicit_mul(row);
}
```

### 3.5 Updated `build_frac_like` (from Part 1 merge)

```cpp
MathASTNode* build_frac_like(TSNode node) {
    const char* cmd = read_field_text(node, "cmd");
    if (str_contains(cmd, "genfrac")) {
        return build_genfrac_from_fields(node);
    } else if (str_contains(cmd, "binom")) {
        auto* frac = make_math_frac(arena);
        frac->frac_thickness = 0;
        frac->frac_left_delim = '(';
        frac->frac_right_delim = ')';
        frac->above = build_field(node, "first");
        frac->below = build_field(node, "second");
        return frac;
    } else {
        auto* frac = make_math_frac(arena);
        frac->above = build_field(node, "first");
        frac->below = build_field(node, "second");
        return frac;
    }
}
```

### 3.6 Updated `build_spacing` (from Part 1 merge)

```cpp
MathASTNode* build_spacing(TSNode node) {
    const char* cmd = read_field_text(node, "cmd");
    float width_mu = 0;
    if (str_eq(cmd, "\\,")) width_mu = 3;
    else if (str_eq(cmd, "\\:")) width_mu = 4;
    else if (str_eq(cmd, "\\;")) width_mu = 5;
    else if (str_eq(cmd, "\\!")) width_mu = -3;
    else if (str_eq(cmd, "\\quad")) width_mu = 18;
    else if (str_eq(cmd, "\\qquad")) width_mu = 36;
    else {
        TSNode arg = get_field(node, "arg");
        if (!ts_node_is_null(arg))
            width_mu = parse_dimension_to_mu(read_node_text(arg));
    }
    return make_math_space(arena, width_mu);
}
```

### 3.7 Updated Dispatch Table

```cpp
MathASTNode* build_ts_node(TSNode node) {
    const char* type = ts_node_type(node);

    if (str_eq(type, "math"))              return build_math(node);
    if (str_eq(type, "group"))             return build_group(node);
    if (str_eq(type, "symbol"))            return build_symbol(node);
    if (str_eq(type, "number"))            return build_number(node);
    if (str_eq(type, "digit"))             return build_number(node);
    if (str_eq(type, "operator"))          return build_operator(node);
    if (str_eq(type, "relation"))          return build_relation(node);
    if (str_eq(type, "punctuation"))       return build_punctuation(node);
    if (str_eq(type, "command"))           return build_command(node);  // dispatches structural cmds
    if (str_eq(type, "subsup"))            return build_subsup(node);   // handles FLAG_LIMITS
    if (str_eq(type, "frac_like"))         return build_frac_like(node);
    if (str_eq(type, "infix_frac"))        return build_infix_frac(node);
    if (str_eq(type, "radical"))           return build_radical(node);
    if (str_eq(type, "delimiter_group"))   return build_delimiter_group(node);
    if (str_eq(type, "sized_delimiter"))   return build_sized_delimiter(node);
    if (str_eq(type, "accent"))            return build_accent(node);
    if (str_eq(type, "environment"))       return build_environment(node);
    if (str_eq(type, "matrix_command"))    return build_matrix_command(node);
    if (str_eq(type, "textstyle_command")) return build_textstyle_command(node);
    if (str_eq(type, "spacing_command"))   return build_spacing_command(node);
    if (str_eq(type, "brack_group"))       return build_brack_group(node);
    // ASCII math nodes
    if (str_eq(type, "word"))              return build_word(node);
    if (str_eq(type, "ascii_operator"))    return build_ascii_operator(node);
    if (str_eq(type, "quoted_text"))       return build_quoted_text(node);
    if (str_eq(type, "paren_script"))      return build_paren_script(node);
    // ... fallback: try children ...
}
```

Note: After Phase 4, `build_command()` internally dispatches to `build_annotated_command()`, `build_boxlike_command()`, `build_color_command()`, `build_rule_command()`, and `build_mathop_command()` based on the command name. These functions are still present but now called from `build_command()` instead of `build_ts_node()`.

---

## Part 4: Implementation Plan

### Phase 1: Simplify LaTeX Math Grammar ✅

| Step | Task | Status |
|------|------|--------|
| 1.1 | Merge spacing commands + eliminate `dimension_unit` | ✅ |
| 1.2 | Merge frac/binom/genfrac → `frac_like` | ✅ |
| 1.3 | Merge box/phantom → `boxlike_command` | ✅ |
| 1.4 | Merge overunder/arrow → `annotated_command` | ✅ |
| 1.5 | Merge text/style → `textstyle_command` | ✅ |
| 1.6 | Collapse delimiter char literals → regex | ✅ |
| 1.7 | Run `make generate-grammar`, verify `parser.c` size reduction | ✅ |
| 1.8 | Update `tex_math_ast_builder.cpp` dispatch + builder methods | ✅ |
| 1.9 | Run `make test-lambda-baseline` — all tests must pass | ✅ |

### Phase 2: Add ASCII Math Support to Grammar ✅

| Step | Task | Status |
|------|------|--------|
| 2.1 | Add `word` token (bare multi-letter identifiers) | ✅ |
| 2.2 | Add `ascii_operator` token (multi-char ops: `<=`, `->`, `xx`, etc.) | ✅ |
| 2.3 | Add `quoted_text` rule (`"quoted text"`) | ✅ |
| 2.4 | Extend `_script_arg` with `paren_script` and `word` | ✅ |
| 2.5 | Regenerate parser, verify parser.c size stays reasonable | ✅ |
| 2.6 | Verify existing LaTeX math tests still pass (no regressions) | ✅ |

### Phase 3: ASCII Flavor in AST Builder ✅

| Step | Task | Status |
|------|------|--------|
| 3.1 | Add `MathFlavor` enum and parameter to `MathASTBuilder` | ✅ |
| 3.2 | Implement `build_word()` with ASCII table lookups | ✅ |
| 3.3 | Implement `build_ascii_operator()` with ASCII→Unicode table | ✅ |
| 3.4 | Implement `build_quoted_text()` → TEXT node | ✅ |
| 3.5 | Add implicit multiplication insertion in `build_math()` | ✅ |
| 3.6 | Wire `MathFlavor` through `input-math.cpp` dispatcher | ✅ |

### Phase 4: Further Grammar Reduction ✅

| Step | Task | Status |
|------|------|--------|
| 4.1 | Remove 8 regex command tokens (`_symbol_cmd`, `_operator_cmd`, etc.) | ✅ |
| 4.2 | Remove 7 atom rules (`symbol_command`, `big_operator`, etc.) | ✅ |
| 4.3 | Simplify `operator` to char-only, `relation` to chars + `_updown_arrow_cmd` | ✅ |
| 4.4 | Enhance `build_command()` with structural command dispatch | ✅ |
| 4.5 | Add `collect_cmd_args()` helper for positional arg extraction | ✅ |
| 4.6 | Update `build_subsup()` to check FLAG_LIMITS for OVERUNDER | ✅ |
| 4.7 | Regenerate parser: 2.38 MB → 1.56 MB (-35%) | ✅ |

### Phase 5: Validation & Cleanup ✅

| Step | Task | Status |
|------|------|--------|
| 5.1 | ASCII math roundtrip tests (27/27 passing) | ✅ |
| 5.2 | LaTeX math roundtrip tests (11/11 passing) | ✅ |
| 5.3 | Input baseline (1714 tests: WPT + CommonMark + YAML + ASCII Math + LaTeX Math) | ✅ |
| 5.4 | Lambda baseline (448 tests) | ✅ |
| 5.5 | Radiant baseline (2149 tests) | ✅ |
| 5.6 | Remove `input-math-ascii.cpp` (old hand-written parser retired) | ✅ |

---

## Appendix A: ASCII↔LaTeX Syntax Equivalence

| ASCII Math | LaTeX Math | Grammar Token |
|------------|-----------|---------------|
| `x`, `y` | `x`, `y` | `symbol` (identical) |
| `123`, `3.14` | `123`, `3.14` | `number` (identical) |
| `+`, `-`, `*` | `+`, `-`, `*` | `operator` (identical) |
| `=`, `<`, `>` | `=`, `<`, `>` | `relation` (identical) |
| `^`, `_` | `^`, `_` | `subsup` (identical) |
| `{...}` | `{...}` | `group` (identical) |
| `(...)`, `[...]` | `(...)`, `[...]` | `punctuation` (identical) |
| `alpha`, `sin` | `\alpha`, `\sin` | `word` vs `command` (AST builder interprets) |
| `<=`, `>=`, `!=` | `\leq`, `\geq`, `\neq` | `ascii_operator` vs `command` |
| `->`, `<->` | `\to`, `\leftrightarrow` | `ascii_operator` vs `command` |
| `xx`, `+-`, `-:` | `\times`, `\pm`, `\div` | `ascii_operator` vs `command` |
| `"text"` | `\text{text}` | `quoted_text` vs `text_command` |
| `sum_(i=0)^n` | `\sum_{i=0}^{n}` | `word` + `paren_script` vs `command` + `group` |

All tokens on the left are **additions** to the grammar; all tokens on the right **already exist**. The grammar gains 3 new leaf tokens + 1 structural rule for full ASCII math coverage.

## Appendix B: File Change Summary

| File | Action |
|------|--------|
| `lambda/tree-sitter-latex-math/grammar.js` | Simplified (30 → 19 `_atom`), added ASCII tokens, removed 8 regex command tokens |
| `lambda/tree-sitter-latex-math/src/parser.c` | Regenerated (81K → 55K lines, 2.38 MB → 1.56 MB) |
| `lambda/tex/tex_math_ast_builder.cpp` | Added `MathFlavor`, merged builders, enhanced `build_command()` dispatch, `collect_cmd_args()` |
| `lambda/input/input-math.cpp` | Passes `MathFlavor` to builder |
| `lambda/input/input-math-ascii.cpp` | **Removed** — replaced by unified grammar path |
| `lambda/format/format-math-ascii.cpp` | Rewritten for tree-sitter Mark AST output |
| `lambda/format/format-math-latex.cpp` | **New** — LaTeX math formatter |
| `test/test_math_ascii_gtest.cpp` | ASCII math roundtrip tests (27 cases) |
| `test/test_math_gtest.cpp` | LaTeX math roundtrip tests (11 cases) |

No new parser files. No new build targets. The unified approach uses one grammar for both flavors.
