# Radiant vs Pretext Analysis

## What is Pretext?

Pretext (`@chenglou/pretext`) is a **pure JavaScript/TypeScript library for multiline text measurement and layout** that avoids expensive DOM layout operations. It is NOT a layout engine — it solves one narrow problem: predict multiline text height/line-breaks without DOM reflow.

- **Two-phase model**: `prepare(text, font)` caches segment widths once (expensive); `layout(prepared, maxWidth, lineHeight)` is pure arithmetic on cached widths (cheap, 0.0002ms per text)
- **Zero runtime dependencies** — uses browser built-ins (`Intl.Segmenter`, Canvas API)
- **Published**: npm as `@chenglou/pretext`, v0.0.5

---

## 1. Feature Comparison

| Capability                            | Pretext               | Radiant                  |
| ------------------------------------- | --------------------- | ------------------------ |
| Block/Flex/Grid/Table layout          | ❌                     | ✅                        |
| CSS cascade & specificity             | ❌                     | ✅                        |
| Box model (margin/padding/border)     | ❌                     | ✅                        |
| Transforms, filters, blend modes      | ❌                     | ✅                        |
| Multi-column                          | ❌                     | ✅                        |
| Positioning (abs/rel/sticky)          | ❌                     | ✅                        |
| Text line-breaking                    | ✅                     | ✅                        |
| `white-space: pre-wrap`               | ✅                     | ✅                        |
| `word-break: keep-all`                | ✅                     | ✅                        |
| `overflow-wrap: break-word`           | ✅                     | ✅                        |
| Soft hyphen handling                  | ✅                     | ✅                        |
| Tab stops in `pre-wrap`               | ✅                     | ✅                        |
| CJK kinsoku (line-break classes)      | ✅                     | ✅                        |
| Bidi metadata                         | ✅ (metadata only)     | ✅ (`direction` property) |
| Variable-width per-line layout        | ✅ (core API)          | ❌ Not exposed            |
| Shrinkwrap/tight-fit measurement      | ✅ (binary search)     | ✅ (intrinsic sizing)     |
| Text wrapping around arbitrary shapes | ✅ (user-land via API) | ❌                        |
| Rich inline (mentions/pills)          | ✅                     | N/A (different scope)    |

**Bottom line**: Radiant is a far more complete CSS engine. Pretext excels at one thing Radiant doesn't expose: **variable-width-per-line text layout** for flowing text around irregular shapes.

---

## 2. How Pretext Supports Text Wrapping Around Irregular Shapes

Pretext does **not** implement CSS `shape-outside` or CSS Exclusions. Instead, it provides **low-level cursor-based APIs** that the user composes with custom geometry code.

### The API

```typescript
// Lay out one line at a caller-specified maxWidth, return cursor for next call
layoutNextLine(prepared, cursor, maxWidth) → { text, width, end cursor } | null
```

Each call lays out **one line** at a caller-specified `maxWidth`, then returns a cursor the caller feeds into the next call. The user varies `maxWidth` per line based on whatever geometry they want.

### The dynamic-layout demo flow

1. **Rasterize SVG logos** → extract alpha contour → compute convex hull (`getWrapHull()`)
2. **Transform hull points** by logo position + rotation angle
3. **For each line band** (y → y + lineHeight):
   - Query all obstacle polygons for blocked horizontal intervals at this band
   - **Carve available text slots** = full column width minus blocked intervals
   - Pick the widest available slot
   - Call `layoutNextLine(prepared, cursor, slotWidth)` — Pretext breaks text to fit that width
   - Advance cursor to next line
4. **Cursor carries across columns** — left column fills first, right column resumes from where left stopped

### Key insight

Pretext doesn't know about shapes at all. The geometry (`wrap-geometry.ts`) is entirely user code. Pretext just provides the primitive: "give me the next line of text that fits in N pixels." This makes it extremely composable.

### 8 Semantic Break Kinds

Pretext uses a richer break model than binary breakable/non-breakable:

