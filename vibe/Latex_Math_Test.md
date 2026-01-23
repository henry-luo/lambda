# LaTeX Math Test Framework Proposal

## Overview

This document proposes a new comprehensive testing framework for LaTeX math typesetting in Lambda. Instead of relying solely on DVI binary comparison (which is fragile and focuses on low-level details), we adopt a **multi-layered semantic comparison approach** that evaluates correctness at different abstraction levels.

## Design Philosophy

### Problem with Pure DVI Comparison

Direct DVI comparison has several drawbacks:
1. **Brittle**: Minor font metric differences cause false failures
2. **Non-semantic**: Can't distinguish between important errors and cosmetic differences
3. **Hard to debug**: Binary diffs don't explain *what* is wrong
4. **All-or-nothing**: No partial credit for partially correct output

### Proposed Solution: Multi-Layer Semantic Testing

We compare output at three abstraction levels:
1. **AST Layer** (50% weight): Structural/semantic correctness
2. **HTML Layer** (40% weight): Visual representation correctness
3. **DVI Layer** (10% weight): Precise typographic correctness

This weighted approach ensures:
- Semantic correctness is prioritized
- Visual output matters significantly
- DVI precision is a bonus, not a gate

---

## Test Architecture

### Directory Structure

```
test/
├── latex/
│   ├── test_math_comparison.js      # Main test runner (Node.js)
│   ├── mathlive_reference.mjs       # MathLive AST/HTML generator
│   ├── katex_reference.mjs          # KaTeX HTML generator
│   ├── comparators/
│   │   ├── ast_comparator.js        # AST semantic comparison
│   │   ├── html_comparator.js       # HTML structure comparison
│   │   └── dvi_comparator.js        # DVI output comparison
│   ├── math/                        # All test files (flat structure)
│   │   ├── accents_*.json           # Accent tests
│   │   ├── arrows_*.json            # Arrow symbol tests
│   │   ├── bigops_*.json            # Big operator tests (\sum, \int, etc.)
│   │   ├── choose_*.json            # Binomial coefficient tests
│   │   ├── delims_*.json            # Delimiter tests
│   │   ├── fonts_*.json             # Font style tests (\mathbf, \mathrm, etc.)
│   │   ├── fracs_*.json             # Fraction tests
│   │   ├── greek_*.json             # Greek letter tests
│   │   ├── matrix_*.json            # Matrix/array tests
│   │   ├── operators_*.json         # Binary/relation operator tests
│   │   ├── scripts_*.json           # Subscript/superscript tests
│   │   ├── spacing_*.json           # Spacing and phantom tests
│   │   ├── sqrt_*.json              # Square root tests
│   │   └── complex_*.json           # Complex real-world formulas
│   ├── baseline/                    # Baseline tests (DVI must pass 100%)
│   │   ├── *.tex
│   │   └── reference/*.dvi
│   └── reference/                   # Reference files for extended tests
│       ├── *.ast.json               # MathLive AST reference
│       ├── *.mathlive.html          # MathLive HTML reference
│       ├── *.katex.html             # KaTeX HTML reference
│       └── *.dvi                    # pdfTeX DVI reference
```

### Test Naming Convention

All test files start with their **feature group prefix** for easy identification and filtering:

