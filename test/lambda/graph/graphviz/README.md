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

`reference_semantics.ls` compares canonical Lambda semantics with a checked-in
Graphviz `dot_json` reference through Graph Scene Mark; reference provenance is
recorded under `reference/`. `content_shapes_markers.ls` covers plain-label
substitutions and literal backslashes, shape aliases/families, endpoint marker
direction, semantic HTML metadata, and generated SVG marker geometry.
`html_interactions.ls` covers sanitized Graphviz HTML-like table content and
inert node/edge links and tooltips. `records_ports.ls` covers measured flat
record fields, canonical named ports, semantic HTML port metadata, and routed
attachment offsets. `rank_layout.ls` covers `rank=same`, `constraint=false`,
edge weight propagation, successor relaxation, and the four boundary rank
classes. `annotations.ls` covers measured node/edge `xlabel`, `headlabel`, and
`taillabel` metadata and placement. `render.ls` exercises the retained custom
layout callback through final SVG and Graph Scene adaptation.
`route_classes.ls` covers all supported Graphviz `splines` values and aliases,
invalid-value diagnostics, semantic HTML propagation, route geometry, hidden
edges, and deterministic curved SVG paths.

`manifest.mark` and `suite.ls` provide one retained-runtime pass over the
positive DOT corpus. Run the native parser checks and all package fixtures with
`make test-graph-graphviz`.

Upstream grammar reference: `rydesun/tree-sitter-dot` at
`80327abbba6f47530edeb0df9f11bd5d5c93c14d`.
