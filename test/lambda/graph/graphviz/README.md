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

Upstream grammar reference: `rydesun/tree-sitter-dot` at
`80327abbba6f47530edeb0df9f11bd5d5c93c14d`.
