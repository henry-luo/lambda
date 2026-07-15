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

Upstream grammar reference: `rydesun/tree-sitter-dot` at
`80327abbba6f47530edeb0df9f11bd5d5c93c14d`.