| Prefix | Feature Group | Examples |
|--------|---------------|----------|
| `accents_` | Accents & decorations | `accents_hat.json`, `accents_vec.json` |
| `arrows_` | Arrow symbols | `arrows_basic.json`, `arrows_extensible.json` |
| `bigops_` | Big operators | `bigops_sum.json`, `bigops_integral.json` |
| `choose_` | Binomial coefficients | `choose_basic.json`, `choose_nested.json` |
| `delims_` | Delimiters | `delims_basic.json`, `delims_extensible.json` |
| `fonts_` | Font styles | `fonts_mathbf.json`, `fonts_mathrm.json` |
| `fracs_` | Fractions | `fracs_basic.json`, `fracs_nested.json` |
| `greek_` | Greek letters | `greek_lower.json`, `greek_upper.json` |
| `matrix_` | Matrices & arrays | `matrix_basic.json`, `matrix_cases.json` |
| `not_` | Negations | `not_basic.json`, `not_relations.json` |
| `operators_` | Binary/relation ops | `operators_binary.json`, `operators_relations.json` |
| `scripts_` | Sub/superscripts | `scripts_basic.json`, `scripts_limits.json` |
| `spacing_` | Spacing & phantoms | `spacing_quad.json`, `spacing_phantom.json` |
| `sqrt_` | Square roots | `sqrt_basic.json`, `sqrt_nested.json` |
| `complex_` | Complex formulas | `complex_calculus.json`, `complex_physics.json` |

### Test Categories

| Category | Comparison | Requirement | Purpose |
|----------|------------|-------------|---------|
| `baseline` | DVI only | Must pass 100% | Core functionality, regression prevention |
| `extended` | AST + HTML + DVI | Scored | Comprehensive coverage, quality tracking |

---

## Comparison Layers

### 1. AST Comparison (50% weight)

**Purpose**: Verify that we parse LaTeX into the correct semantic structure.

**Reference**: MathLive AST (via `convertLatexToMathMl()` or internal AST)

**Normalization Rules**:
- Ignore node IDs and internal metadata
- Normalize whitespace in text content
- Treat equivalent structures as equal (e.g., `\frac{a}{b}` vs `{a \over b}`)
- Ignore style-only attributes that don't affect meaning

**Comparison Algorithm**:
```javascript
function compareAST(lambdaAST, mathLiveAST) {
    const results = {
        totalNodes: 0,
        matchedNodes: 0,
        differences: []
    };
    
    // Recursive tree comparison with normalization
    compareNodes(lambdaAST, mathLiveAST, '', results);
    
    return {
        passRate: results.matchedNodes / results.totalNodes * 100,
        differences: results.differences
    };
}
```

**Node Matching Criteria**:

| Property | Weight | Notes |
|----------|--------|-------|
| Node type | 40% | `frac`, `sqrt`, `sup`, etc. |
| Children count | 20% | Structural integrity |
| Children match | 30% | Recursive comparison |
| Attributes | 10% | Non-style attributes |

**Example Normalization**:
```javascript
// Input: \frac{1}{2}
// Lambda AST:
{ type: 'frac', numer: { type: 'ord', value: '1' }, denom: { type: 'ord', value: '2' } }

// MathLive AST (normalized):
{ type: 'frac', numer: { type: 'ord', value: '1' }, denom: { type: 'ord', value: '2' } }

// Result: 100% match
```

---

### 2. HTML Comparison (40% weight)

**Purpose**: Verify visual output structure matches expected rendering.

**Reference**: MathLive HTML output + KaTeX HTML output (cross-reference)

**Cross-Reference Strategy**:
Compare Lambda's HTML output against both MathLive and KaTeX, then take the **higher score** as the final HTML score. This accounts for cases where one reference may be wrong or differ in interpretation.

```javascript
async function compareHTML(lambdaHTML, testName) {
    // Load both references
    const mathLiveHTML = await loadReference(`${testName}.mathlive.html`);
    const katexHTML = await loadReference(`${testName}.katex.html`);
    
    // Compare against both
    const mathLiveScore = compareHTMLTrees(lambdaHTML, mathLiveHTML);
    const katexScore = compareHTMLTrees(lambdaHTML, katexHTML);
    
    // Take the higher score (be generous)
    const bestScore = Math.max(mathLiveScore.passRate, katexScore.passRate);
    const bestRef = mathLiveScore.passRate >= katexScore.passRate ? 'mathlive' : 'katex';
    
    return {
        passRate: bestScore,
        bestReference: bestRef,
        mathliveScore: mathLiveScore.passRate,
        katexScore: katexScore.passRate,
        differences: bestRef === 'mathlive' ? mathLiveScore.differences : katexScore.differences
    };
}
```

