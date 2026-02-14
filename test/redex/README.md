# Redex Layout — Formal CSS Layout Semantics

A formal, executable specification of CSS layout algorithms using [PLT Redex](https://docs.racket-lang.org/redex/), the Racket-based domain-specific language for defining and testing operational semantics.

## Overview

This project provides:

1. **Formal specification** — A precise, executable definition of CSS Block, Inline, Flex, Grid, and Positioned layout in PLT Redex.
2. **Verification oracle** — An independent reference implementation to validate and debug the Radiant C++ layout engine.
3. **Extensible framework** — A foundation for future layout paradigms (graph layout, constraint-based layout).

## Project Structure

```
test/redex/
├── info.rkt                    # Racket package metadata
├── css-layout-lang.rkt         # Core Redex language definition (box tree, styles, views)
├── layout-common.rkt           # Shared utilities (box model, length resolution, margins)
├── layout-block.rkt            # Block flow layout (CSS 2.2 §10)
├── layout-inline.rkt           # Inline layout + line breaking
├── layout-flex.rkt             # Flexbox 9-phase algorithm (CSS Flexbox §9)
├── layout-grid.rkt             # Grid track sizing + placement (CSS Grid §12)
├── layout-positioned.rkt       # Absolute/fixed/relative positioning
├── layout-intrinsic.rkt        # Min-content / max-content sizing (CSS Sizing §4)
├── layout-dispatch.rkt         # Top-level layout dispatch
├── json-bridge.rkt             # JSON import/export for differential testing
├── run-layout.rkt              # CLI entry point
├── test-all.rkt                # Test runner
└── tests/
    ├── test-block.rkt           # Block layout unit tests
    ├── test-flex.rkt            # Flex layout unit tests
    ├── test-grid.rkt            # Grid layout unit tests
    └── test-invariants.rkt      # Cross-cutting property tests
```

## Requirements

- [Racket](https://racket-lang.org/) 8.0+ with `redex-lib` and `rackunit-lib`

Install dependencies:
```bash
raco pkg install redex-lib rackunit-lib
```

## Running Tests

```bash
# Run all tests
racket test/redex/test-all.rkt

# Run individual test suites
racket test/redex/tests/test-block.rkt
racket test/redex/tests/test-flex.rkt
racket test/redex/tests/test-grid.rkt
racket test/redex/tests/test-invariants.rkt
```

## Differential Testing with Radiant

The layout model can be used to verify Radiant's C++ output:

```bash
# 1. Export box tree from Radiant (requires --export-box-tree flag)
./lambda.exe layout test/layout/data/flex-basic.html --export-box-tree temp/box-tree.json

# 2. Run Redex layout model
racket test/redex/run-layout.rkt temp/box-tree.json -o temp/redex-layout.json

# 3. Compare results
python3 utils/compare-layouts.py view_tree.json temp/redex-layout.json
```

## Language Definition

The core `CSS-Layout` language defines:

- **Box tree** — block, inline, inline-block, flex, grid, table, text, replaced, none
- **Styles** — resolved computed CSS values (width, height, margin, padding, flex properties, grid properties, etc.)
- **Available space** — definite, indefinite, min-content, max-content
- **View tree** — layout result with (id, x, y, width, height, children)

### Box Tree Example

```racket
;; A flex container with two items
'(flex container
      (style (width (px 400))
             (flex-direction row)
             (justify-content space-between))
      ((block sidebar
              (style (width (px 150)) (height (px 300))
                     (flex-grow 0) (flex-shrink 0))
              ())
       (block main
              (style (height (px 300))
                     (flex-grow 1))
              ())))
```

### Running Layout

```racket
(require "layout-dispatch.rkt")

(define box '(block root (style (width (px 400)))
               ((block child (style (height (px 100))) ()))))

(define view (layout-document box 800 600))
;; => (view root 0 0 400 100 ((view child 0 0 400 100 ())))
```

## Layout Modes Implemented

| Mode | File | CSS Spec | Status |
|------|------|----------|--------|
| Block flow | `layout-block.rkt` | CSS 2.2 §10 | ✅ Core |
| Inline flow | `layout-inline.rkt` | CSS 2.2 §9.4.2 | ✅ Basic |
| Flexbox | `layout-flex.rkt` | CSS Flexbox §9 | ✅ Full 9-phase |
| Grid | `layout-grid.rkt` | CSS Grid §12 | ✅ Track sizing + placement |
| Positioned | `layout-positioned.rkt` | CSS 2.2 §9.3 | ✅ Absolute/relative |
| Intrinsic sizing | `layout-intrinsic.rkt` | CSS Sizing §4 | ✅ Min/max content |
| Table | `layout-dispatch.rkt` | CSS 2.2 §17 | ⚠️ Simplified |

## Design Decisions

- **Abstraction level**: Works with resolved computed styles, not raw CSS. No parsing or cascade.
- **Text measurement**: Simplified to pre-measured widths. Text nodes carry a `measured-width` field.
- **Pixel precision**: Uses exact rationals. No rounding artifacts.
- **Coordinate system**: Same as Radiant — positions relative to parent's border box.

## References

- [PLT Redex Documentation](https://docs.racket-lang.org/redex/)
- [CSS 2.2 Visual Formatting Model](https://www.w3.org/TR/CSS22/visuren.html)
- [CSS Flexible Box Layout](https://www.w3.org/TR/css-flexbox-1/)
- [CSS Grid Layout](https://www.w3.org/TR/css-grid-1/)
- [CSS Sizing Level 3](https://www.w3.org/TR/css-sizing-3/)
- [Radiant Layout Design](../../doc/Radiant_Layout_Design.md)
