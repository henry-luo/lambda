# Upstream graph render corpus

`make test-graph-upstream` builds Lambda and checks 111 SVG renders from 102 source
fixtures. With an existing build, run
`python3 test/lambda/graph/upstream/compare.py`. The test starts the ordinary
`lambda render` command and writes SVGs and a semantic diff under
`temp/graph-upstream/`. It runs offline using the checked-in oracle observations.

The [fixture manifest](sources.json) records the upstream commits, source paths,
PGF manual line ranges, license notices, and SHA-256 hashes. Complete source
files are copied verbatim. The TikZ files contain the corresponding unmodified
`tikzpicture` excerpts. The expansion added 61 sources and 68 renders.
It checked all new sources against one another and 1,032 existing source
files under `test`, `lambda-input/test`, `radiant/test` and `lambda-test`,
using content with whitespace removed. For TeX files the scan also indexed
individual `tikzpicture` bodies.

## Observations

| Format | Source files | SVGs | Checked output |
|---|---:|---:|---|
| D2 | 16 | 16 | Node and edge identities and labels, direction and rank order, parallel edges, selected stroke and opacity, and thick-edge arrow dimensions |
| DOT | 30 | 30 | Node and edge identities, ports, visible node labels, edge labels and annotations, and cluster labels and paint |
| Mermaid | 29 | 29 | Node identities and selected labels, flowchart and class edges, markers, edge labels, cluster endpoints and border attachment, selected class fills, and state transition count |
| Structurizr | 15 | 24 | View selection by key, projected node names, relationships, static and parallel dynamic labels, and selected deployment boundaries |
| TikZ | 12 | 12 | Line lengths and curved subpaths, rectangle and polygon closure, dash lengths and phase, stroke colors, and even-odd fills |

The graph check uses the semantic structure of each rendered SVG and considers
engine-specific geometry only where the fixture has a specific invariant.
The manifest specifies where to compare authored paint, rank order or
rendered node labels. For
TikZ it converts PDF points to CSS pixels, tolerates 0.15 px of measurement
error, and allows a one-unit rounding difference per RGB channel. The
comparison viewport is 2600 × 2300 CSS px to fit the entire test diagram. The
checker verifies that the graph bounds fit the exported SVG and that the
Mermaid compound edge meets its cluster border.
[Graph conformance design §13](../../../../vibe/Lambda_Pkg_Graph.md#13-conformance-model)

## Regenerate the reference observations

Use D2 0.7.1, Structurizr CLI 2025.11.09 libraries, and the Mermaid 11.16.0
lockfile already present in this tree. Install `@viz-js/viz@3.28.0` for
Graphviz 15.0.0. The recorded run used Chromium headless shell 146.0.7680.153,
pdfTeX 1.40.27 from TeX Live 2025 with PGF 3.1.10, and Poppler 26.04.0.
The Structurizr Java driver uses the official DSL parser and DOT exporter.
Graphviz renders that DOT and the independent DOT fixtures.

Run from the repository root with the release tools available as local files.

```sh
mkdir -p temp/graph-tools
export TMPDIR="$PWD/temp/graph-tools"
cp test/lambda/graph/mermaid/reference/package*.json temp/graph-tools/
PUPPETEER_SKIP_DOWNLOAD=1 npm --prefix temp/graph-tools ci --cache "$PWD/temp/graph-tools/npm-cache"
PUPPETEER_SKIP_DOWNLOAD=1 npm --prefix temp/graph-tools install --cache "$PWD/temp/graph-tools/npm-cache" @viz-js/viz@3.28.0
python3 test/lambda/graph/upstream/reference/render.py \
  --d2 /path/to/d2 --java /path/to/java --structurizr /path/to/structurizr-cli \
  --node-tools temp/graph-tools --chrome /path/to/chrome-headless-shell
python3 test/lambda/graph/upstream/compare.py --capture-reference
```

The second Python command reads the upstream SVGs into
[`expected.json`](expected.json), then compares current Lambda output.
Review changes to the saved observations whenever a fixture or renderer changes.