**Normalization Rules**:
- Strip all inline styles (compare structure, not exact styling)
- Normalize class names to semantic categories
- Ignore library-specific wrapper elements (MathLive's ML__, KaTeX's katex__)
- Collapse consecutive text nodes
- Normalize unicode math characters

**Comparison Algorithm**:
```javascript
function compareHTML(lambdaHTML, mathLiveHTML) {
    // Parse both into DOM trees
    const lambdaTree = parseHTML(lambdaHTML);
    const mathLiveTree = parseHTML(mathLiveHTML);
    
    // Normalize both trees
    const normLambda = normalizeHTMLTree(lambdaTree);
    const normMathLive = normalizeHTMLTree(mathLiveTree);
    
    // Compare structure
    return compareHTMLNodes(normLambda, normMathLive);
}
```

**Element Matching Criteria**:

| Property | Weight | Notes |
|----------|--------|-------|
| Tag name | 25% | Semantic element type |
| Class category | 20% | `frac`, `sqrt`, etc. |
| Children count | 20% | Structure integrity |
| Children match | 25% | Recursive comparison |
| Text content | 10% | For leaf nodes |

**Class Normalization Map**:
```javascript
const CLASS_CATEGORIES = {
    // Fractions
    'ML__frac': 'frac',
    'frac': 'frac',
    'lambda-frac': 'frac',
    
    // Roots
    'ML__sqrt': 'sqrt',
    'sqrt': 'sqrt',
    'lambda-sqrt': 'sqrt',
    
    // Scripts (MathLive)
    'ML__sup': 'superscript',
    'ML__sub': 'subscript',
    
    // Scripts (KaTeX)
    'msupsub': 'scripts',
    'vlist': 'vlist',
    // ... etc
};
```

---

### 3. DVI Comparison (10% weight)

**Purpose**: Verify precise typographic output for critical cases.

**Reference**: pdfTeX-generated DVI

**Normalization Rules**:
- Compare character codes and positions (with tolerance)
- Ignore font name variations (compare by metrics)
- Allow positional tolerance of ±0.5pt
- Normalize special characters

**Comparison Algorithm**:
```javascript
function compareDVI(lambdaDVI, referenceDVI) {
    const lambdaGlyphs = parseDVI(lambdaDVI);
    const refGlyphs = parseDVI(referenceDVI);
    
    const results = {
        totalGlyphs: refGlyphs.length,
        matchedGlyphs: 0,
        positionTolerance: 0.5, // points
        differences: []
    };
    
    // Match glyphs with tolerance
    for (const refGlyph of refGlyphs) {
        const match = findMatchingGlyph(lambdaGlyphs, refGlyph, results.positionTolerance);
        if (match) {
            results.matchedGlyphs++;
        } else {
            results.differences.push({ expected: refGlyph, found: null });
        }
    }
    
    return {
        passRate: results.matchedGlyphs / results.totalGlyphs * 100,
        differences: results.differences
    };
}
```

**Glyph Matching Criteria**:

| Property       | Required         | Tolerance              |
| -------------- | ---------------- | ---------------------- |
| Character code | Exact            | -                      |
| Font family    | Normalized       | cmr→roman, cmmi→italic |
| X position     | Within tolerance | ±0.5pt                 |
| Y position     | Within tolerance | ±0.5pt                 |

---

## Scoring System

### Per-Test Score Calculation

```javascript
function calculateTestScore(astResult, htmlResult, dviResult) {
    const weights = {
        ast: 0.50,   // 50% - semantic structure
        html: 0.40,  // 40% - visual representation
        dvi: 0.10    // 10% - precise typographics
    };
    
    const score = 
        astResult.passRate * weights.ast +
        htmlResult.passRate * weights.html +
        dviResult.passRate * weights.dvi;
    
    return {
        overall: score,
        breakdown: {
            ast: { rate: astResult.passRate, weighted: astResult.passRate * weights.ast },
            html: { rate: htmlResult.passRate, weighted: htmlResult.passRate * weights.html },
            dvi: { rate: dviResult.passRate, weighted: dviResult.passRate * weights.dvi }
        }
    };
}
```

