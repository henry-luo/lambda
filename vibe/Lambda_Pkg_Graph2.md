# Lambda Graph Package 2 - Structurizr DSL and C4 Diagrams

**Status:** Draft proposal

## 1. Summary

This proposal extends `lambda.package.graph` with native Structurizr DSL and C4
diagram support. Structurizr is a workspace language: one architecture model
defines many views, and each view projects a different diagram from the same
elements and relationships. The implementation must therefore preserve the
workspace model instead of lowering the input immediately to one graph.

The proposed pipeline is:

```text
Structurizr DSL
  -> manual C/C+ parser
  -> source-faithful Structurizr Mark
  -> pure Lambda semantic normalization
  -> canonical C4 Workspace Mark
  -> selected C4 view projection
  -> canonical Graph IR
  -> semantic HTML
  -> retained Radiant custom layout and SVG paint
```

The primary supported view families are:

- system landscape;
- system context;
- container;
- component;
- filtered;
- dynamic collaboration;
- deployment;
- custom C4-compatible views.

Structurizr DSL is based on the C4 model and explicitly separates the model
from its views. The official language also supports view expressions, styles,
themes, groups, deployment instances, archetypes, includes, workspace
extension, scripts, and plugins. This proposal stages those features so the
trusted declarative core is useful before executable or external extensions are
considered.

## 2. Goals

1. Parse useful Structurizr workspaces without Java, Node.js, Structurizr CLI,
   Graphviz, or a browser dependency.
2. Use a compact manual C/C+ parser and actively track its physical LOC and
   release-binary contribution.
3. Reuse existing graph parsing infrastructure instead of creating a parallel
   lexer, source tracker, diagnostics system, or Mark builder.
4. Preserve source order, scopes, identifiers, directives, and unknown
   extensions before semantic resolution.
5. Represent the architecture once as canonical C4 Workspace Mark and project
   any number of views from it.
6. Reuse the existing canonical Graph IR, semantic HTML transform, Velmt
   measurement, retained layout callback, routing, paint, and Graph Scene tests.
7. Compare model and view semantics rather than pixels or renderer-specific SVG.
8. Keep parsing and normalization deterministic, side-effect free, and safe for
   untrusted input.

## 3. Non-goals

The first release does not attempt to:

- reproduce Structurizr's automatic layout pixel for pixel;
- execute `!script`, `!plugin`, JVM code, or custom implied-relationship code;
- fetch remote workspaces, includes, themes, icons, or images during parsing;
- render image views backed by PlantUML, Mermaid, Kroki, or external services;
- preserve comments and whitespace for byte-identical source formatting;
- implement the full Structurizr cloud/server API;
- store manual diagram coordinates from the Structurizr JSON format;
- make sequence layout part of generic graph layout.

Unsupported constructs must remain represented in source Mark and produce
stable diagnostics. They must not execute or silently disappear.

## 4. Why C4 Belongs in the Graph Package

Static C4 diagrams are topology-oriented. Their defining operations are model
projection, visual containment, relationship routing, boundary layout, and
overlap avoidance. These map directly to the graph package:

- people, systems, containers, components, and instances become graph nodes;
- groups, software-system boundaries, container boundaries, and deployment
  nodes become graph clusters;
- model relationships become graph edges;
- view inclusion and exclusion select the graph projection;
- `autoLayout` maps to layered graph direction and separation constraints;
- relationship routing maps to the existing direct, orthogonal, and curved
  route classes.

A Structurizr dynamic view can be rendered as either a collaboration diagram or
a sequence diagram. The collaboration form belongs in the graph package and
uses ordered relationship instances. A sequence rendering remains a specialized
ordered-axis layout and should be delegated to `lambda.package.chart.sequence`
when that package exists. The canonical dynamic-view IR must preserve enough
order and parallel-sequence information for both projections.

## 5. Three-Level Data Model

### 5.1 Source Structurizr Mark

The manual parser emits source-stage Mark. It records syntax, not resolved C4
semantics.