- `text` — ordinary words/graphemes
- `space` — collapsible spaces (CSS `white-space: normal`)
- `preserved-space` — non-collapsible spaces in `pre-wrap` mode
- `tab` — tabs (advanced to next browser-style stop)
- `glue` — NBSP/NNBSP/ZWSP-style unbreakable runs
- `zero-width-break` — ZWSP (invisible but breakable)
- `soft-hyphen` — visible when broken, invisible when not
- `hard-break` — `\n` (only in `pre-wrap` mode)

---

## 3. How Pretext Tests Chrome Compatibility

Pretext achieves "100% match on controlled accuracy sweeps" through **browser-oracle validation**, not spec-based testing.

### Testing layers

| Layer | Method | Scale |
|---|---|---|
| **Accuracy sweep** | Hidden DOM divs with same text/font/size/width; compare `getBoundingClientRect()` height vs Pretext `layout()` height | 4 fonts × 8 sizes × 8 widths × 30 texts = **7,680 test cases** |
| **Per-line diagnostics** | On mismatch, extract both Pretext's and browser's actual line breaks, diff line-by-line | Automatic on any failure |
| **Long-form corpora** | 13 real-world texts (Chinese, Japanese, Arabic, Thai, Korean, Urdu, Myanmar, Khmer, mixed) swept across 61 widths (300–900px, step 10) | **~800 test points per corpus per browser** |
| **Multi-browser** | Chrome, Safari, Firefox each tested independently with browser-specific shims | 3 browsers |
| **Snapshots** | Results checked into `accuracy/*.json`, `corpora/*-step10.json` | Regression-tracked |

### How "100% compatible" works

- They test against **what Chrome actually does**, not the CSS spec
- Tolerance: ±0.005px (Chrome/Firefox), ±1/64px (Safari)
- Browser-specific shims: `lineFitEpsilon` differs per engine, CJK carry-after-quote is Chrome-only
- They accept they can't hit 100% on all widths for all scripts (Arabic shaping, Chinese punctuation compression remain "canaries")

### Failure taxonomy

Mismatches are classified into categories that guide whether a fix is worth pursuing:

- `corpus-dirty` — source text not trustworthy
- `normalization` — whitespace/break-kind modeling wrong
- `boundary-discovery` — wrong segmentation/merge boundaries
- `glue-policy` — right boundaries but wrong attachment
- `font-shaping` — context-dependent glyph shaping beyond segment-sum model
- `alignment-fitness` — paragraph-scale accumulation effects

---

## 4. Things Radiant Can Learn from Pretext

### A. Two-phase text architecture (prepare once, layout many)

Pretext's `prepare()` caches segment widths once; `layout()` is pure arithmetic. Radiant re-measures text during layout. For resize-heavy scenarios (responsive design, animation), caching segment widths could significantly reduce font metric lookups.

### B. 8 semantic break kinds

Distinguishing `text`, `space`, `preserved-space`, `tab`, `glue`, `zero-width-break`, `soft-hyphen`, `hard-break` is more granular than a binary breakable/non-breakable model and maps directly to CSS `white-space` modes. Worth comparing against Radiant's inline break model.

### C. Preprocessing over runtime heuristics

All of Pretext's accuracy wins came from the analysis/normalization phase (punctuation merging, kinsoku, glue rules), not from smarter line-breaking at layout time. Suggests that improving Radiant's text preprocessing could yield more than tweaking the line-breaking loop.

### D. Browser-oracle testing methodology

Radiant already does reference-based layout testing (comparing against Chrome view trees), but Pretext's approach of **fine-grained corpus sweeps** (61 widths × 13 languages) with **failure taxonomy** is more systematic for text-specific regression. Could be adapted for Radiant's inline layout testing.

### E. Variable-width line layout for CSS Exclusions

If Radiant ever wants to support `shape-outside` or CSS Exclusions, Pretext's `layoutNextLine(cursor, maxWidth)` pattern is the right primitive — calculate available width per line band from exclusion geometry, then lay out one line at that width. The `layoutColumn()` function in the dynamic-layout demo is essentially a reference implementation.

### F. Shrinkwrap via binary search

