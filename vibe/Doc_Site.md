# Lambda Script Website — Design & Structure

## Overview

A static HTML/CSS/JS marketing and documentation website for Lambda Script, served from `./site/`. No build tool, no framework — plain files served by any HTTP server.

**Stack**: HTML5 + CSS3 (custom properties) + vanilla JS  
**Fonts**: Google Fonts — Inter (sans), JetBrains Mono (mono)  
**Theme**: Dark (background `#0f1117`)  
**SEO**: JSON-LD structured data (SoftwareApplication, HowTo)  
**Responsive**: Single breakpoint at 768px  

```
Local preview:  cd site && python3 -m http.server 8090
```

---

## File Structure

```
site/
├── index.html                  # Homepage (479 lines)
├── css/
│   └── style.css               # Design system (699 lines)
├── js/
│   └── main.js                 # Client interactivity (~70 lines)
├── download/
│   └── index.html              # Download & install page
├── docs/
│   ├── index.html              # Documentation hub
│   ├── quickstart/
│   │   └── index.html          # 15-minute getting started tutorial
│   ├── language-tour/
│   │   └── index.html          # Comprehensive language tour
│   └── cli/
│       └── index.html          # Full CLI reference
├── recipes/
│   └── index.html              # Task-oriented recipe cards
├── formats/
│   └── index.html              # Supported formats directory
├── examples/
│   └── index.html              # Code examples gallery
├── community/
│   └── index.html              # Community hub
└── blog/
    └── index.html              # Blog (featured + coming-soon)
```

All pages use clean `/path/` URLs (each is a directory with `index.html`).

---

## Design System (`css/style.css`)

### Color Palette

| Token                  | Value                        | Usage                    |
|------------------------|------------------------------|--------------------------|
| `--color-bg`           | `#0f1117`                    | Page background          |
| `--color-bg-alt`       | `#161822`                    | Alternate section bg     |
| `--color-bg-card`      | `#1c1e2b`                    | Card backgrounds         |
| `--color-bg-code`      | `#1a1c28`                    | Code block backgrounds   |
| `--color-border`       | `#2a2d3e`                    | Default borders          |
| `--color-border-light` | `#363950`                    | Hover borders            |
| `--color-text`         | `#e2e4ed`                    | Body text                |
| `--color-text-muted`   | `#9499b3`                    | Secondary text           |
| `--color-text-dim`     | `#6b7094`                    | Tertiary/labels          |
| `--color-primary`      | `#7c6ef6`                    | Primary purple           |
| `--color-primary-light`| `#9b8fff`                    | Links, highlights        |
| `--color-accent`       | `#38bdf8`                    | Accent blue              |
| `--color-green`        | `#34d399`                    | Success/green accents    |
| `--color-yellow`       | `#fbbf24`                    | Warning                  |
| `--color-red`          | `#f87171`                    | Error                    |

### Typography

- **Headings**: Inter 700, white (`#fff`), tight letter-spacing
- **Body**: Inter 400, `--color-text`, line-height 1.7
- **Code**: JetBrains Mono, `--color-primary-light` on `--color-bg-code`
- **Scale**: h1 2.75rem, h2 2rem, h3 1.4rem, h4 1.15rem, body 1rem

### Spacing Scale

8-step scale via custom properties: `--space-xs` (0.25rem) through `--space-4xl` (6rem).

### Layout

- `--max-width`: 1200px (`.container`)
- `--max-width-narrow`: 800px (`.container-narrow`)
- `--nav-height`: 64px (fixed header)

### Radius

- `--radius-sm`: 6px (buttons, tags)
- `--radius-md`: 10px (code blocks, pipeline steps)
- `--radius-lg`: 16px (cards, panels)

---

## Component Library

### Navigation (`.nav`)
Fixed top bar with backdrop blur (`rgba(15,17,23,0.85)` + `blur(16px)`). Contains logo (`λ Lambda Script`), links, and a highlighted CTA button (Download). Mobile: hamburger toggle replaces links, slides down as a column.

### Hero (`.hero`)
Centered layout with radial gradient glow behind. Contains badge pill (version info), h1 with gradient-highlighted span, subtitle, and 3-button action row (primary/secondary/ghost).

### Feature Grid (`.feature-grid`)
CSS Grid `auto-fit, minmax(320px, 1fr)`. Cards with colored icon squares (purple/blue/green variants), h3, and muted-color description. Hover lifts border color.

### Pipeline Diagram (`.pipeline`)
Flexbox row of step boxes connected by `→` arrows. Each step has an uppercase label and a bold value. Wraps on mobile.

### Stats Row (`.stats-row`)
Grid `auto-fit, minmax(180px, 1fr)`. Large white number + muted label. Falls to 2-column on mobile.

### Code Tabs (`.code-example`)
Card container with a tab bar (`.code-tabs`) and switchable panels (`.code-panel`). Active tab has a purple bottom border. Panels contain `<pre><code>` with inline `<span style>` syntax highlighting.

### OS Tabs (`.os-tabs`)
Pill-style buttons for macOS/Linux/Windows. Active state uses primary bg tint + border. Controls visibility of `.os-panel` blocks.

### Cards (`.card`, `.card-list`)
Grid `auto-fill, minmax(280px, 1fr)`. Cards lift 2px on hover with purple border. Optional `.card-tag` pill in purple/blue/green.

### Tables
Minimal style: left-aligned, `--color-border` bottom borders, uppercase dim headers on alt background. `.benchmark-table` wraps a table in a rounded card.

### Install Command (`.install-command`)
Mono-font bar with green prompt character, command text, and a copy button. Max-width 500px, centered.