```lambda
<workspace flavor: "structurizr", ir-stage: "source",
    source-start: 0, source-end: 412>
    <model>
        <declaration keyword: "person", identifier: "user",
            statement-index: 0>
            <argument kind: "quoted", value: "User">
        </declaration>
        <declaration keyword: "softwareSystem", identifier: "shop",
            statement-index: 1>
            <argument kind: "quoted", value: "Online shop">
            <declaration keyword: "container", identifier: "web">
                <argument kind: "quoted", value: "Web application">
            </declaration>
        </declaration>
        <relationship from: "user", to: "web", statement-index: 2>
            <argument role: "description", value: "Uses">
        </relationship>
    </model>
    <views>
        <view kind: "container", scope: "shop", key: "Containers">
            <include expression: "*">
            <auto-layout direction: "lr">
        </view>
    </views>
</workspace>
```

Every source element carries `source-start`, `source-end`, `source-line`, and
`source-column`. Statement order is explicit. Quoted and bare arguments remain
distinguishable where that affects source fidelity. Unknown statements use a
generic `<statement>` with their keyword, arguments, body, and source span.

The parser does not:

- resolve identifiers;
- apply flat or hierarchical identifier rules;
- infer parent elements from names;
- create implied relationships;
- evaluate include/exclude expressions;
- expand archetypes;
- select view contents;
- apply style or theme cascades.

### 5.2 Canonical C4 Workspace Mark

Pure Lambda normalization transforms source Mark into a renderer-neutral C4
workspace.

```lambda
<c4-workspace name: "Shop" ir-stage: "canonical">
    <c4-model>
        <c4-element id: "e1", identifier: "user", kind: "person",
            name: "User">
            <description>...</description>
            <tag name: "Element">
            <tag name: "Person">
        </c4-element>
        <c4-element id: "e2", identifier: "shop",
            kind: "software-system", name: "Online shop">
        <c4-element id: "e3", identifier: "shop.web", parent: "e2",
            kind: "container", name: "Web application">
        <c4-relationship id: "r1", source: "e1", destination: "e3",
            description: "Uses">
    </c4-model>
    <c4-views>
        <c4-view key: "Containers", kind: "container", scope: "e2">
            <include kind: "wildcard", value: "*">
            <auto-layout direction: "lr", rank-separation: 300,
                node-separation: 300>
        </c4-view>
    </c4-views>
    <c4-styles>...</c4-styles>
    <diagnostics>...</diagnostics>
</c4-workspace>
```

Canonical IDs are generated deterministically and are separate from authored
identifiers. Authored identifiers and source spans remain available for
diagnostics, formatting, editor navigation, and semantic round trips.

### 5.3 Per-view Canonical Graph IR

View projection lowers one `<c4-view>` to the existing Graph IR:

```lambda
<graph flavor: "structurizr-c4", ir-stage: "canonical",
    diagram-type: "container", source-view-key: "Containers",
    direction: "left-to-right">
    <cluster id: "e2", role: "software-system-boundary">
        <label>Online shop</label>
        <node id: "e3", role: "c4-container", c4-kind: "container">
            <content>...</content>
        </node>
    </cluster>
    <node id: "e1", role: "c4-person", c4-kind: "person">
        <content>...</content>
    </node>
    <edge id: "r1", from: "e1", to: "e3">
        <label>Uses</label>
    </edge>
</graph>
```

The projected graph preserves C4 identity, kind, parent, tags, technology,
description, source relationship identity, view key, and workspace provenance.
Generic layout code must not need to understand Structurizr syntax.

## 6. Manual C/C+ Parser

### 6.1 Location and dispatch

The parser is implemented in:

```text
lambda/input/input-graph-structurizr.cpp
```

with the public entry point:

```cpp
void parse_graph_structurizr(Input* input, const char* source);
```

`parse_graph()` accepts the `structurizr` and `c4` flavors. CLI dispatch accepts
`.dsl` as Structurizr only when explicitly selected or when conservative content
detection finds a top-level `workspace` construct. A `.structurizr` extension
may be added as an unambiguous convenience. Generic `.dsl` must not be claimed
without detection because it is not unique to Structurizr.

### 6.2 Existing infrastructure to reuse

The parser reuses:

- `InputContext` for parse lifetime and error collection;
- `SourceTracker` for byte offsets, lines, columns, peeking, and bounded reads;
- `MarkBuilder` for all source Mark allocation;
- `parse_shared_quoted_string()` from input parsing utilities;
- `skip_wsc()` for whitespace and `//` comments;
- `read_graph_identifier()` where Structurizr identifier rules match;
- `graph_set_source_span()` for source metadata;
- `graph_append_diagnostics()` for structured diagnostics;
- graph child-append and integer/string attribute helpers;
- existing string, string-buffer, arena, and collection types from `lib/`.