### Suite Score Aggregation

```javascript
function calculateSuiteScore(testResults) {
    const total = testResults.reduce((sum, r) => sum + r.overall, 0);
    const average = total / testResults.length;
    
    return {
        totalTests: testResults.length,
        averageScore: average,
        breakdown: {
            ast: average(testResults.map(r => r.breakdown.ast.rate)),
            html: average(testResults.map(r => r.breakdown.html.rate)),
            dvi: average(testResults.map(r => r.breakdown.dvi.rate))
        },
        passed: testResults.filter(r => r.overall >= 80).length,
        failed: testResults.filter(r => r.overall < 80).length
    };
}
```

---

## Output & Reporting

### Console Output Format

```
================================================================================
📊 LaTeX Math Test Results (make test-math)
================================================================================

📂 Baseline Tests (DVI must pass 100%)
--------------------------------------------------------------------------------
  ✅ fracs_basic.tex            DVI: 100.0%
  ✅ sqrt_basic.tex             DVI: 100.0%
  ✅ scripts_basic.tex          DVI: 100.0%
--------------------------------------------------------------------------------
  Baseline: 3/3 passed (100%)

📂 Extended Tests by Feature Group
--------------------------------------------------------------------------------
  📁 accents (5 tests)
     ✅ accents_hat.json         AST:  98.0%  HTML:  95.0%  DVI:  92.0%  →  96.3%
     ✅ accents_vec.json         AST: 100.0%  HTML:  98.0%  DVI:  95.0%  →  98.7%
     Group Average: 97.5%

  📁 fracs (8 tests)
     ✅ fracs_basic.json         AST: 100.0%  HTML: 100.0%  DVI: 100.0%  → 100.0%
     ✅ fracs_nested.json        AST:  95.0%  HTML:  92.0%  DVI:  88.0%  →  93.3%
     ⚠️  fracs_continued.json    AST:  82.0%  HTML:  78.0%  DVI:  65.0%  →  79.7%
     Group Average: 91.0%

  📁 scripts (6 tests)
     ✅ scripts_basic.json       AST: 100.0%  HTML: 100.0%  DVI: 100.0%  → 100.0%
     ✅ scripts_limits.json      AST:  96.0%  HTML:  94.0%  DVI:  90.0%  →  94.8%
     Group Average: 97.4%

  📁 complex (10 tests)
     ✅ complex_calculus.json    AST:  92.0%  HTML:  88.0%  DVI:  82.0%  →  89.4%
     ❌ complex_physics.json     AST:  68.0%  HTML:  62.0%  DVI:  45.0%  →  63.3%
     Group Average: 76.4%

--------------------------------------------------------------------------------
📊 Feature Group Summary
--------------------------------------------------------------------------------
  accents:    97.5%  ████████████████████░░░░░
  arrows:     94.2%  ███████████████████░░░░░░
  bigops:     89.0%  ██████████████████░░░░░░░
  choose:     91.5%  ██████████████████░░░░░░░
  delims:     92.1%  ██████████████████░░░░░░░
  fonts:      95.8%  ███████████████████░░░░░░
  fracs:      91.0%  ██████████████████░░░░░░░
  greek:      99.1%  ████████████████████░░░░░
  matrix:     85.3%  █████████████████░░░░░░░░
  not:        88.0%  ██████████████████░░░░░░░
  operators:  96.8%  ███████████████████░░░░░░
  scripts:    97.4%  ███████████████████░░░░░░
  spacing:    88.5%  ██████████████████░░░░░░░
  sqrt:       95.0%  ███████████████████░░░░░░
  complex:    76.4%  ███████████████████░░░░░░
--------------------------------------------------------------------------------

================================================================================
📈 OVERALL SUMMARY
================================================================================
  Total Tests: 48
  Passed: 42 (87.5%)
  Failed: 6 (12.5%)
  
  Component Averages:
    AST:  92.0%  (target: 95%)
    HTML: 87.6%  (target: 90%)
    DVI:  83.1%  (target: 80%)
  
  Weighted Average Score: 90.1%
================================================================================
```

