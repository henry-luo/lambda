# Graphviz DOT parser fixtures

`grammar.dot` combines the official DOT grammar forms and the cases exercised
by the pinned `tree-sitter-dot` corpus used in the parser-size experiment.
`parser.ls` compares source-stage Graph Mark semantics rather than parser trees
or rendered pixels. `recovery.dot` verifies that malformed statements produce
structured diagnostics while later statements remain available.

`canonical.ls` covers source-ordered defaults, implicit and redeclared node
identity, endpoint-set expansion, strict edge updates, visual clusters,
semantic scopes, rank constraints, typed attributes, and property provenance.
`semantics.ls` covers invalid compass/operator/rank/engine diagnostics and
unordered strict-edge identity.

`reference_semantics.ls` compares canonical Lambda semantics and selected
tolerant rendered geometry with a checked-in Graphviz `dot -Tjson` reference
through Graph Scene Mark; reference provenance and coordinate conversion are
recorded under `reference/`. `content_shapes_markers.ls` covers plain-label
substitutions and literal backslashes, shape aliases/families, endpoint marker
direction, semantic HTML metadata, and generated SVG marker geometry.
`specialized_shapes.ls` covers the biological/specialized Graphviz shape names,
the layered `fixedsize=shape` box, and clipping routes to that authored shape
when label content makes the outer node larger. `paint_styles.ls` covers safe
palette indices, qualified SVG colors, weighted stripes/wedges, multi-stop
radial paint, unsupported-scheme diagnostics, and PostScript font fallback.
`html_interactions.ls` covers sanitized Graphviz HTML-like table content and
inert node/edge links and tooltips. `records_ports.ls` covers measured flat
record fields, canonical named ports, semantic HTML port metadata, and routed
attachment offsets. `rank_layout.ls` covers `rank=same`, `constraint=false`,
edge weight propagation, successor relaxation, and the four boundary rank
classes. `annotations.ls` covers measured node/edge `xlabel`, `headlabel`, and
`taillabel` metadata, normalized font styling, and collision-free placement
against nodes, other edge routes, and other labels. `render.ls` exercises the retained custom layout callback
through final SVG and Graph Scene adaptation.
`route_classes.ls` covers all supported Graphviz `splines` values and aliases,
invalid-value diagnostics, semantic HTML propagation, route geometry, hidden
edges, and deterministic curved SVG paths.
`formatter.ls` covers source-stage formatter idempotence and canonical semantic
round trips, including endpoint sets, scoped defaults, quoted/HTML/numeric IDs,
and Graphviz backslash label escapes.

`manifest.mark` and `suite.ls` provide one retained-runtime pass over the
positive DOT corpus. Reference paths, geometry tolerances, and comparison flags
are declared in the manifest rather than embedded in the runner. Run the native
parser checks and all package fixtures with `make test-graph-graphviz`.
That target also opens `view.gv` through the CLI's in-memory graph bridge in
headless mode, covering `.gv` extension dispatch without creating a window.
Manifest discovery, comparison defaults, retained render execution, and scene
sanity checks are shared with Mermaid through
`lambda.package.graph.conformance`; Graphviz keeps only its JSON adapter and
format-specific case summaries.

Upstream grammar reference: `rydesun/tree-sitter-dot` at
`80327abbba6f47530edeb0df9f11bd5d5c93c14d`.