Before adding a Structurizr-local lexical helper, the implementation must search
DOT, Mermaid, D2, `input-utils`, and `SourceTracker`. If the same lexical shape
already exists, promote a narrowly parameterized helper to `input-graph.h` or
`input-utils.hpp` rather than copying it.

Likely shared extractions from the current DOT parser are:

- boundary-aware keyword matching;
- optional-whitespace punctuation consumption;
- bounded generated source IDs;
- quoted-or-bare argument reading;
- balanced block recovery;
- source-spanned generic statement construction.

These helpers must remain format-neutral. Structurizr semantic names and
context rules stay in the Structurizr parser.

### 6.3 Parsing strategy

Use a small hand-written recursive-descent parser over nested brace blocks.
Structurizr statements are mostly:

```text
[identifier =] keyword argument... [{ child statements }]
source -> destination argument... [{ relationship properties }]
!directive argument... [{ child statements }]
```

The parser keeps a context enum such as `WORKSPACE`, `MODEL`, `ELEMENT`,
`DEPLOYMENT`, `VIEWS`, `VIEW`, `STYLES`, `STYLE`, and `GENERIC_BLOCK`. A compact
table maps known keywords to statement kinds and allowed contexts. One generic
statement parser handles arguments, optional assignment, optional relationship
operator, and optional body. Small specialized handlers are reserved for forms
that are structurally different, not merely for different keywords.

This avoids one parser function per DSL keyword and keeps LOC proportional to
grammar shapes rather than vocabulary size.

### 6.4 Parser responsibilities

The parser is responsible for:

- comments, strings, bare tokens, assignments, arrows, braces, and newlines;
- preserving nested blocks and source statement order;
- distinguishing declarations, relationships, directives, and view statements;
- retaining optional arguments without guessing omitted semantic roles;
- bounded error recovery at the next newline or balanced closing brace;
- maximum nesting, token length, statement count, and input-size checks;
- source-spanned syntax diagnostics;
- preserving unsupported and unknown statements.

The parser is not responsible for:

- identifier lookup or type validation;
- legal parent/child C4 combinations;
- archetype inheritance;
- view keys or default-view generation;
- include/exclude expression evaluation;
- relationship legality or implication;
- style resolution;
- filesystem or network access.

### 6.5 Parser size budget

Physical LOC is an acceptance metric, not an informal preference.

| Unit | Initial target | Hard review threshold |
|---|---:|---:|
| `input-graph-structurizr.cpp` core parser | <= 800 | 1,000 |
| shared graph-parser helper growth | <= 100 | 150 |
| parser-specific native tests | tracked separately | no production padding |

Current implementation measurement (2026-07-15):

| Unit | Physical LOC | Budget result |
|---|---:|---|
| `input-graph-structurizr.cpp` | 341 | passes the 800-line target |
| `input-graph.cpp` growth | 2 | dispatch only |
| `input-graph.h` growth | 1 | parser declaration only |

`GraphParserTest.ParserLocBudget` enforces the parser and aggregate graph-parser
ceilings. Release object, executable, and compressed-artifact measurements are
still required before Stage 4E closes.

The threshold is not met by compressing multiple statements onto one line or
removing useful root-cause comments. Crossing it requires a design review of
duplicated context handlers, keyword switches, and lexical helpers. A native
`ParserLocBudget` test records the limit alongside the existing DOT and Mermaid
parser ledgers.

Release builds record the before/after sizes of:

- the Structurizr parser object file;
- shared graph parser objects;
- stripped `lambda.exe`;
- compressed distribution artifact, when available.

## 7. Structurizr Semantic Normalization

The Lambda package adds:

```text
lambda/package/graph/structurizr/
  structurizr.ls        public facade
  normalize.ls          source Mark to canonical workspace
  model.ls              identifiers, hierarchy, elements, relationships
  archetypes.ls         declarative archetype expansion
  expressions.ls        restricted include/exclude expression evaluator
  views.ls              C4 view projection rules
  styles.ls             tags, style cascade, terminology, themes
  deployment.ls         environments, nodes, and instances
  dynamic.ls            ordered relationship instances
  transform.ls          selected view to canonical Graph IR
  schema.ls             source and canonical Mark validation
```