### Detailed Failure Report

When a test fails or scores below threshold, show detailed diff:

```
================================================================================
❌ DETAILED FAILURE REPORT: edge_case_spacing.tex
================================================================================

[AST COMPARISON] Score: 72.0%
--------------------------------------------------------------------------------
├─ Total Nodes: 25
├─ Matched: 18
└─ Differences:
   
   1. Path: root > frac > numer > mrow[0]
      Expected: { type: 'mspace', width: '0.2em' }
      Got:      { type: 'mspace', width: '0.16em' }
      Issue: Width mismatch
   
   2. Path: root > frac > denom
      Expected: 3 children
      Got:      2 children
      Issue: Missing node

[HTML COMPARISON] Score: 65.0%
--------------------------------------------------------------------------------
├─ Total Elements: 40
├─ Matched: 26
└─ Differences:
   
   1. Path: .math-container > .frac > .numer
      Expected: <span class="mspace" style="width:0.2em">
      Got:      <span class="mspace" style="width:0.16em">
      Issue: Style mismatch (ignored for scoring, but noted)
   
   2. Path: .math-container > .frac > .denom
      Expected: 3 child elements
      Got:      2 child elements
      Issue: Missing element

[DVI COMPARISON] Score: 45.0%
--------------------------------------------------------------------------------
├─ Total Glyphs: 20
├─ Matched: 9
├─ Position Tolerance: 0.5pt
└─ Differences:
   
   1. Glyph #5: '+'
      Expected: (120.5pt, 45.0pt)
      Got:      (118.2pt, 45.0pt)
      Delta: 2.3pt (exceeds 0.5pt tolerance)
   
   2. Glyph #12: missing
      Expected: 'x' at (145.0pt, 45.0pt)
      Got:      <not found>

================================================================================
```

---

## CLI Interface

### Basic Usage

```bash
# Run all tests
make test-math

# Run specific category
make test-math suite=baseline
make test-math suite=extended

# Run single test with verbose output
make test-math test=fracs_nested -v

# Run all tests in a feature group
make test-math group=fracs
make test-math group=scripts
make test-math group=complex

# Run with specific comparison only
make test-math compare=ast
make test-math compare=html
make test-math compare=dvi
```

### Node.js Script Options

```bash
node test/latex/test_math_comparison.js [options]

Options:
  --suite <name>        Test suite: 'baseline', 'extended', or 'all' (default)
  --test <file>         Run specific test file
  --group <prefix>      Run tests with given prefix (e.g., 'fracs', 'scripts')
  --compare <layer>     Run specific comparison: 'ast', 'html', 'dvi', or 'all'
  --tolerance <px>      Position tolerance for DVI comparison (default: 0.5)
  --verbose, -v         Show detailed comparison output
  --json                Output results as JSON
  --threshold <pct>     Minimum pass threshold (default: 80)
  --help, -h            Show help
```

---

## Implementation Plan

### Phase 1: Foundation (Week 1)
- [ ] Create test directory structure
- [ ] Implement AST comparator with normalization
- [ ] Set up MathLive reference generation

### Phase 2: HTML Comparison (Week 2)
- [ ] Implement HTML comparator
- [ ] Build normalization rules
- [ ] Create reference HTML files

### Phase 3: DVI Integration (Week 3)
- [ ] Adapt existing DVI comparison code
- [ ] Add tolerance-based matching
- [ ] Integrate with scoring system

