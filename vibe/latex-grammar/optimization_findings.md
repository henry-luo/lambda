# LaTeX Tree-sitter Grammar Size Optimization Findings

**Date:** December 13, 2025

## Executive Summary

Comparative analysis of grammar approaches reveals massive size differences. The hybrid approach achieves **262KB** (vs 6.3MB current) while maintaining LaTeX.js-compatible structure.

---

## Comparison Matrix

| Metric | Original | Phase 9 | Minimal | **Hybrid** | JavaScript |
|--------|----------|---------|---------|------------|------------|
| **Parser Size** | 61 MB | 6.3 MB | 84 KB | **262 KB** | 2.4 MB |
| **STATE_COUNT** | ~10,000+ | 3,169 | 93 | **241** | 1,722 |
| **LARGE_STATE_COUNT** | ~2,000+ | 435 | 13 | **11** | 326 |
| **SYMBOL_COUNT** | ~250 | 236 | 34 | **81** | 259 |
| **Rule Count** | ~150 | 150 | 22 | **50** | 150 |

## Hybrid Grammar Approach

The hybrid grammar follows LaTeX.js design philosophy:

> **"Parse structure, not semantics."**

### Key Features
1. ✅ **Generic command parsing** - All `\command` become generic nodes
2. ✅ **Preamble + Document structure** - Matches `\begin{document}...\end{document}`
3. ✅ **Section hierarchy** - Sections with content (part → subparagraph)
4. ✅ **Math environments** - Inline (`$...$`, `\(...\)`) and display (`$$...$$`, `\[...\]`)
5. ✅ **Generic environments** - `\begin{name}...\end{name}`
6. ✅ **Verbatim environments** - External scanner for verbatim, lstlisting, minted
7. ✅ **Comment environments** - External scanner for `\begin{comment}...\end{comment}`
8. ✅ **Paragraph breaks** - Blank line detection
9. ✅ **Ligatures** - `---`, `--`, ``` `` ```, `''`, etc.
10. ✅ **Control symbols** - `\$`, `\%`, `\#`, etc.

---

## Feature Comparison

### Current Grammar (Phase 9)

**Strengths:**
- 24+ distinct command types (citation, label, include, etc.)
- 14 curly_group variants with field extraction
- 9+ specialized environment types
- External scanners for verbatim content
### Semantic Post-Processing

Since commands are generic, semantic distinctions are handled at runtime:

```javascript
// Example: Identify citation commands from generic command nodes
function isCitation(commandNode) {
  const name = commandNode.namedChildren.find(c => c.type === 'command_name');
  return /\\(cite|citep|citet|autocite|parencite)/.test(name?.text);
}
```

This matches LaTeX.js's approach where the PEG.js parser uses a single `macro` rule and the generator determines command behavior via `g.hasMacro(name)`.

---

## Metrics Tracking

| Feature Added | Parser Size | STATE_COUNT | LARGE_STATE_COUNT | Delta |
|--------------|-------------|-------------|-------------------|-------|
| Baseline (minimal) | 84 KB | 93 | 13 | - |
| + LaTeX.js structure | 244 KB | 235 | 7 | +160 KB |
| + External scanners | 244 KB | 235 | 7 | +0 KB |
| + Section hierarchy | 262 KB | 241 | 11 | +18 KB |
| **Final Hybrid** | **262 KB** | **241** | **11** | **24x smaller than Phase 9** |

---

## Root Causes of Size Explosion

### 1. Command Type Explosion
Each specialized command rule creates unique parse paths:
```javascript
// CURRENT: 24 command alternatives = state explosion
_command: $ => choice(
  $.citation,           // creates states for cite|citep|citet|...
  $.label_command,      // creates states for label|ref|eqref|...
  $.include_command,    // creates states for include|input|...
  // ... 21 more types
)

// MINIMAL: 1 generic command = minimal states
command: $ => seq($.command_name, repeat($.arg))
```

### 2. Curly Group Variants
14 typed variants vs 1 generic:
```javascript
// CURRENT: 14 variants
curly_group_text, curly_group_label, curly_group_path,
curly_group_uri, curly_group_spec, curly_group_impl, ...

// MINIMAL: 1 variant
curly_group: $ => seq('{', repeat($._inline_item), '}')
```