Modules may be combined while small. The third near-identical semantic path
must be extracted before a new file or copied branch is added.

### 7.1 Identifier resolution

Normalization supports flat and hierarchical identifiers. Each scope records
authored identifiers, canonical IDs, parent identity, and declaration source.
The special `this` identifier resolves to the current element. Duplicate,
unknown, ambiguous, and illegal cross-scope references produce diagnostics with
both use and declaration provenance where available.

Resolution happens after parsing so declarations can remain source-faithful and
the same parser can support workspace extension and includes later.

### 7.2 C4 element hierarchy

The canonical model supports:

- person;
- software system;
- container;
- component;
- custom element;
- group;
- deployment environment and group;
- deployment node;
- infrastructure node;
- software-system instance;
- container instance.

Normalization validates legal containment. Instance elements reference their
logical model element rather than copying its identity or metadata.

### 7.3 Relationships

Relationships preserve source, destination, description, technology, tags,
properties, URL, and perspectives. Explicit relationship identifiers remain
stable so dynamic views can reference them.

Boolean implied relationships are supported after explicit relationships are
resolved. Custom Java implied-relationship strategies are diagnosed as
unsupported and never loaded. Duplicates created through implication retain
provenance and are normalized deterministically.

### 7.4 Archetypes

Declarative element and relationship archetypes may provide default
description, technology, tags, properties, and perspectives. Expansion is a
pure merge with explicit instance values taking precedence. Cycles and unknown
base archetypes are errors. Executable archetype extensions are out of scope.

### 7.5 Includes, extension, and external directives

The parser always records `!include` and `workspace extends`. Resolution is a
separate host operation with an explicit policy:

- local files are opt-in and resolved relative to the containing source;
- include roots can be restricted by the caller;
- cycles, depth, file count, and total bytes are bounded;
- directory expansion is deterministic and sorted;
- network URLs are disabled by default;
- included sources retain their own file identity and source spans;
- JSON workspace extension is deferred until a canonical Structurizr JSON
  adapter exists.

`!script`, `!plugin`, `!components`, custom-code `!impliedRelationships`, and
other executable directives remain inert source nodes with
`structurizr.unsafe-directive` diagnostics.

## 8. View Projection

### 8.1 Static C4 views

The projection rules follow view semantics rather than merely filtering by
element type:

| View | Primary contents |
|---|---|
| system landscape | people and software systems |
| system context | scoped system, connected people, and connected systems |
| container | containers in the scoped system plus connected external elements |
| component | components in the scoped container plus connected external elements |
| filtered | tag-based projection over a resolved base static view |
| custom | explicitly selected custom elements and relationships |

`include *`, reluctant `*?`, explicit identifiers, and supported expressions
are evaluated against the model and view scope. Relationships are included only
when their selected endpoints and the view rules permit them. Exclusion runs
after inclusion in source order according to Structurizr semantics.

### 8.2 Restricted expression evaluator

View expressions are parsed into a small Mark expression AST. They are not
evaluated as Lambda source. The initial allowlist covers the official element
and relationship predicates needed by the corpus, including type, tag, parent,
scope, source, destination, and relationship properties. Boolean composition
is added only with explicit grammar and tests.

Unknown predicates produce `structurizr.unsupported-expression`; malformed
expressions produce `structurizr.invalid-expression`. Neither falls back to
including everything.

### 8.3 Dynamic views

A dynamic view contains ordered instances of existing model relationships.
Canonical entries preserve:

- sequence order;
- optional explicit order token;
- relationship identity or source/destination reference;
- per-instance description and technology override;
- parallel sequence grouping;
- source span.

The graph projection initially renders a collaboration diagram. Edge labels
include stable sequence ordinals, and parallel occurrences remain separate
edges. A future sequence projection consumes the same canonical entries through
`lambda.package.chart.sequence`.

### 8.4 Deployment views

Deployment projection maps nested deployment nodes to clusters and
infrastructure/system/container instances to nodes. Logical relationships are
lifted to the selected instances only when the deployment environment and view
scope allow them. Instance multiplicity is preserved; two container instances
must not collapse to one logical node.

Deployment groups constrain relationship applicability and remain explicit in
the canonical workspace even when they have no visible boundary.

### 8.5 View selection API