### Footer (`.footer`)
4-column grid: brand column (2fr) + 3 link columns (1fr each). Each column has an uppercase dim heading and link list. Bottom bar with copyright. Collapses to 1-column on mobile.

### Doc Pages (`.page-header`, `.doc-content`)
Page header: centered text with nav-height top padding + bottom border. Doc content: padded body with spacing rules for h2/h3 (margins ensure visual hierarchy).

---

## JavaScript (`js/main.js`)

Four behaviors, all initialized on `DOMContentLoaded`:

1. **Mobile nav toggle** — toggles `.open` on `.nav-links`, swaps hamburger ↔ close icon.
2. **Code tab switching** — `data-tab` on buttons maps to `data-panel` on content blocks. Scoped to `.code-example` container.
3. **OS tab switching** — `data-os` buttons control `.os-panel` visibility within the nearest `<section>`.
4. **Copy button** — reads `.cmd` text from `.install-command`, writes to clipboard, shows "Copied!" feedback for 1.5s.
5. **Active nav link** — compares `pathname` against `href` to add `.active` class.

---

## Page Descriptions

### Homepage (`/`)
Landing page. Sections in order:
1. **Hero** — tagline "Documents as Data. One Language. One Pipeline.", version badge, 3 CTAs
2. **Pipeline** — 4-step diagram: Input → Parse → Transform → Output
3. **Stats** — 5 metrics: 13× faster, 100% HTML5, 100% CommonMark, 20+ formats, 9 MB binary
4. **Features** — 6 cards: functional core, JIT perf, multi-format, schema validation, layout engine, interactive viewer
5. **Code Examples** — 5-tab block: Pipes, Markup Literals, Type System, SQL-like Queries, Pattern Matching (inline syntax-highlighted)
6. **Benchmarks** — table comparing Lambda vs Node.js V8 vs QuickJS vs CPython
7. **Standards** — conformance stats (HTML5, CommonMark, CSS)
8. **Use Cases** — 6 card links (data pipeline, doc conversion, schema validation, rendering, interactive viewer, scripting)
9. **Quick Try** — copyable `lambda view` command
10. **Footer** — site links, GitHub, license

Includes `SoftwareApplication` JSON-LD.

### Download (`/download/`)
OS-tabbed install instructions (macOS/Linux/Windows) with copy buttons. Build from source section. Verify installation section. Includes `HowTo` JSON-LD.

### Quick Start (`/docs/quickstart/`)
7-step tutorial: Install → REPL → Pipes → Convert → Validate → Render/View → Pipeline. Ends with "What's Next" card links.

### Docs Hub (`/docs/`)
Card-based navigation organized into sections: Getting Started, Language Reference, Tools & Integration, Quick Reference. Each card links to a subpage or anchor.

### Language Tour (`/docs/language-tour/`)
Comprehensive reference page with anchored sections: data-types, variables, pipes, for-expressions, type-system (objects, element type patterns), functions, pattern-matching (query operator), error-handling, documents (markup literals, multi-format parsing, format conversion).

### CLI Reference (`/docs/cli/`)
Full command reference. Synopsis block, global options table, then per-command sections (run, validate, convert, layout, render, view, fetch, js, REPL) each with flag tables and examples.

### Recipes (`/recipes/`)
Task-oriented card grid in 4 sections: Format Conversion (6 recipes), Data Validation (3), Rendering & Viewing (5), Data Transformation (3). Each recipe shows a `<pre>` command.

### Formats (`/formats/`)
Supported formats directory. Pipeline diagram, 3 input tables (Lightweight Markup: 10, Data Interchange: 7, Other: 9), output format tags (18 conversion + 4 render), Lambda/Mark schema table, parsing code examples.

### Examples (`/examples/`)
7 code examples with source and output: Data Pipeline, Vector Arithmetic, HTML Generation, JSON Processing, Pattern Matching Dispatch, Schema Validation, Markdown→HTML with Custom Layout.

### Community (`/community/`)
4 link cards (GitHub, Issues, Discussions, Releases). Contributing guide (5 steps). Good First Contributions list. License info. Sponsorship CTA. Security disclosure policy.

### Blog (`/blog/`)
Featured article: "Why Lambda Script: Documents as Data" with code example. 4 coming-soon cards (Markdown tutorial, Pandoc comparison, Schema design, ETL patterns).

---

## URL Routing

All paths use directory-style clean URLs:

| URL                    | File                              |
|------------------------|-----------------------------------|
| `/`                    | `site/index.html`                 |
| `/download/`           | `site/download/index.html`        |
| `/docs/`               | `site/docs/index.html`            |
| `/docs/quickstart/`    | `site/docs/quickstart/index.html` |
| `/docs/language-tour/` | `site/docs/language-tour/index.html` |
| `/docs/cli/`           | `site/docs/cli/index.html`        |
| `/recipes/`            | `site/recipes/index.html`         |
| `/formats/`            | `site/formats/index.html`         |
| `/examples/`           | `site/examples/index.html`        |
| `/community/`          | `site/community/index.html`       |
| `/blog/`               | `site/blog/index.html`            |

---

## Shared Assets

Every page links:
- `<link rel="stylesheet" href="/css/style.css">` (homepage uses relative `css/style.css`)
- `<script src="/js/main.js" defer>` (homepage uses relative `js/main.js`)
- Google Fonts: Inter (400–800) + JetBrains Mono (400–500)

---

## Deployment Notes

- **No build step** — serve `site/` directly from any static file host.
- **Compatible with**: GitHub Pages, Cloudflare Pages, Netlify, Vercel, nginx, Apache, `python3 -m http.server`.
- **Missing for production**: favicon, Open Graph meta tags, XML sitemap, 404 page, analytics snippet, image assets.