### 3. Environment Specialization
Each environment type multiplies rules via `specialEnvironment()`:
```javascript
// CURRENT: Each creates 5 rules (main + begin + end + group + name)
...specialEnvironment({ rule: 'comment_environment', ... }),
...specialEnvironment({ rule: 'verbatim_environment', ... }),
...specialEnvironment({ rule: 'listing_environment', ... }),
// ... 6 more

// MINIMAL: 1 generic environment
environment: $ => seq($.begin_env, repeat($._block_item), $.end_env)
```

### 4. Text Parsing Complexity
Rich inline text parsing creates combinatorial states:
```javascript
// CURRENT: Complex inline with embedded commands
_text_content: $ => choice(
  $.curly_group, $.block_comment, $._command,
  $.paragraph_break, $.text, $._math_content, '(', ')'
)

// MINIMAL: Simple text chunk
text_chunk: $ => /[^\\{}$%\[\]\n]+/
```

---

## Hybrid Optimization Strategy

### Phase 1: Baseline (84 KB, 93 states)
Start with ChatGPT's minimal grammar.

### Phase 2: Add External Scanners
**Critical for correctness** - verbatim environments cannot be parsed with regex.
- `_trivia_raw_env_comment`
- `_trivia_raw_env_verbatim`
- `_trivia_raw_env_listing`
- `_trivia_raw_env_minted`

**Expected impact:** +50-100 states

### Phase 3: Add Math Environments
Displayed equations need proper handling.
- `$...$` and `$$...$$`
- `\(...\)` and `\[...\]`
- `equation`, `align`, etc.

**Expected impact:** +100-200 states

### Phase 4: Selective Command Types (Optional)
Only add if post-processing is too slow/complex:
- Citation commands (for bibliography integration)
- Include commands (for multi-file support)

**Expected impact:** +500-1000 states each

### Phase 5: Section Structure (Optional)
If AST hierarchy is needed at parse-time:
- `part`, `chapter`, `section`, etc.

**Expected impact:** +200-500 states

---

## Metrics Tracking Template

| Feature Added | Parser Size | STATE_COUNT | LARGE_STATE_COUNT | Notes |
|--------------|-------------|-------------|-------------------|-------|
| Baseline | 84 KB | 93 | 13 | Minimal grammar |
| + External scanners | TBD | TBD | TBD | |
| + Math environments | TBD | TBD | TBD | |
| + Citations | TBD | TBD | TBD | |
| + Sections | TBD | TBD | TBD | |

---

## Decision Framework

For each feature, ask:
1. **Is parse-time detection essential?** (e.g., verbatim content must stop LaTeX parsing)
2. **Can post-processing handle it efficiently?** (e.g., regex over `command` nodes)
3. **What's the state cost?** (measure before/after)

**Rule of thumb:** If state cost > 500 for a feature, prefer post-processing.

---

## Files in This Directory

- `OPTIMIZATION_FINDINGS.md` - This document
- `tree_sitter_la_te_x_grammar_size_optimization_guide.md` - ChatGPT's optimization guide
- `refactored_size_optimized_la_te_x_tree_sitter_grammar.js` - ChatGPT's minimal grammar (84KB)
- `test-grammar/` - ChatGPT's minimal grammar test
- `hybrid-grammar/` - **Recommended grammar** (262KB, LaTeX.js-compatible)
  - `grammar.js` - Hybrid grammar following LaTeX.js structure
  - `src/scanner.c` - External scanner for verbatim/comment environments
  - `src/parser.c` - Generated parser

---

## Results Summary

| Approach | Parser Size | Reduction | LaTeX.js Compatible |
|----------|-------------|-----------|---------------------|
| Original | 61 MB | - | ❌ Over-specified |
| Phase 9 | 6.3 MB | 90% | ❌ Over-specified |
| Minimal | 84 KB | 99.8% | ❌ Too minimal |
| **Hybrid** | **262 KB** | **99.6%** | ✅ **Yes** |

**Recommendation:** Use the hybrid grammar at `hybrid-grammar/` for LaTeX.js-style processing.

---

## Next Steps

1. ✅ Document findings (this file)
2. ✅ Create hybrid grammar with external scanners
3. ✅ Measure state growth for each feature
4. ✅ Target achieved: 262 KB parser, 241 states
5. 🔄 Copy hybrid grammar to main tree-sitter-latex if approved
6. 🔄 Update LaTeX input parser to use new AST structure