The public Lambda API is pure:

```lambda
structurizr.normalize(source_workspace)
structurizr.view_keys(workspace)
structurizr.project(workspace, view_key)
structurizr.project_all(workspace)
structurizr.to_html(workspace, view_key)
```

`project()` returns either canonical Graph IR or structured diagnostics. It
does not install the layout callback or render. Existing graph transform APIs
remain responsible for HTML and retained Radiant rendering.

CLI behavior should be:

```text
lambda view workspace.dsl --view SystemContext
lambda render workspace.dsl --view Containers -o containers.svg
lambda convert workspace.dsl -t mark
```

When a workspace has exactly one renderable view, `--view` may be omitted.
Multiple views without an explicit or declared default produce a diagnostic and
list stable view keys rather than choosing by source accident.

## 9. C4 HTML and Rendering

The existing semantic graph vocabulary remains the rendering boundary:

```html
<graph class="lambda-graph c4-diagram"
       data-radiant-layout="lambda-graph"
       data-c4-view="Containers">
  <node class="graph-node c4-person" data-node-id="e1">...</node>
  <cluster class="graph-cluster c4-software-system-boundary">...</cluster>
  <edge data-from="e1" data-to="e3" data-directed="true"></edge>
</graph>
```

C4 node content is rich measured HTML containing the element name, type or
configured terminology, optional technology, and optional description. It is
not flattened into one measured text string. Tags, properties, URLs, and
perspectives are metadata unless an allowlisted transform explicitly displays
them.

Default roles include:

- `c4-person`;
- `c4-software-system`;
- `c4-container`;
- `c4-component`;
- `c4-deployment-node`;
- `c4-infrastructure-node`;
- `c4-instance`;
- `c4-group-boundary`;
- `c4-software-system-boundary`;
- `c4-container-boundary`.

Structurizr element and relationship styles lower through the existing safe
style layer. Unsupported shapes, icons, fonts, or paint values preserve their
source property and produce deterministic warnings. External icons are not
fetched during transform or render.

## 10. Layout Mapping

`autoLayout [tb|bt|lr|rl] [rankSeparation] [nodeSeparation]` maps directly to
Graph IR direction, rank separation, and node separation. Structurizr specifies
the two separations in pixels, matching the graph layout's CSS-pixel geometry.

The layout must additionally respect:

- nested C4 boundaries;
- boundary labels and padding;
- fixed view membership;
- relationship labels;
- direct, orthogonal, and curved routing hints;
- multiple relationship instances;
- deployment instance identity;
- stable source order as the final tie breaker.

Views without `autoLayout` use a deterministic source-order layered fallback
and emit `structurizr.implicit-layout`. Future Structurizr JSON import may carry
manual coordinates through a separate fixed-layout mode.

## 11. Diagnostics

Stable diagnostic codes include:

| Code | Condition |
|---|---|
| `structurizr.syntax` | malformed token, statement, or block |
| `structurizr.unknown-statement` | statement is preserved but not understood |
| `structurizr.invalid-context` | known statement appears in an illegal parent |
| `structurizr.duplicate-identifier` | identifier conflicts in its effective scope |
| `structurizr.unresolved-identifier` | model or view reference cannot be resolved |
| `structurizr.invalid-containment` | C4 element appears under an illegal parent |
| `structurizr.invalid-relationship` | source and destination kinds are incompatible |
| `structurizr.invalid-view-scope` | view scope does not match its view kind |
| `structurizr.duplicate-view-key` | two canonical views have the same key |
| `structurizr.invalid-expression` | include/exclude expression is malformed |
| `structurizr.unsupported-expression` | expression is valid but outside the allowlist |
| `structurizr.unsupported-view` | image or another deferred view is requested |
| `structurizr.unsafe-directive` | executable or external extension remains inert |
| `structurizr.include-cycle` | local include graph contains a cycle |
| `structurizr.include-limit` | include depth, file count, or byte limit is exceeded |
| `structurizr.unsupported-style` | style value cannot be represented safely |
| `structurizr.implicit-layout` | deterministic fallback layout was selected |

## 12. Testing and Conformance

Tests live under:

```text
test/lambda/graph/structurizr/
  manifest.mark
  README.md
  cases/
    parser/
    model/
    views/
    dynamic/
    deployment/
    styles/
    invalid/
  expected/
    source/
    workspace/
    graph/
    scene/
  reference/
    STRUCTURIZR_VERSION
    LICENSE
    generate_workspace_refs.*
    structurizr_json_adapter.ls
```