### Phase 4: Reporting & Polish (Week 4)
- [ ] Build detailed reporting system
- [ ] Create Makefile targets
- [ ] Write documentation
- [ ] Establish baseline reference files

---

## Reference: Layout Test Comparison

This framework draws inspiration from `test/layout/test_radiant_layout.js`:

| Feature | Layout Tests | Math Tests |
|---------|--------------|------------|
| Reference source | Browser rendering | MathLive + KaTeX + pdfTeX |
| Comparison layers | 1 (DOM structure) | 3 (AST, HTML, DVI) |
| Tolerance | 5px layout | 0.5pt DVI position |
| Scoring | Element + Text % | Weighted composite |
| Normalization | Anonymous boxes, display:none | Node types, styles |

Key patterns adopted:
- Hierarchical tree comparison
- Tolerance-based matching
- Detailed difference reporting
- Pass rate thresholds
- JSON output mode for CI integration

---

## Success Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Baseline DVI pass rate | 100% | All baseline tests must pass DVI at 100% |
| Extended average score | ≥ 85% | Weighted average across all extended tests |
| AST component average | ≥ 90% | Semantic correctness |
| HTML component average | ≥ 85% | Visual representation (best of MathLive/KaTeX) |
| DVI component average | ≥ 75% | Precise typographics |
| Feature group minimums | ≥ 80% | No feature group below 80% average |

---

## Appendix A: MathLive Integration

### Generating Reference AST

```javascript
import { convertLatexToMarkup, serializeToJson } from 'mathlive';

async function generateReference(latex) {
    // Get MathLive's internal AST
    const mathfield = new MathfieldElement();
    mathfield.value = latex;
    const ast = mathfield.expression.json;
    
    // Get HTML output
    const html = convertLatexToMarkup(latex, { mathstyle: 'displaystyle' });
    
    return { ast, html };
}
```

### AST Normalization Schema

```javascript
const NORMALIZED_NODE_TYPES = {
    // Atoms
    'ord': 'ordinary',
    'op': 'operator',
    'bin': 'binary',
    'rel': 'relation',
    'open': 'open',
    'close': 'close',
    'punct': 'punctuation',
    
    // Structures
    'frac': 'fraction',
    'sqrt': 'radical',
    'sup': 'superscript',
    'sub': 'subscript',
    'supsub': 'scripts',
    
    // Layout
    'mrow': 'row',
    'mspace': 'space',
    'mphantom': 'phantom'
};
```

---

## Appendix B: KaTeX Integration

### Generating KaTeX Reference HTML

```javascript
import katex from 'katex';

function generateKaTeXReference(latex) {
    const html = katex.renderToString(latex, {
        displayMode: true,
        throwOnError: false,
        strict: false
    });
    
    return html;
}
```

### KaTeX HTML Structure

KaTeX produces HTML with specific class patterns:
- `.katex` - Root container
- `.katex-html` - HTML output (vs MathML)
- `.base` - Base content
- `.strut` - Vertical struts for alignment
- `.mord`, `.mbin`, `.mrel` - Atom classes
- `.mfrac` - Fractions
- `.msupsub` - Scripts
- `.sqrt` - Square roots

### Cross-Reference Scoring

```javascript
function htmlCrossReference(lambdaHTML, testName) {
    const mathlive = compareHTML(lambdaHTML, loadRef(`${testName}.mathlive.html`));
    const katex = compareHTML(lambdaHTML, loadRef(`${testName}.katex.html`));
    
    // Report both scores, use best
    console.log(`  MathLive: ${mathlive.passRate.toFixed(1)}%`);
    console.log(`  KaTeX:    ${katex.passRate.toFixed(1)}%`);
    console.log(`  Best:     ${Math.max(mathlive.passRate, katex.passRate).toFixed(1)}%`);
    
    return {
        passRate: Math.max(mathlive.passRate, katex.passRate),
        mathlive: mathlive,
        katex: katex
    };
}
```

---

## Appendix C: Future Enhancements