Pretext's bubble demo finds the tightest width that keeps the same line count via binary search on `layout()`. Simple, elegant, and potentially useful for Radiant's intrinsic sizing edge cases.

---

## Appendix: CSS `shape-outside` vs CSS Exclusions

These are two distinct CSS specifications for flowing text around non-rectangular shapes. They are often confused but differ fundamentally in scope and status.

### CSS `shape-outside`

- **Spec**: [CSS Shapes Module Level 1](https://www.w3.org/TR/css-shapes-1/) — W3C Candidate Recommendation (CR)
- **Browser support**: ~96.7% global (Chrome 37+, Firefox 62+, Safari 10.1+, Edge 79+)
- **Scope**: Controls **how inline content wraps around floated elements only**
- **Applies to**: Floats (`float: left` or `float: right`)
- **Direction**: Inline content flows along the **outside** of the defined shape

#### How it works

```css
.floated-image {
  float: left;
  width: 200px;
  height: 200px;
  shape-outside: circle(50%);   /* text wraps around a circular boundary */
  shape-margin: 10px;           /* adds clearance between shape and text */
}
```

The float's rectangular box is replaced by a shape for the purpose of inline layout. Text flows along the shape boundary instead of the float's bounding box. The shape can be:

- **Basic shapes**: `circle()`, `ellipse()`, `inset()`, `polygon()`
- **Image-based**: `url(image.png)` — derives shape from the alpha channel
- **Box values**: `margin-box`, `border-box`, `padding-box`, `content-box`

#### Limitations

- **Only works with floats** — cannot shape-wrap around positioned, grid, or flex items
- **Only affects one side** — text wraps on the opposite side of the float direction
- **No `shape-inside`** — Level 1 does not allow shaping the container's inner boundary (deferred to Level 2, which is stalled)

### CSS Exclusions

- **Spec**: [CSS Exclusions Module Level 1](https://www.w3.org/TR/css3-exclusions/) — W3C Working Draft (stalled since 2015)
- **Browser support**: ~0.29% global (only IE 10–11 with `-ms-` prefix; no modern browser)
- **Scope**: A **general mechanism** for any element to define an exclusion area that inline content flows around
- **Applies to**: Any positioned element (not limited to floats)
- **Direction**: Content flows around **both sides** of the exclusion

#### How it would work (if implemented)

```css
.obstacle {
  position: absolute;
  top: 100px;
  left: 150px;
  width: 200px;
  height: 200px;
  wrap-flow: both;        /* inline content wraps on both sides */
  wrap-through: wrap;     /* this element participates in exclusion */
}
```

Key properties:
- `wrap-flow`: `auto | both | start | end | minimum | maximum | clear` — controls which sides text wraps around
- `wrap-through`: `wrap | none` — whether an element's content respects exclusions
- Works with **any positioning scheme**, not just floats

#### Why it died

- Microsoft was the sole implementer (IE 10) and dropped it from Edge
- Complexity: requires a general "exclusion context" that interacts with all layout modes
- `shape-outside` solved the 80% use case (magazine-style float wrapping) with far less complexity
- No other browser vendor expressed intent to implement

### Comparison

| Aspect | `shape-outside` | CSS Exclusions |
|---|---|---|
| Spec status | CR (stable) | WD (stalled/dead) |
| Browser support | 96.7% | 0.29% (IE only, prefixed) |
| Applies to | Floats only | Any positioned element |
| Wrap direction | One side (opposite float) | Both sides, configurable |
| Shape definition | On the float itself | On the exclusion element |
| Complexity | Low (extends existing float model) | High (new layout primitive) |
| `shape-inside` | Deferred to Level 2 | Included via `wrap-flow` |

### Relevance to Radiant

For Radiant, **`shape-outside` is the practical target** — it has universal browser support, stable spec, and extends the existing float layout path. CSS Exclusions are effectively dead and not worth implementing.

Pretext's variable-width line approach (calling `layoutNextLine` with different `maxWidth` per line) maps naturally to `shape-outside` implementation: for each line band, compute the available width after subtracting the float's shape boundary, then lay out text at that width.
