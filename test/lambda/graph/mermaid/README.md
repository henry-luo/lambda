# Mermaid Graph Corpus

This suite contains Lambda-authored regression cases and source-text fixtures
adapted from Mermaid. Adapted cases are pinned to the revision in
`UPSTREAM_COMMIT`, retain their source file and test name in `manifest.mark`,
and are distributed under `LICENSE.mermaid`.

Ordinary tests read only checked-in Mermaid source and semantic Mark
expectations. They do not install Mermaid, launch a browser, compare images, or
rewrite references. Reference generation is a separate maintenance operation.

`reference/mermaid_svg_adapter.mjs` is that maintenance boundary. Import
`adaptMermaidSvg()` in Mermaid's browser test environment after `mermaid.render`
has produced an SVG DOM, then pass the result to `formatGraphSceneMark()`.
The adapter flattens SVG transforms and strips renderer-specific element names,
classes, wrappers, and CSS while retaining graph identities, topology, labels,
shapes, markers, bounds, and sampled routes.

Case policy values are:

- `ir`: compare source-stage parser semantics.
- `scene-semantic`: render through Lambda/Radiant and compare Graph Scene
  identities, topology, labels, shapes, markers, and route classes.
- `scene-geometry`: additionally compare explicit geometry using the tolerances
  recorded by the case.

`baseline`, `extended`, `unsupported`, and `invalid` status values remain
visible as named GTest cases; no manifest entry is silently skipped.

The adapted source corpus is reproducible from Mermaid's pinned revision:

```bash
cd test/lambda/graph/mermaid/reference
npm ci
npm run extract
```

`npm run extract` parses the upstream JavaScript tests with Acorn and verifies
all 18 checked-in Mermaid sources. `npm run extract:write` is an explicit
maintenance command that rewrites them from the pinned checkout.

Browser references are also maintenance-only:

```bash
npm run references
npm run references:write
```

The first command renders the selected cases with pinned Mermaid and Puppeteer,
adapts the live SVG DOM into Graph Scene Mark, and checks the reviewed semantic
fixtures. The second command updates generated reference artifacts and must be
reviewed. Ordinary `make test-graph-mermaid` runs only checked-in fixtures and
uses one retained Lambda runtime for all `scene-semantic` manifest cases.