### 12.1 Native parser tests

Native tests cover lexical forms, assignments, relationships, nested blocks,
directives, recovery, source spans, depth/token limits, and parser LOC. The
parser suite compares source-stage Mark and never treats successful allocation
as sufficient parsing coverage.

### 12.2 Lambda semantic tests

Lambda tests cover identifier modes, hierarchy, `this`, archetypes, implied
relationships, instances, view membership, expressions, style cascade,
dynamic order, deployment lifting, and canonical Graph IR projection. Every new
`.ls` test has a paired `.txt` result.

### 12.3 Official semantic reference

Maintenance tooling may run a pinned Structurizr CLI release to export a DSL
workspace to Structurizr JSON. A Lambda adapter converts that JSON and Lambda's
canonical workspace to the same semantic comparison Mark.

Comparison checks:

- element identity, kind, parent, name, description, technology, and tags;
- explicit and implied relationships;
- deployment environments, nodes, and instances;
- view kind, scope, key, and selected membership;
- dynamic relationship order and parallel groups;
- resolved element and relationship styles;
- projected graph identity and containment;
- tolerant geometry relations only where layout is under test.

Ordinary tests use checked-in semantic Mark, not images, raw SVG, Java, or the
Structurizr CLI. Reference provenance records the exact CLI version and source
case. The existing `lambda.package.graph.conformance` runner is extended rather
than cloned.

### 12.4 Test command

```text
make test-graph-structurizr
```

The current target runs native parser/LOC tests and Lambda semantic fixtures.
Stage 4E extends the same target with retained end-to-end Radiant scene tests
and one headless `.dsl` CLI test.

## 13. Implementation Stages

### 13.1 Current checkpoint

The first executable slice is implemented and covered by
`make test-graph-structurizr`:

- explicit `structurizr` and `c4` flavor dispatch to a 341-line manual parser;
- source-ordered workspace/model/views Mark with spans, recovery, resource
  limits, inert generic statements, and preserved style color tokens;
- pure normalization of hierarchical IDs, core C4 elements, relationships,
  deployment declarations/instances, tags, static view metadata, and styles;
- system-context and container projection, with shared landscape/component
  selection paths, C4 rich node content, software-system boundaries, and
  canonical graph edges;
- selected-view `to_html()` through the existing graph transform;
- native parser/LOC tests and Lambda source, canonical, and projection fixtures.

This checkpoint is partial Stage 4A through Stage 4C, not full Structurizr
support. Conservative `.dsl` auto-detection, canonical schemas and diagnostics,
archetypes/implied relationships, complete expression and style semantics,
filtered/custom fixtures, dynamic/deployment projection, includes, CLI view
selection, reference adaptation, scene coverage, and release size measurement
remain outstanding.

### Stage 4A - Manual parser and source contract

Status: **partially implemented**. Explicit flavor dispatch and the manual
source contract are present; `.dsl` auto-detection and the release size ledger
remain outstanding.

- add flavor and conservative `.dsl` dispatch;
- extract only proven shared graph lexical helpers;
- parse workspace/model/views block structure;
- preserve assignments, declarations, relationships, properties, directives,
  and source order;
- add source spans, recovery, limits, diagnostics, LOC test, and size ledger;
- support inert recording of all unknown or deferred statements.

### Stage 4B - Canonical C4 workspace

Status: **partially implemented**. Core hierarchy, hierarchical identifiers,
relationships, tags, deployment declarations, and logical instance references
normalize deterministically. Schemas, full diagnostics, properties,
perspectives, archetypes, implied relationships, and complete `this` resolution
remain outstanding.

- add source and canonical schemas;
- resolve flat/hierarchical identifiers and `this`;
- normalize core C4 hierarchy and relationships;
- support tags, properties, perspectives, terminology, and declarative
  archetypes;
- implement boolean implied relationships;
- preserve unsupported directives without execution.

### Stage 4C - Static views and C4 HTML

Status: **partially implemented**. Context/container projection and HTML output
are covered. Landscape/component share the projection implementation but need
dedicated fixtures; filtered/custom views, full expression semantics, style
cascade, terminology, and retained scene/render coverage remain outstanding.

