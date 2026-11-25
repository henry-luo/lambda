# Upstream graph render corpus

`make test-graph-upstream` builds Lambda and checks 43 SVG renders from 41 source
fixtures. With an existing build, run
`python3 test/lambda/graph/upstream/compare.py`. The test starts the ordinary
`lambda render` command and writes SVGs and a semantic diff under
`temp/graph-upstream/`. It runs offline using the checked-in oracle observations.

The [fixture manifest](sources.json) records the upstream commits, source paths,
PGF manual line ranges, license notices, and SHA-256 hashes. Complete source
files are copied verbatim. The TikZ files contain the corresponding unmodified
`tikzpicture` excerpts. Fixture candidates were checked for duplicates against
`test/input`, `test/lambda`, and `test/render` using content with whitespace
removed.

## Observations

| Format | Source files | SVGs | Checked output |
|---|---:|---:|---|
| D2 | 8 | 8 | Node names and labels, directed and undirected connections, parallel edges, edge labels, and horizontal order |
| DOT | 9 | 9 | Node and edge identities, named ports, edge labels, cluster labels, and authored cluster stroke and dashes |
| Mermaid | 10 | 10 | Node identities, flowchart and class edges, markers, labels, subgraph endpoints, selected class fills, and state transition count |
| Structurizr | 7 | 9 | View selection by key, projected node names, relationships, and static or dynamic relationship labels |
| TikZ | 7 | 7 | Line lengths and curved subpaths, dash lengths and phase, stroke colors, and even-odd fills |

The graph check uses the semantic structure of each rendered SVG and considers
engine-specific geometry only where the fixture has a specific invariant. For
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
