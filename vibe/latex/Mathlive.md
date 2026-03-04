# MathLive Architecture Analysis

This document provides a detailed analysis of the MathLive codebase to serve as a reference for improving Radiant's LaTeX and math support.

## Table of Contents

1. [Overview](#overview)
2. [Parsing: LaTeX Source → Tokens → Atoms](#parsing-latex-source--tokens--atoms)
   - [Tokenizer](#tokenizer)
   - [Parser](#parser)
   - [Command Definitions](#command-definitions)
3. [Data Representation: The Atom Tree](#data-representation-the-atom-tree)
   - [Atom Class Hierarchy](#atom-class-hierarchy)
   - [Atom Types](#atom-types)
   - [Branches](#branches)
4. [Typesetting: Atoms → Boxes](#typesetting-atoms--boxes)
   - [Box Class](#box-class)
   - [Context System](#context-system)
   - [Math Styles](#math-styles)
   - [Rendering Pipeline](#rendering-pipeline)
   - [Inter-Box Spacing](#inter-box-spacing)
5. [Key Typesetting Algorithms](#key-typesetting-algorithms)
   - [Fractions (GenfracAtom)](#fractions-genfracatom)
   - [Radicals (SurdAtom)](#radicals-surdatom)
   - [Delimiters](#delimiters)
   - [Superscript/Subscript](#superscriptsubscript)
   - [Arrays/Matrices](#arraysmatrices)
6. [Font Metrics](#font-metrics)
7. [Lessons for Radiant](#lessons-for-radiant)

---

## Overview

MathLive is a TypeScript-based math editor that implements a LaTeX-compatible typesetting engine. Its architecture follows the classic TeX approach with a three-stage pipeline:

```
LaTeX String → Tokenizer → Parser → Atom Tree → Renderer → Box Tree → HTML
```

**Key directories:**
- `src/core/` - Core parsing and rendering engine
- `src/atoms/` - Individual atom type implementations
- `src/latex-commands/` - LaTeX command definitions
- `src/editor-mathfield/` - Interactive editor components

---

## Parsing: LaTeX Source → Tokens → Atoms

### Tokenizer

**File:** [src/core/tokenizer.ts](../mathlive/src/core/tokenizer.ts)

The tokenizer converts a LaTeX string into a stream of tokens. It handles:

```typescript
// Token types:
// - '<space>'     - whitespace
// - '<{>'         - opening brace
// - '<}>'         - closing brace
// - '<$>'         - math mode shift
// - '<$$>'        - display math mode shift
// - '\\command'   - LaTeX commands
// - literal chars - single characters like 'a', '+', etc.
```

**Key class: `Tokenizer`**

```typescript
class Tokenizer {
  obeyspaces = false;           // If true, preserve individual spaces
  private readonly s: string | string[];  // Input string (may be grapheme array)
  private pos = 0;              // Current position

  end(): boolean;               // Check if at end
  get(): string;                // Get next char and advance
  peek(): string;               // Look at next char without advancing
  match(regEx: RegExp): string; // Match regex pattern
  next(): Token | null;         // Get next token
}
```

**Token expansion:**
- `\relax` - ignored (no-op)
- `\noexpand` - prevents expansion of next token
- `\obeyspaces` - enables space preservation
- `\bgroup`/`\egroup` - synonyms for `{`/`}`
- `\csname...\endcsname` - construct command names
- `#n` - parameter substitution (for macros)

**Main API:**

```typescript
function tokenize(s: string, args?: (arg: string) => string | undefined): Token[]
```

### Parser

**File:** [src/core/parser.ts](../mathlive/src/core/parser.ts)

The parser transforms tokens into an Atom tree. It maintains a **parsing context** that tracks:

```typescript
interface ParsingContext {
  parent: ParsingContext | undefined;
  mathlist: Atom[];          // Current list being built
  style: Style;              // Current style (color, font, etc.)
  parseMode: ParseMode;      // 'math' | 'text' | 'latex'
  mathstyle: MathstyleName;  // 'displaystyle' | 'textstyle' | etc.
  tabular: boolean;          // Inside tabular environment?
}
```

**Key Parser methods:**

| Method | Description |
|--------|-------------|
| `scan(done?)` | Parse tokens until terminator, returns `Atom[]` |
| `scanGroup()` | Parse `{...}` group, returns `GroupAtom` |
| `scanArgument(type)` | Parse command argument of given type |
| `scanOptionalArgument(type)` | Parse `[...]` optional argument |
| `scanDelim()` | Parse delimiter like `(`, `\vert` |
| `scanLeftRight()` | Parse `\left...\right` |
| `scanEnvironment()` | Parse `\begin{env}...\end{env}` |
| `parseSupSub()` | Handle `^` and `_` |
| `scanSymbolOrCommand(cmd)` | Parse a command and its arguments |

**Argument Types:**

```typescript
type ArgumentType = 
  | 'auto'           // Current mode (math or text)
  | 'math'           // Math content
  | 'text'           // Text content  
  | 'expression'     // Math expression
  | 'string'         // Literal string
  | 'balanced-string'// Balanced {...} string
  | 'delim'          // Delimiter
  | 'colspec'        // Column specification for arrays
  | 'value'          // Dimension, glue, or string
  | 'bbox'           // BBox parameters
  | 'rest'           // Remaining content
```

**Entry point:**

```typescript
function parseLatex(latex: string, options?: {...}): Atom[]
```

### Command Definitions

**File:** [src/latex-commands/definitions-utils.ts](../mathlive/src/latex-commands/definitions-utils.ts)

Commands are defined in two registries:

1. **MATH_SYMBOLS** - Simple symbols (no arguments)
```typescript
interface LatexSymbolDefinition {
  definitionType: 'symbol';
  type: AtomType;         // 'mord', 'mbin', 'mrel', etc.
  codepoint: number;      // Unicode codepoint
  variant?: Variant;      // Font variant
}
```

2. **LATEX_COMMANDS** - Functions with arguments
```typescript
interface LatexCommandDefinition<T extends Argument[] = Argument[]> {
  definitionType: 'function';
  params: FunctionArgumentDefinition[];  // Parameter specifications
  infix: boolean;                        // Is it infix (like \over)?
  isFunction: boolean;                   // Is it a function name?
  ifMode?: ParseMode;                    // Only valid in this mode
  applyMode?: ParseMode;                 // Switches to this mode
  createAtom?: (options: CreateAtomOptions<T>) => Atom;
  applyStyle?: (...) => PrivateStyle;
  serialize?: (atom: Atom, options: ToLatexOptions) => string;
  render?: (atom: Atom, context: Context) => Box | null;
}
```

**Example - Defining `\sqrt`:**

```typescript
defineFunction('sqrt', '[index:auto]{radicand:expression}', {
  ifMode: 'math',
  createAtom: (options) =>
    new SurdAtom({
      ...options,
      body: argAtoms(options.args![1]),
      index: options.args![0] ? argAtoms(options.args![0]) : undefined,
    }),
});
```

---

## Data Representation: The Atom Tree

### Atom Class Hierarchy

**File:** [src/core/atom-class.ts](../mathlive/src/core/atom-class.ts)

An **Atom** represents an elementary mathematical unit, independent of its graphical representation.

```typescript
class Atom<T extends (Argument | null)[] = (Argument | null)[]> {
  // Tree structure
  parent: Atom | undefined;
  parentBranch: Branch | undefined;
  private _branches: Branches;

  // Content
  value: string;                    // Single character value
  command: string;                  // LaTeX command ('\sin') or character
  args: T;                          // Command arguments
  verbatimLatex: string | undefined;// Original LaTeX

  // Type and styling
  type: AtomType | undefined;
  mode: ParseMode;
  style: PrivateStyle;

  // Layout hints
  subsupPlacement: 'auto' | 'over-under' | 'adjacent' | undefined;
  isFunction: boolean;
  isRoot: boolean;
  skipBoundary: boolean;
  captureSelection: boolean;

  // Rendering state (updated each render cycle)
  isSelected: boolean;
  containsCaret: boolean;
  caret: ParseMode | undefined;
}
```

### Atom Types

From TeX's classification (TeXBook p.158):

| Type | Description | Example |
|------|-------------|---------|
| `mord` | Ordinary atom | x, y, α |
| `mop` | Large operator | ∑, ∫ |
| `mbin` | Binary operation | +, −, × |
| `mrel` | Relation | =, <, ≤ |
| `mopen` | Opening delimiter | (, [, { |
| `mclose` | Closing delimiter | ), ], } |
| `mpunct` | Punctuation | , |
| `minner` | Inner (fractions) | ½ |

**Specialized Atom Types (in `src/atoms/`):**

| Class | Purpose |
|-------|---------|
| `GenfracAtom` | Fractions, binomials |
| `SurdAtom` | Square roots, nth roots |
| `LeftRightAtom` | `\left...\right` delimiters |
| `ArrayAtom` | Matrices, arrays, align environments |
| `AccentAtom` | Accents like `\hat`, `\vec` |
| `OverunderAtom` | `\overbrace`, `\underbrace` |
| `SubsupAtom` | Standalone sub/superscripts |
| `OperatorAtom` | Function names like `\sin`, `\lim` |
| `GroupAtom` | Grouped content `{...}` |
| `EncloseAtom` | `\boxed`, `\cancel` |
| `ExtensibleSymbolAtom` | Extensible operators |
| `SpacingAtom` | Explicit spacing |
| `TextAtom` | Text mode content |

### Branches

Atoms can have multiple **branches** of children:

```typescript
const NAMED_BRANCHES: BranchName[] = [
  'body',        // Main content
  'above',       // Numerator / index / accent
  'below',       // Denominator / under-content
  'superscript', // Superscript
  'subscript',   // Subscript
];

type Branch = BranchName | [row: number, col: number];  // For arrays
```

**Branch access:**

```typescript
// Read-only access
atom.body          // readonly Atom[] | undefined
atom.superscript   // readonly Atom[] | undefined
atom.subscript     // readonly Atom[] | undefined
atom.above         // readonly Atom[] | undefined
atom.below         // readonly Atom[] | undefined

// Setters trigger isDirty
atom.body = atoms;

// Generic access
atom.branch('superscript')    // readonly Atom[] | undefined
atom.createBranch('body')     // Atom[] (creates if needed)
```

---

## Typesetting: Atoms → Boxes

### Box Class

**File:** [src/core/box.ts](../mathlive/src/core/box.ts)

A **Box** is the most elementary renderable element. It represents the geometric layout result.

```typescript
class Box implements BoxInterface {
  // Tree structure  
  parent: Box | undefined;
  children?: Box[];
  value: string;          // Text content

  // Type for inter-box spacing
  type: BoxType;
  
  // Dimensions (in em)
  _height: number;        // Distance above baseline
  _depth: number;         // Distance below baseline
  _width: number;
  skew: number;           // Horizontal skew
  italic: number;         // Italic correction
  scale: number;          // Relative to parent (1.0 = no scale)
  maxFontSize: number;

  // Styling
  classes: string;
  cssProperties?: Partial<Record<BoxCSSProperties, string>>;
  cssId?: string;
  attributes?: Record<string, string>;

  // SVG content (for delimiters, lines)
  svgBody?: string;
  svgOverlay?: string;

  // Selection state
  isSelected: boolean;
  caret?: ParseMode;
}
```

**Box Types (for spacing):**

```typescript
type BoxType = 
  | 'ord'    | 'bin'    | 'op'    | 'rel'
  | 'open'   | 'close'  | 'punct' | 'inner'
  | 'rad'    | 'latex'  | 'composition'
  | 'middle' | 'ignore' | 'lift'  | 'skip';
```

### Context System

**File:** [src/core/context.ts](../mathlive/src/core/context.ts)

The **Context** encapsulates rendering state, forming a chain for inheritance:

```typescript
class Context implements ContextInterface {
  readonly parent?: Context;        // Parent context for inheritance
  readonly registers: Registers;    // TeX registers (e.g., \thinmuskip)
  
  // Inherited properties
  letterShapeStyle: 'tex' | 'french' | 'iso' | 'upright';
  readonly smartFence: boolean;
  readonly color: string;
  readonly backgroundColor: string;
  readonly minFontScale: number;
  readonly maxMatrixCols: number;
  
  // Font sizing
  readonly size: FontSize;           // Base font size (1-10)
  readonly mathstyle: Mathstyle;     // Current math style
  
  // Computed properties
  get scalingFactor(): number;       // Combined scale from size + mathstyle
  get metrics(): FontMetrics;        // Font metrics for current style
  get isDisplayStyle(): boolean;
  get isCramped(): boolean;
  get isTight(): boolean;
}
```

**Context creation:**

```typescript
// Inherit from parent with new mathstyle
const context = new Context(
  { parent: parentContext, mathstyle: 'superscript' },
  style
);
```

### Math Styles

**File:** [src/core/mathstyle.ts](../mathlive/src/core/mathstyle.ts)

TeX defines 8 math styles that control sizing and layout:

```typescript
// Style IDs (higher = larger)
const D = 7;    // Displaystyle
const Dc = 6;   // Displaystyle, cramped
const T = 5;    // Textstyle  
const Tc = 4;   // Textstyle, cramped
const S = 3;    // Scriptstyle
const Sc = 2;   // Scriptstyle, cramped
const SS = 1;   // Scriptscriptstyle
const SSc = 0;  // Scriptscriptstyle, cramped

class Mathstyle {
  readonly id: 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7;
  readonly sizeDelta: -4 | -3 | 0;  // Font size adjustment
  readonly cramped: boolean;
  metrics: FontMetrics;             // Pre-computed metrics

  get sup(): Mathstyle;     // Style for superscript
  get sub(): Mathstyle;     // Style for subscript  
  get fracNum(): Mathstyle; // Style for fraction numerator
  get fracDen(): Mathstyle; // Style for fraction denominator
  get cramp(): Mathstyle;   // Cramped version
  get isTight(): boolean;   // Is script/scriptscript?
}
```

**Style transitions:**

| Current | Superscript | Subscript | Frac Num | Frac Den |
|---------|-------------|-----------|----------|----------|
| D  | S  | Sc | T  | Tc |
| D' | S' | Sc | T' | Tc |
| T  | S  | Sc | S  | Sc |
| T' | S' | Sc | S' | Sc |
| S  | SS | SSc | SS | SSc |
| S' | SS'| SSc | SS'| SSc |

### Rendering Pipeline

The rendering process transforms Atoms to Boxes:

```
Atom.render(context) → Box | null
```

**In Atom class:**

```typescript
render(parentContext: Context): Box | null {
  // 1. Get custom renderer from definition
  const def = getDefinition(this.command, this.mode);
  if (def?.render) return def.render(this, parentContext);

  // 2. Create box for body/value
  const context = new Context({ parent: parentContext }, this.style);
  let result = this.createBox(context, { classes: ... });
  
  // 3. Attach superscript/subscript if present
  if (!this.subsupPlacement && (this.superscript || this.subscript)) {
    result = this.attachSupsub(context, { base: result });
  }

  return result.wrap(context);
}

// Static helper to render a list of atoms
static createBox(
  context: Context,
  atoms: readonly Atom[] | undefined,
  options?: { type?: BoxType; classes?: string }
): Box | null {
  // Group atoms by style runs, render each run
  const runs = getStyleRuns(atoms);
  const boxes: Box[] = [];
  for (const run of runs) {
    const box = renderStyleRun(context, run, {...});
    if (box) boxes.push(box);
  }
  
  if (boxes.length === 0) return null;
  if (boxes.length === 1) return boxes[0].wrap(context);
  return new Box(boxes, { classes, type }).wrap(context);
}
```

**Box.wrap(context):**

Applies context properties (color, scale, background) to the box:

```typescript
wrap(context: Context): Box {
  if (context.isPhantom) this.setStyle('opacity', 0);
  
  // Apply color
  if (color && color !== parent.color) 
    this.setStyle('color', color);
  
  // Apply background
  if (backgroundColor && backgroundColor !== parent.backgroundColor) {
    this.setStyle('background-color', backgroundColor);
    this.classes += ' ML__bg';
  }
  
  // Apply scale
  this.scale = context.scalingFactor;
  return this;
}
```

### Inter-Box Spacing

**File:** [src/core/inter-box-spacing.ts](../mathlive/src/core/inter-box-spacing.ts)

After rendering, spacing is inserted between boxes based on their types. The lookup tables are **hardcoded in source code**, directly implementing the TeXBook spacing rules from p.170.

#### The Spacing Tables

The main spacing table maps (previous-type, current-type) → spacing amount:

```typescript
// Values: 3 = \thinmuskip, 4 = \medmuskip, 5 = \thickmuskip
// Empty cells = no spacing
const INTER_BOX_SPACING = {
  ord:   { op: 3, bin: 4, rel: 5, inner: 3 },
  op:    { ord: 3, op: 3, rel: 5, inner: 3 },
  bin:   { ord: 4, op: 4, open: 4, inner: 4 },
  rel:   { ord: 5, op: 5, open: 5, inner: 5 },
  close: { op: 3, bin: 4, rel: 5, inner: 3 },
  punct: { ord: 3, op: 3, rel: 3, open: 3, punct: 3, inner: 3 },
  inner: { ord: 3, op: 3, bin: 4, rel: 5, open: 3, punct: 3, inner: 3 },
};
```

**As a visual table (TeXBook p.170):**

|       | Ord | Op  | Bin | Rel | Open | Close | Punct | Inner |
|-------|-----|-----|-----|-----|------|-------|-------|-------|
| Ord   | 0   | 3   | 4   | 5   | 0    | 0     | 0     | 3     |
| Op    | 3   | 3   | *   | 5   | 0    | 0     | 0     | 3     |
| Bin   | 4   | 4   | *   | *   | 4    | *     | *     | 4     |
| Rel   | 5   | 5   | *   | 0   | 5    | 0     | 0     | 5     |
| Open  | 0   | 0   | *   | 0   | 0    | 0     | 0     | 0     |
| Close | 0   | 3   | 4   | 5   | 0    | 0     | 0     | 3     |
| Punct | 3   | 3   | *   | 3   | 3    | 0     | 3     | 3     |
| Inner | 3   | 3   | 4   | 5   | 3    | 0     | 3     | 3     |

*Legend: 0=none, 3=thin, 4=medium, 5=thick, \*=impossible (converted by type adjustment)*

#### Tight Spacing (Script/Scriptscript Styles)

A reduced table is used in scriptstyle/scriptscriptstyle (most spacing removed):

```typescript
const INTER_BOX_TIGHT_SPACING = {
  ord:   { op: 3 },
  op:    { ord: 3, op: 3 },
  close: { op: 3 },
  inner: { op: 3 },
};
```

#### Muskip Register Values

The spacing amounts (3, 4, 5) reference TeX registers defined in [registers.ts](../mathlive/src/core/registers.ts):

```typescript
const DEFAULT_REGISTERS: Registers = {
  // ...
  'thinmuskip':  { glue: { dimension: 3, unit: 'mu' } },      // 3mu
  'medmuskip':   { 
    glue: { dimension: 4, unit: 'mu' },
    grow: { dimension: 2, unit: 'mu' },
    shrink: { dimension: 4, unit: 'mu' },
  },  // 4mu plus 2mu minus 4mu
  'thickmuskip': { 
    glue: { dimension: 5, unit: 'mu' },
    grow: { dimension: 5, unit: 'mu' },
  },  // 5mu plus 5mu
  // ...
};
```

**Note on `mu` (math unit):** 1 mu = 1/18 em at the current math style size.

#### How Spacing is Applied

```typescript
export function applyInterBoxSpacing(root: Box, context: Context): Box {
  const boxes = root.children;
  
  // First: adjust types (handle unary minus, etc.)
  adjustType(boxes);
  
  // Get actual spacing values from registers (in em)
  const thin = context.getRegisterAsEm('thinmuskip');   // ~0.167em
  const med = context.getRegisterAsEm('medmuskip');     // ~0.222em
  const thick = context.getRegisterAsEm('thickmuskip'); // ~0.278em

  traverseBoxes(boxes, (prev, cur) => {
    if (!prev) return;
    
    // Look up spacing in table
    const table = cur.isTight 
      ? INTER_BOX_TIGHT_SPACING[prev.type]
      : INTER_BOX_SPACING[prev.type];
    const hskip = table?.[cur.type] ?? null;
    
    // Insert skip box before current
    if (hskip === 3) addSkipBefore(cur, thin);
    if (hskip === 4) addSkipBefore(cur, med);
    if (hskip === 5) addSkipBefore(cur, thick);
  });
  
  return root;
}
```

#### Type Adjustment Rules

Before applying spacing, box types are adjusted per TeXBook p.442:

```typescript
function adjustType(boxes: Box[]): void {
  traverseBoxes(boxes, (prev, cur) => {
    // Rule 5: Bin after nothing/Bin/Op/Rel/Open/Punct → Ord
    // Example: "-4" the minus is unary (Ord), not binary
    if (cur.type === 'bin' && 
        (!prev || /^(middle|bin|op|rel|open|punct)$/.test(prev.type)))
      cur.type = 'ord';

    // Rule 6: Bin before Rel/Close/Punct → Ord
    // Example: "1-=" would convert "-" to Ord
    if (prev?.type === 'bin' && /^(rel|close|punct)$/.test(cur.type))
      prev.type = 'ord';
  });
}
```

**Examples of type adjustment:**
- `-4` → minus becomes Ord (unary minus, no spacing before)
- `1-4` → minus stays Bin (binary minus, medium spacing around)
- `(-4)` → minus after Open becomes Ord
- `1-=` → minus before Rel becomes Ord

---

## Key Typesetting Algorithms

### Fractions (GenfracAtom)

**File:** [src/atoms/genfrac.ts](../mathlive/src/atoms/genfrac.ts)

Fractions follow TeXBook Appendix G, Rule 15:

```typescript
render(context: Context): Box | null {
  const metrics = fracContext.metrics;
  
  // 1. Render numerator and denominator in appropriate styles
  const numContext = new Context({ parent: fracContext, mathstyle: 'numerator' });
  const numerBox = Atom.createBox(numContext, this.above);
  
  const denomContext = new Context({ parent: fracContext, mathstyle: 'denominator' });
  const denomBox = Atom.createBox(denomContext, this.below);
  
  // 2. Calculate vertical shifts (Rule 15b)
  if (fracContext.isDisplayStyle) {
    numerShift = numContext.metrics.num1;  // σ8
    denomShift = denomContext.metrics.denom1;  // σ11
    clearance = 3 * ruleThickness;
  } else {
    numerShift = ruleThickness > 0 ? metrics.num2 : metrics.num3;
    denomShift = metrics.denom2;  // σ12
  }
  
  // 3. Adjust for minimum clearance (Rule 15c, 15d)
  // ...
  
  // 4. Build vertical stack with VBox
  const frac = new VBox({
    individualShift: [
      { box: denomBox, shift: denomShift },
      { box: fracLine, shift: -denomLine },  // If has bar line
      { box: numerBox, shift: -numerShift },
    ],
  }).wrap(fracContext);
  
  // 5. Add delimiters if needed
  return new Box([leftDelim, frac, rightDelim], { type: 'inner' });
}
```

### Radicals (SurdAtom)

**File:** [src/atoms/surd.ts](../mathlive/src/atoms/surd.ts)

Follows TeXBook Rule 11:

```typescript
render(context: Context): Box | null {
  // 1. Render inner content in cramped style
  const innerContext = new Context({ parent: context, mathstyle: 'cramp' });
  const innerBox = Atom.createBox(innerContext, this.body);
  
  // 2. Calculate clearance
  const ruleWidth = innerContext.metrics.defaultRuleThickness;
  const phi = context.isDisplayStyle ? X_HEIGHT : ruleWidth;
  const lineClearance = ruleWidth + phi / 4;
  
  // 3. Create surd delimiter of required height
  const minDelimiterHeight = innerBox.height + innerBox.depth + lineClearance + ruleWidth;
  const delimBox = makeCustomSizedDelim('inner', '\\surd', minDelimiterHeight, ...);
  
  // 4. Create the horizontal line
  const line = new Box(null, { classes: 'ML__sqrt-line' });
  line.height = ruleWidth;
  
  // 5. Stack line and inner content
  const stack = new VBox({ ... });
  
  // 6. Combine with surd symbol and optional index
  return new Box([indexBox, delimBox, stack], { ... });
}
```

### Delimiters

**File:** [src/core/delimiters.ts](../mathlive/src/core/delimiters.ts)

Three approaches for delimiter sizing:

1. **Small delimiters** - Use regular font at different mathstyles
2. **Large delimiters** - Use Size1-Size4 fonts
3. **Stacked delimiters** - Assemble from pieces (top, middle, repeat, bottom)

```typescript
// Auto-sizing delimiter (for \left...\right)
function makeLeftRightDelim(
  type: BoxType,
  delim: string,
  height: number,
  depth: number,
  context: Context,
  options: { ... }
): Box {
  const axisHeight = context.metrics.axisHeight;
  const delimiterFactor = 901;  // TeX constant
  const delimiterShortfall = 5.0 / PT_PER_EM;  // TeX constant
  
  const maxHeight = Math.max(height - axisHeight, depth + axisHeight);
  const totalHeight = Math.max(
    maxHeight / 500 * delimiterFactor,
    2 * maxHeight - delimiterShortfall
  );
  
  return makeCustomSizedDelim(type, delim, totalHeight, true, context, options);
}
```

### Superscript/Subscript

**In Atom class:**

```typescript
attachSupsub(parentContext: Context, options: { base: Box; ... }): Box {
  // TeXBook p.445, rules 18(a-f)
  
  // Rule 18a: Render sup/sub in appropriate styles
  if (superscript) {
    const context = new Context({ parent, mathstyle: 'superscript' });
    supBox = Atom.createBox(context, superscript);
    supShift = base.height - context.metrics.supDrop;
  }
  
  if (subscript) {
    const context = new Context({ parent, mathstyle: 'subscript' });
    subBox = Atom.createBox(context, subscript);
    subShift = base.depth + context.metrics.subDrop;
  }
  
  // Rule 18c: Minimum superscript shift
  if (isDisplayStyle) minSupShift = metrics.sup1;
  else if (isCramped) minSupShift = metrics.sup3;
  else minSupShift = metrics.sup2;
  
  // Rule 18e: Both sup and sub - ensure minimum gap
  if (subBox && supBox) {
    const gap = supShift - supBox.depth - (subBox.height - subShift);
    if (gap < 4 * ruleWidth) {
      // Adjust to create minimum gap
      subShift = 4 * ruleWidth - (supShift - supBox.depth) + subBox.height;
    }
  }
  
  // Stack vertically
  return new VBox({ individualShift: [...] });
}
```

### Arrays/Matrices

**File:** [src/atoms/array.ts](../mathlive/src/atoms/array.ts)

Arrays handle:
- Column alignment (left, center, right)
- Row/column separators
- Cell spacing
- Delimiters (for matrix environments)

```typescript
render(context: Context): Box | null {
  // 1. Render all cells
  const rows: ArrayRow[] = [];
  for (let rowIndex = 0; rowIndex < this.cells.length; rowIndex++) {
    const row = this.cells[rowIndex];
    const cells: Box[] = [];
    for (let colIndex = 0; colIndex < row.length; colIndex++) {
      const cellBox = Atom.createBox(context, row[colIndex]);
      cells.push(cellBox);
    }
    rows.push({ cells, height, depth, pos });
  }
  
  // 2. Calculate column widths
  // 3. Apply column alignment
  // 4. Build vertical stack with row gaps
  // 5. Add delimiters if matrix environment
  
  return result;
}
```

---

## Font Metrics

**File:** [src/core/font-metrics.ts](../mathlive/src/core/font-metrics.ts)

Font metrics come from TeX's cmsy fonts. Key metrics (in em):

```typescript
const FONT_METRICS = {
  // [displaystyle, scriptstyle, scriptscriptstyle]
  xHeight: [0.431, 0.431, 0.431],      // σ5 - x-height
  quad: [1.0, 1.171, 1.472],            // σ6 - em width
  
  // Fraction positioning
  num1: [0.5, 0.732, 0.925],            // σ8 - numer shift (display)
  num2: [0.394, 0.384, 0.5],            // σ9 - numer shift (text)
  denom1: [0.686, 0.752, 1.025],        // σ11 - denom shift (display)
  denom2: [0.345, 0.344, 0.532],        // σ12 - denom shift (text)
  
  // Super/subscript positioning
  sup1: [0.413, 0.503, 0.504],          // σ13 - superscript shift
  sub1: [0.15, 0.143, 0.2],             // σ16 - subscript shift
  supDrop: [0.386, 0.353, 0.494],       // σ18 - sup baseline drop
  subDrop: [0.05, 0.071, 0.1],          // σ19 - sub baseline drop
  
  // Other
  defaultRuleThickness: [0.04, 0.049, 0.049],  // ξ8
  axisHeight: [0.25, 0.25, 0.25],       // σ22
};

export const PT_PER_EM = 10.0;
export const AXIS_HEIGHT = 0.25;
export const X_HEIGHT = 0.431;
```

---

## Lessons for Radiant

### Key Architectural Insights

1. **Two-Phase Rendering**
   - First phase: Parse LaTeX → Build Atom tree (semantic)
   - Second phase: Render Atom tree → Build Box tree (geometric)
   - This separation allows re-rendering without re-parsing

2. **Context Chain for Inheritance**
   - Styles, colors, and math styles propagate via parent context
   - Each nested scope creates a new context linked to parent
   - `wrap()` applies accumulated context properties to final box

3. **Math Style System**
   - 8 styles control sizing automatically (display → script → scriptscript)
   - Cramped variants for subscripts/radicals
   - Automatic transitions for fractions, sub/superscripts

4. **TeXBook Algorithms**
   - Follow TeXBook Appendix G for precise layout
   - Font metrics (σ, ξ constants) are essential
   - Inter-box spacing uses lookup tables

### Recommendations for Radiant

1. **Adopt Atom/Box separation**
   - `MathAtom` - semantic representation (type, value, branches)
   - `MathBox` - geometric layout (position, dimensions)

2. **Implement math style tracking**
   - Add `mathstyle` to layout context
   - Support style transitions for fractions, scripts

3. **Port key metrics**
   - Copy font metric tables
   - Implement metric lookups for different styles

4. **Support essential atom types**
   - Priority: fractions, radicals, sub/superscripts, delimiters
   - Later: arrays, accents, operators

5. **Inter-element spacing**
   - Implement box type classification
   - Apply spacing table after layout

### Data Flow Comparison

```
MathLive:
  LaTeX String
    → Tokenizer (tokenizer.ts)
    → Token[]
    → Parser (parser.ts)
    → Atom tree
    → Atom.render() (atom-class.ts)
    → Box tree
    → Box.toMarkup() (box.ts)
    → HTML string

Radiant (proposed):
  LaTeX String
    → input-math.cpp (existing)
    → Mark/Item tree
    → MathBuilder (new)
    → MathAtom tree
    → layout_math() (new)
    → View tree (DomNode)
    → render() (existing)
    → SVG/PDF/PNG
```

### Critical Implementation Details

1. **Vertical stacking (VBox)**
   - Essential for fractions, scripts, radicals
   - Track individual shifts per child
   - Calculate total height/depth

2. **Delimiter sizing**
   - Need glyph variants for different sizes
   - Stacking for very large delimiters
   - Axis-centered positioning

3. **Baseline handling**
   - height = distance above baseline
   - depth = distance below baseline
   - All children positioned relative to baseline

4. **Font switching**
   - Math mode uses different fonts (Main, Math, Size1-4)
   - Text mode uses standard text fonts
   - Variant support (bold, italic, calligraphic)