- implement landscape, context, container, component, filtered, and custom
  projection;
- add wildcard, reluctant wildcard, identifier, and initial expression support;
- lower C4 nodes, boundaries, and relationships to canonical Graph IR;
- add measured C4 content, default roles, safe style lowering, and routing;
- render selected views through the retained Radiant callback.

### Stage 4D - Dynamic and deployment views

Status: **not started beyond source/canonical declaration preservation**.

- preserve ordered and parallel dynamic relationship instances;
- render dynamic collaboration diagrams with stable ordinals;
- normalize deployment environments, groups, nested nodes, infrastructure, and
  logical instances;
- project deployment boundaries and instance relationships without identity
  collapse.

### Stage 4E - Includes, CLI, and conformance

Status: **not started**, except for the initial dedicated native/Lambda test
target.

- add policy-controlled local includes with cycle and resource limits;
- add view selection to `view`, `render`, and conversion commands;
- add Structurizr JSON reference adaptation and pinned corpus provenance;
- add manifest-driven native, retained-runtime, scene, and headless tests;
- document the final parser LOC and release-binary size outcome.

## 14. Acceptance Criteria

The initial Structurizr/C4 release is complete when:

1. The manual parser emits source-ordered, source-spanned Mark for the supported
   declarative grammar and preserves unknown statements.
2. Parser LOC and binary-size changes are measured and remain within the
   reviewed budget.
3. Parsing performs no identifier resolution, file access, network access, or
   executable extension loading.
4. Pure Lambda normalization produces deterministic canonical C4 Workspace
   Mark with validated identity, hierarchy, relationships, and provenance.
5. Landscape, context, container, component, filtered, and custom views project
   correct model subsets into canonical Graph IR.
6. Dynamic collaboration views preserve ordered and parallel relationship
   instances.
7. Deployment views preserve nested deployment boundaries and distinct model
   instances.
8. C4 nodes use measured rich content and boundaries use existing cluster
   geometry rather than fixed text-size approximations.
9. `autoLayout` direction and separation values reach the retained layout
   callback without reparsing or recompilation on repeated layout.
10. Safe Structurizr styles and terminology survive HTML, final SVG, and Graph
    Scene adaptation.
11. Unsupported executable/external features produce stable diagnostics and
    remain inert.
12. Checked-in semantic fixtures compare successfully with a pinned official
    Structurizr JSON reference without image comparisons.
13. `.dsl` view and render commands work without Structurizr, Java, Graphviz, or
    Mermaid installed.
14. Graphviz and Mermaid graph suites remain green after shared parser, model,
    layout, and conformance changes.
15. Lambda and relevant Radiant baselines remain green for the implemented
    surface.

## 15. Deferred Work

- Structurizr JSON import/export parity and manual coordinate preservation;
- remote include/extend resolution under an explicit network capability;
- image views and third-party PlantUML/Mermaid/Kroki rendering;
- external themes and icons;
- documentation and ADR rendering;
- custom component discovery;
- executable scripts, plugins, or JVM extension classes;
- full Structurizr expression language beyond the reviewed allowlist;
- dynamic sequence rendering through `lambda.package.chart.sequence`;
- workspace editing and source-preserving formatter;
- pixel parity with Structurizr's browser renderer.

## 16. Primary References

- [Structurizr DSL overview](https://docs.structurizr.com/dsl)
- [Structurizr DSL language reference](https://docs.structurizr.com/dsl/language)
- [Structurizr DSL cookbook](https://docs.structurizr.com/dsl/cookbook/)
- [System context view](https://docs.structurizr.com/dsl/cookbook/system-context-view/)
- [Container view](https://docs.structurizr.com/dsl/cookbook/container-view/)
- [Component view](https://docs.structurizr.com/dsl/cookbook/component-view/)
- [Dynamic view](https://docs.structurizr.com/dsl/cookbook/dynamic-view/)
- [Deployment view](https://docs.structurizr.com/dsl/cookbook/deployment-view/)
- [Structurizr exporters](https://docs.structurizr.com/export)
- [Structurizr Mermaid export](https://docs.structurizr.com/export/mermaid)

The official language reference is the semantic authority. Checked-in corpus
fixtures must record their upstream source and license, and reference
regeneration must pin the Structurizr CLI version used to produce workspace
JSON.