### 1. Visual Regression Testing (Optional Layer)

**Purpose**: Catch visual rendering issues that pass structural comparison but look wrong.

**Approach**:
- Render both Lambda and MathLive output to PNG at fixed DPI
- Use perceptual image diff (e.g., pixelmatch, resemble.js)
- Report visual similarity percentage

---

### 2. Symbol Coverage Tracking

**Purpose**: Ensure comprehensive coverage of LaTeX math symbols.

### 3. Test Categorization by Feature

Organize tests by mathematical feature for targeted testing:

```
test/latex/
├── baseline/           # Core tests (must pass)
├── extended/
│   ├── fractions/      # \frac, \tfrac, \dfrac, nested
│   ├── scripts/        # subscripts, superscripts, limits
│   ├── roots/          # \sqrt, nth roots
│   ├── matrices/       # matrix environments
│   ├── delimiters/     # \left \right, sizing
│   ├── accents/        # \hat, \bar, \vec, etc.
│   ├── operators/      # \sum, \int, \prod with limits
│   ├── spacing/        # \quad, \!, \,, phantom
│   ├── fonts/          # \mathbf, \mathrm, \mathcal
│   └── complex/        # Real-world equations
```

**CLI Support**:
```bash
make test-latex-math feature=fractions    # Test only fractions
make test-latex-math feature=scripts      # Test only scripts
```

### 4. KaTeX Cross-Reference

Add KaTeX as secondary reference for validation:

**Benefits**:
- Cross-validate reference correctness
- Identify cases where references disagree
- Higher confidence when multiple sources match

### 5. Interactive Debug Mode

When investigating failures, launch interactive mode:

```bash
make test-latex-debug test=complex_fraction
```

**Features**:
- Side-by-side AST tree view
- Highlight differing nodes
- Expand/collapse subtrees
- Show normalized vs raw values
- Export comparison to HTML report

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 🔍 Interactive Debug: complex_fraction.tex                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│ Lambda AST                         │ MathLive AST                           │
│ ───────────────────────────────────│────────────────────────────────────────│
│ ▼ frac                             │ ▼ frac                                 │
│   ▼ numer                          │   ▼ numer                              │
│     ● ord: "x"                     │     ● ord: "x"                         │
│     ● sup                          │     ● sup                              │
│       ● ord: "2"                   │       ● ord: "2"                       │
│   ▼ denom                          │   ▼ denom                              │
│     ● ord: "y"                     │     ● ord: "y"                         │
│   ❌ MISSING                       │     ● mspace: 0.2em    ← EXTRA         │
├─────────────────────────────────────────────────────────────────────────────┤
│ [a]ST  [h]TML  [d]VI  [n]ext diff  [p]rev diff  [e]xport  [q]uit           │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Conclusion

This multi-layer testing approach provides:

1. **Better signal**: Semantic comparison catches real bugs, not cosmetic differences
2. **Graduated feedback**: Percentage scores show progress, not just pass/fail
3. **Debuggability**: Detailed diffs explain exactly what's wrong
4. **Flexibility**: Weights can be adjusted as the engine matures
5. **Robustness**: Not dependent on any single layer being perfect

The 50/40/10 weighting prioritizes semantic correctness while still valuing visual output and precise typographics.

### Enhancement Summary

| Enhancement | Priority | Effort | Value |
|-------------|----------|--------|-------|
| Visual regression testing | Medium | Medium | Catches visual-only bugs |
| Symbol coverage tracking | High | Low | Ensures completeness |
| Feature categorization | High | Low | Targeted testing |
| Error handling tests | High | Medium | Robustness |
| KaTeX cross-reference | Low | Medium | Validation confidence |
| Performance benchmarking | Medium | Low | Track regressions |
| Snapshot management | High | Medium | Developer productivity |
| CI/CD integration | High | Low | Automation |
| Interactive debug mode | Medium | High | Debug productivity |
| Real document extraction | Low | Medium | Real-world coverage |
