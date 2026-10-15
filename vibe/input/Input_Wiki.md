# Wiki Family Input in Lambda

**Status (2026-10-15):** initial static profiles implemented for both phases:
shared native readers and Lambda Script writers in **lambda.doc.wiki**.
The user confirmed Phase II ownership and requested implementation of the
proposal, including the task attributes in section 4.1. Build and script
compilation checks have been run; conformance and regression tests remain
pending.

**Scope:** extend Lambda's current MediaWiki-oriented wiki parser to read
TikiWiki, TWiki, Vimwiki, XWiki, ZimWiki, Creole 1.0, and Jira/Atlassian wiki
markup, including a Confluence wiki-markup profile. Creole and Atlassian support
were added at the user's request on 2026-10-15. Each reader produces the same
normalized Mark Doc structure as the existing wiki reader. Phase II implements
all these wiki writers, including MediaWiki, as shipped Lambda Script packages.

**Formal-spec linkage:** [S2.6.1v2 and S2.6.2–S2.6.5](../../doc/Lambda_Formal_Semantics.md)
govern normalized element content. [D7.1.5](../../doc/Lambda_Formal_Design.md)
assigns parser construction to the io-owned MarkBuilder.
[D7.1.1](../../doc/Lambda_Formal_Design.md) governs the native dispatch boundary;
[D7.2.1–D7.2.4](../../doc/Lambda_Formal_Design.md) govern shipped script packages,
their loading, caching, and namespace. No formal ruling defines wiki dialect
coverage or richer task fields; this working design defines the task fields and
does not change a formal ruling.

## 1. Design

Treat these formats as flavors of the existing wiki input family, sharing one
document model. Keep the current MediaWiki behavior as the default for
the existing **wiki** input name. Add explicit selectors:

| Flavor | Selector |
|--------|----------|
| MediaWiki | **wiki:mediawiki** |
| DokuWiki | **wiki:dokuwiki** |
| TikiWiki | **wiki:tikiwiki** |
| TWiki | **wiki:twiki** |
| Vimwiki | **wiki:vimwiki** |
| XWiki | **wiki:xwiki** |
| ZimWiki | **wiki:zimwiki** |
| Creole 1.0 | **wiki:creole** |
| Jira / Atlassian wiki markup | **wiki:jira** |
| Confluence wiki markup | **wiki:confluence** |

Explicit selection is important because extensions overlap or are ambiguous:
TWiki and ZimWiki commonly use **.txt**, Vimwiki commonly uses **.wiki**, and
TikiWiki and XWiki sources do not have a universal file extension. Do not
change the meaning of an existing **wiki** request or guess a dialect from
content when the syntax is ambiguous. Keep the existing MediaWiki default and
require a flavor for the new readers.

Each flavor parses into the same root and common vocabulary:

- Root: one **doc** element containing one **body** element.
- Shared constructs use the existing Mark Doc elements: headings, paragraphs,
  emphasis, code, lists, definition lists, tables, links, images, and rules.
- Format-specific identifiers and presentation hints use the schema's existing
  **id**, **class**, and custom data-attribute conventions where defined.
- Unexpanded source constructs that have no static Mark Doc equivalent are
  retained in a **raw** node with the source flavor and original source, with a
  diagnostic. The reader does not execute macros, scripts, plugins, or includes.

Every element is built through MarkBuilder (D7.1.5). All parser output obeys
S2.6.1v2 and S2.6.2–S2.6.5: absent and empty content is dropped, lists spread
only where the content model requires it, and adjacent strings merge. A source
syntax difference must not create a wiki-specific root or bypass normalization.

## 2. Parser organization

Reuse the current wiki block and inline parser infrastructure, but move
MediaWiki-only syntax decisions behind flavor-specific adapter behavior. The
existing **Format::WIKI** checks are not enough to distinguish these dialects:
the adapter and parser configuration need the selected flavor.

The adapters should declare syntax and feature behavior; shared parser routines
should continue to build common Mark Doc nodes. Extract shared parsing when two
or more dialects have the same structural rule, rather than copying a
MediaWiki-specific parser for each format. Keep flavor-specific code only for
syntax whose delimiters, nesting, escaping, or precedence actually differ.

In particular, audit the current MediaWiki assumptions in heading/list/table
detection, code blocks, links, emphasis, nowiki handling, and brace-delimited
templates. A brace sequence is not a universal wiki macro, and identical
delimiters can have different meanings across flavors. Parsing must follow the
selected adapter, not a broad “wiki-looking” heuristic.

Vimwiki supports its own syntax as well as Markdown and MediaWiki syntax.
**wiki:vimwiki** should mean Vimwiki's own syntax. Users with a Vimwiki notebook
configured for Markdown or MediaWiki should select Lambda's existing Markdown
or MediaWiki reader; do not silently mix those grammars into the Vimwiki
profile. XWiki's syntax is versioned and should start with an explicit,
documented Syntax 2.1 profile. Add other XWiki syntax versions only as named
profiles with fixtures.

The Creole profile targets the published Creole 1.0 language. Engine-specific
extensions must be recognized separately and preserved when their meaning is
not defined by that profile.

Jira and Confluence use a shared Atlassian wiki-notation core, with explicit
**jira** and **confluence** profiles for supported macros and resource
references. Atlassian documents that Jira's wiki renderer uses Confluence wiki
notation, while some Confluence macros depend on concepts absent from Jira.
Reuse one grammar core and shared static-macro handlers, with profile-specific
capabilities; do not treat all platform macros as interchangeable. These
selectors designate plain wiki markup.
See [Jira renderer documentation](https://confluence.atlassian.com/display/ADMINJIRASERVER100/Configuring%2Brenderers)
and [Confluence wiki markup](https://confluence.atlassian.com/doc/confluence-wiki-markup-251003035.html).

## 3. Phase 1 coverage

The first reader profile should cover ordinary authored document structure:

| Family | Phase 1 syntax to map |
|--------|------------------------|
| TikiWiki | Headings, inline emphasis, lists, links, images, tables, code/preformatted blocks, and literal/no-parse regions. |
| TWiki | Headings, inline emphasis, lists, links, images, tables, verbatim/literal blocks, and explicit anchors. |
| Vimwiki | Headings, inline emphasis, lists, tables, links, anchors, and code blocks; reuse the existing binary task representation and preserve richer TODO state. |
| XWiki | Syntax 2.1 headings, inline emphasis, lists, links, images, tables, code blocks, and explicit IDs/classes. |
| ZimWiki | Headings, inline emphasis, lists, links, images, tables, anchors, and code/verbatim blocks; map page tags to existing metadata fields, reuse binary tasks, and preserve richer or position-specific state. |
| Creole 1.0 | Headings, paragraphs, bold/italic, nested ordered/unordered lists, links, images, tables, hard line breaks, rules, escapes, and inline/block nowiki. Preserve extension placeholders without expanding them. |
| Jira / Atlassian wiki markup | h1.–h6. headings, paragraphs, text effects, monospace, mixed nested lists, links, attachments, images, tables, quotes, line breaks, and static code/noformat/anchor/panel/color macros. |
| Confluence wiki markup | The shared Atlassian static syntax and supported static macros, with Confluence page/space/attachment references retained as symbolic targets. |

Representative syntax makes clear why the reader needs an explicit flavor:
TikiWiki uses forms such as **! Heading** and **((Page|label))**; TWiki uses
**---+ Heading**, **[[Web.Topic][label]]**, and percent-delimited variables;
Vimwiki uses **= Heading =**, **[[page|label]]**, and checkbox list markers;
XWiki 2.1 uses **= Heading =**, **[[label>>page]]**, and brace-delimited
macros; ZimWiki uses equals-delimited headings, **[[page|label]]**, **@tags**,
and checkbox list markers. Creole uses **[[page|label]]** links, double-brace
images, triple-brace nowiki, and tilde escaping. Atlassian markup uses
**h1. Heading**, **[label|target]**, **!image.png!**, double-bar table headers,
and **{code}** blocks.

Map Atlassian's static **code** and **noformat** macros to Mark Doc code blocks,
**quote** to blockquote, **anchor** to an ID-bearing span, and simple **panel**
and **color** bodies to ordinary div/span content and supported attributes.
Preserve unsupported macro parameters and presentation semantics with diagnostics.
The **??citation??** text effect is not a bibliographic citation key; retain its
textual semantics rather than inventing a Mark Doc bibliography reference.

Parser precedence must distinguish Creole's image/nowiki braces, Atlassian's
double-brace monospace, and MediaWiki's templates. Creole's italic delimiter
must also respect URL boundaries. These are profile-specific parsing rules.

This is a reader profile, not an emulator for any wiki server or editor. Parse
the documented static syntax. Preserve unknown or unsupported recognizable
constructs as inert raw source and report them; never expand them by contacting
a wiki, loading an attachment, invoking an extension, or running embedded code.

## 4. Features that do not map directly to Mark Doc

Most static prose maps to the existing schema. Some syntax carries behavior or
context that a standalone document tree cannot reproduce:

Page tags already have schema fields: **Meta.tags** and **Meta.keywords** are
optional arrays of strings in
[doc_schema.ls](../../lmd/package/doc/doc_schema.ls), and
[Doc_Schema.md](../../doc/Doc_Schema.md) describes tags as an alias for keywords.
Emitting those fields from wiki input is parser integration work.

Binary tasks also have an existing shared Markdown/Org representation:
a list item has **class="task-list-item"** and
**data-checked="true"** or **"false"**, with a disabled checkbox child.
The Markdown writer reads that representation. This convention is documented
in [Input_Md4c_Compare.md](Input_Md4c_Compare.md) and implemented. The Mark
Doc schema now declares its task fields and checkbox content. The shared
representation is reused; wiki inputs do not introduce a separate binary task
model.

### 4.1 Shared task schema

Extend the existing list item with optional task attributes. The same attribute
group can describe a task-bearing heading, which accommodates Org-style TODO
headings without creating a new document element. An ordinary list item or
heading remains ordinary when it has no task attributes.

Use string-valued data attributes, consistent with the current **data-checked**
representation and Mark Doc's data-attribute model:

| Attribute | Proposed meaning |
|-----------|------------------|
| **data-task-state** | Required for a rich task. One of **todo**, **in-progress**, **done**, **waiting**, **blocked**, **cancelled**, **rejected**, **migrated**, or **custom**. |
| **data-task-label** | Optional original keyword or descriptive status label. Required and nonempty for **custom**. It preserves labels such as REVIEW without assigning them an invented workflow meaning. |
| **data-task-progress-min** | Optional lower bound of completion, an integer percentage encoded as a string from **"0"** to **"100"**. |
| **data-task-progress-max** | Optional upper bound, using the same encoding. Both bounds must be present together and satisfy **0 <= min <= max <= 100**. Equal bounds express an exact percentage. |
| **data-task-marker** | Optional original status token, such as **[o]**, **[-]**, or **REVIEW**, retained for source-compatible output. |
| **data-task-flavor** | Source dialect of the saved marker, such as **vimwiki**, **zimwiki**, or **org**. Present together with **data-task-marker**. |

The state is the semantic authority. The source marker is provenance, and a
writer may reuse it only when decoding it under the recorded flavor and
configuration still yields the current state, label, and progress bounds.
Changing a task's state must not cause Phase II to emit its stale source marker.

Progress is optional and must reflect what the source actually says. In
particular, Vimwiki's partial states describe ranges, not exact percentages:

| Vimwiki marker | State | Progress bounds |
|----------------|-------|-----------------|
| **[ ]** | **todo** | **0..0** |
| **[.]** | **in-progress** | **1..33** |
| **[o]** | **in-progress** | **34..66** |
| **[O]** | **in-progress** | **67..99** |
| **[X]** | **done** | **100..100** |
| **[-]** | **rejected** | absent |

These ranges follow the
[Vimwiki syntax specification](https://github.com/vimwiki/vimwiki/blob/dev/doc/specification.wiki).
They must not be replaced with guessed values such as 50%. A done task's bounds,
if supplied, are **100..100**; a todo task's supplied bounds are **0..0**.
Cancellation, rejection, or migration does not imply a completion percentage.

The [Zim checkbox documentation](https://zim-wiki.org/manual/Help/Check_Boxes.html)
distinguishes completed, not completed, and two migration forms. Map **[*]** to
**done**; preserve **[x]** as **custom** with label **not-completed**, rather
than assuming it means cancellation. Both migration forms use **migrated**,
with labels **moved** and **moved-out** and their distinct source markers.
Configured or unknown status tokens use **custom** unless an explicit profile
mapping gives their meaning. Parent tasks retain their written state; parsing
does not perform an editor's automatic propagation from subtasks.

A Vimwiki item with middle-range progress would produce:

~~~mark
<li class: "task-list-item",
    'data-task-state': "in-progress",
    'data-task-progress-min': "34",
    'data-task-progress-max': "66",
    'data-task-marker': "[o]",
    'data-task-flavor': "vimwiki",
    'data-checked': "false",
    <input type: "checkbox", disabled: "disabled">
    "Finish the report">
~~~

For compatibility, task list items retain **class="task-list-item"**,
**data-checked**, and the existing disabled checkbox child. **data-checked**
is **"true"** only for **done**; the checkbox's checked attribute follows it.
These fields are a binary presentation of the rich state, not an alternative
state store. Validate their consistency. Rich writers read **data-task-state**
first; a legacy task with only the binary representation is interpreted as
**todo** or **done**. A task-bearing heading uses the state attributes without
inserting a list checkbox.

The shared task attribute group and checkbox content are declared in
**doc_schema.ls** and described in **Doc_Schema.md**. This is a common Mark Doc extension for
Markdown, Org, and wiki readers; no new element or content-normalization rule
is needed (S2.6.1v2, S2.6.2–S2.6.5).

### 4.2 Remaining mapping limits

| Feature | Formats | Mark Doc limitation and proposed input behavior |
|---------|---------|--------------------------------------------------|
| Server plugins and macros | TikiWiki, XWiki, Jira/Confluence | Plugins can generate interactive widgets, query data, or arbitrary rendered content. Script/include macros and Atlassian issue-query or page-tree macros depend on the host runtime and data. Preserve their invocation in a raw node and diagnose it; parse explicitly supported static macros only when their mapping is unambiguous. |
| Extension placeholders | Creole | A placeholder preserves content whose semantics belong to a source wiki extension. Retain its exact source as an inert raw node; the standard prose vocabulary does not define its expansion. |
| Variables, includes, searches, and generated TOCs | TWiki; also TikiWiki/XWiki extensions | Values depend on the current page, site configuration, database, or other pages. Do not expand them. Preserve the source as inert raw content. A static TOC could be generated from parsed headings later, but that is a Lambda transformation, not evaluation of the wiki directive. |
| Page hierarchy and relative wiki links | Wiki families, including Creole and Jira/Confluence | A link element can retain a symbolic target and label, but resolving it may require page/notebook context, an interwiki map, or a source-site database. Atlassian issue keys, user mentions, spaces, and attachments may need that context. Preserve unresolved targets without claiming they are portable URLs. |
| Tags attached to particular places | Vimwiki, ZimWiki, and wiki-specific extensions | Page tags fit the existing metadata fields. Tags scoped to an inline position or individual heading have no agreed common mapping. Preserve their position and source until that mapping is defined; do not automatically turn every local tag into a page tag. |
| Task states beyond checked/unchecked | Vimwiki and ZimWiki | Section 4.1 defines the implemented state, progress-range, and marker attributes. Unsupported target states are diagnosed; custom source semantics remain labelled and source-preserved. |
| Notebook/page operations and stored state | Mainly Vimwiki and ZimWiki; also server wikis | Diary navigation, backlinks, page indexes, history, access control, and attachment databases are application state, not content syntax. They cannot be reconstructed from one source page. Keep them outside the Mark Doc conversion contract. |
| Wiki-specific layout, colors, or interactive objects | Wiki families, especially TikiWiki, XWiki, and Jira/Confluence | Standard tables, images, links, classes, IDs, and simple block containers map. Arbitrary plugin output or engine-specific layout has no portable semantic equivalent; retain source and diagnose where needed. |

Thus, the main loss boundary is not ordinary formatting. It is dynamic output,
application state, and relationships that need a wiki's surrounding page set
or runtime. Raw preservation keeps the source available, but it does not make
those features render as they do in the original wiki.

## 5. Phase II: Lambda Script writers

**Confirmed ownership requirement (user, 2026-10-15):** all wiki formatting
semantics belong to Lambda Script, including the replacement MediaWiki writer.
Use a shipped **lambda.doc.wiki** facade, with shared traversal and text-building
helpers and small dialect modules beneath it. The package belongs under
**lmd/package/doc/** following D7.2.4, is distributed as source and cached
under D7.2.3, and uses immutable module bindings under D7.2.1.

The shared script code owns document traversal, list nesting, table structure,
task-state handling, and diagnostics. Dialect modules own syntax, escaping,
links, image forms, and specialized constructs. Extract common behavior before
adding a third near-identical variant; do not translate the C++ implementation
into one copy per flavor. Creole gets its own syntax profile; Jira and
Confluence share the Atlassian writer core with separate capability profiles.

Formatting is a pure document-to-text transformation. File output stays at
the existing resource IO boundary (D7.1.2v2). The script package should expose
serialized text plus structured diagnostics for callers that need a conversion
report; the ordinary format API returns its text or propagates an error under
the selected loss policy.

### 5.1 Writer behavior

Phase 2 adds one writer profile per flavor, using the same Mark Doc vocabulary
and the inverse flavor rules. It should:

1. Write common Mark Doc elements to each wiki's supported static syntax.
2. Escape text and delimiters according to the selected dialect.
3. Preserve page links as symbolic targets unless a caller supplies context for
   resolving them.
4. Emit raw nodes only to their matching source format; for another target,
   retain them with a diagnostic or report the conversion as lossy.
5. Read the shared rich task state and progress bounds. Reuse a saved marker
   only when its semantics still match; otherwise generate the target dialect's
   representation.
6. Report Mark Doc constructs that the target wiki cannot express, including
   richer task states or progress precision that its syntax would lose.

### 5.2 Native integration

Both script-facing formatting and CLI conversion must reach the same Lambda
Script writer. Preserve the existing unqualified **wiki** target as MediaWiki
and route the explicit flavor selectors to the corresponding package profile.
This also applies to the compound **markup** format's wiki/mediawiki aliases
and formatted file-output callers.

C++ owns package invocation, caching, rooted argument/result transport, and
ordinary error/diagnostic propagation. Wiki syntax and emission rules live in
the script package. Under D7.1.1, runtime-owned dispatch invokes the package;
the lower io formatter library must not acquire an upward runtime dependency.
Reuse the existing package-export invocation mechanism rather than synthesizing
Lambda source for every document. A missing or failed package produces an
ordinary error.

### 5.3 Retire the MediaWiki C++ writer

The existing writer does exist. Its wrapper is only one part: MediaWiki rules,
escaping, links, table output, and template/nowiki handlers are distributed
across the shared C++ formatting files. Phase II retires that entire wiki
emission path after the script replacement covers the existing contract.

The migration is complete when:

1. The MediaWiki script writer preserves the existing supported headings,
   inline content, links, nested lists, tables, and saved template/nowiki source.
2. All public wiki formatting paths invoke the script facade.
3. The native wiki wrapper, wiki-specific rules/helpers, declarations, and
   dispatch entries are removed. Shared C++ machinery still used by other
   formats is retained.
4. Build source lists and tests are migrated to the script implementation.
   Edit **build_lambda_config.json** and regenerate through **make**, following
   the repository's build-file policy.
5. Package errors are observable through the public API, and no C++ MediaWiki
   writer remains as a fallback.

Input support does not imply round-trip identity. Phase 1 guarantees normalized
Mark Doc output and inert preservation of unsupported source constructs.
Phase 2 can promise a round trip only for the explicitly supported static
subset of a flavor.

## 6. Acceptance criteria

- Existing unqualified **wiki** input continues to produce the same MediaWiki
  output as before.
- Each new explicit flavor parses its documented static constructs into the
  common doc/body shape.
- Equivalent prose structures produce equivalent Mark Doc nodes across flavors.
- Page tags use the existing metadata fields; binary tasks reuse the shared
  Markdown/Org representation, with richer task states preserved separately.
- All output follows S2.6.1v2 and S2.6.2–S2.6.5 and is built under D7.1.5.
- Dynamic or unsupported constructs remain inert, retain their source, and
  produce an actionable diagnostic.
- Ambiguous file extensions do not change dialect selection.
- Fixtures cover common syntax, flavor-specific syntax, delimiter collisions,
  malformed constructs, and raw-preservation behavior.
- Creole fixtures cover its 1.0 syntax, escape/URL boundaries, image/nowiki
  delimiters, and extension preservation. The published language specification
  is authoritative over a reference site's rendered output or draft test cases.
- Atlassian fixtures cover shared Jira/Confluence syntax, static macros, nested
  lists and tables, symbolic resource references, and unsupported dynamic
  macros. Profile-specific behavior must not silently change between platforms.
- Phase 2 has separate writer fixtures and records loss at each target boundary.
- Rich task fixtures distinguish all proposed states, exact percentages from
  ranges, custom markers, contradictory attributes, and stale source markers.
- Phase II wiki output is generated by Lambda Script through every public
  formatting path; MediaWiki's native emission path is removed.
- Every added Lambda test script has its corresponding expected **.txt** file.

## Appendix A. Existing implementation points

The current implementation is centered in
**lambda/input/markup/format/wiki_adapter.cpp**. The format and flavor enums are
in **markup_common.hpp**; adapter lookup is in **format_registry.cpp**.
MediaWiki-specific branches also appear in **inline_spans.cpp**,
**inline_wiki.cpp**, **block_table.cpp**, **block_list.cpp**, **block_code.cpp**,
and the shared block detection routines. The **wiki** input name is dispatched
through **lambda/input/input.cpp**.

The binary task node construction is in **block_list.cpp**; checkbox detection
is supplied by the Markdown and Org adapters. **lambda/format/format-md.cpp**
reads the same list-item attributes and checkbox child when writing Markdown.
Org heading TODO keywords and trailing tags are currently stripped by
**OrgAdapter::detectHeader**; that implementation does not supply a reusable
structured heading-state schema.

The completed native MediaWiki formatter removal inventory is:

| File | Wiki-specific implementation to migrate and retire |
|------|---------------------------------------------------|
| **lambda/format/format-wiki.cpp** | **format_wiki** and **format_wiki_string** wrappers. |
| **lambda/format/format-utils.cpp / .h** | **WIKI_RULES**, **WIKI_ESCAPE_CONFIG**, **emit_link_wiki**, and wiki/mediawiki rules lookup. |
| **lambda/format/format-markup.cpp / .h** | **wiki_custom_handler**, **wiki_table_row**, **emit_table_wiki**, and their declarations. |
| **lambda/format/format.cpp**, **format.h** | Native wiki dispatch and exported wiki formatter declarations. |
| **build_lambda_config.json** | The native wiki formatter source entry. |
| **test/test_format_markup_gtest.cpp** and wiki round-trip fixtures | Existing writer assertions now call the script writer through CLI conversion; native rule lookup expects no wiki rules. |

The runtime already has **interp_call_package_export** and a package conversion
precedent in **fn_format2** for MathML. The wiki bridge should use that owned
invocation path and return the script's serialized text directly. Audit CLI
conversion and **lambda/runtime/lambda-proc.cpp** formatted output so they do
not bypass it by calling the lower **format_data** wiki path.

The [user support guide](../../doc/Markup_Formats_Support.md#15-wiki-dialect-coverage)
lists all ten input/output profiles, task/tag mappings, and current limits.
The [package reference](../../doc/Lambda_Packages.md#17-docwiki--wiki-writers)
documents **lambda.doc.wiki** exports, options, and diagnostic fields;
the [CLI reference](../../doc/Lambda_CLI.md#convert--format-conversion)
documents selectors and **--loss error|allow**.

## Implementation notes (2026-10-15)

- `lambda/input/markup/wiki.hpp` and `format/wiki_adapter.cpp` select ten
  explicit profiles. Shared `block/block_wiki.cpp` and `inline/inline_wiki.cpp`
  build Input-owned Mark Doc with MarkBuilder (D7.1.5). MediaWiki tables now use
  the same `thead`/`tbody` structure as the other readers. Content appends use
  the existing normalization path (S2.6.1v2, S2.6.2–S2.6.5).
- The new input selectors are available through `input`, `parse`, CLI conversion,
  and the existing parser dispatch. `wiki` remains MediaWiki; ambiguous file
  extensions keep their current interpretation.
- `doc_schema.ls` has been repaired to use element contracts for actual Mark
  Doc nodes, remove duplicate record/example definitions, and keep `Document`
  as the final root declaration. Homogeneous metadata arrays use `T[]`
  (S11.1.1v3). Schema compilation now succeeds.
- `lmd/package/doc/wiki.ls` exposes `format`, `format_report`, `format_native`,
  and `format_file`. `wiki/profiles.ls` supplies syntax data,
  `wiki/writer.ls` owns traversal and escaping, and `wiki/tasks.ls` checks task
  invariants and chooses compatible markers. These are shipped source modules
  under D7.2.4, rather than generated Lambda programs.
- `format(value, "wiki:creole")`, `format(value, {type:"wiki",flavor:"jira"})`,
  formatted `output`, and `lambda convert -t wiki:<flavor>` invoke the package.
  Invocation stays in the runtime/CLI layer (D7.1.1); arguments and results use
  precise roots (D5.3.3). The lower runtime-free `format_data` API explicitly
  refuses wiki output; native hosts use the runtime API or package export.
- `format_report` returns `{text, diagnostics}`. `format` defaults to
  `loss:"error"`; callers can choose `loss:"allow"` after inspecting diagnostics.
  CLI conversion provides `--loss error|allow` for wiki output.
- MediaWiki's native wrapper, rules, escaping configuration, link/table emitters,
  custom handler, declarations, and build entry have been retired. Existing
  writer assertions have been redirected to the public script conversion path.

### Current limits

The profiles cover authored static structure, rather than the complete runtime
of each wiki. Unsupported macros, variables, extension placeholders, Vimwiki
page directives, and unimplemented XWiki link/image parameters remain inert,
flavor-labelled raw source with parser diagnostics. DokuWiki/Zim image option
queries are also preserved raw; their static sizing parameters could be mapped
in a later extension and are not an intrinsic Mark Doc limitation.

Zim tags and Vimwiki tags in the first two source lines populate `meta.tags`.
Vimwiki local tags retain their exact source and position as raw content; their
heading/line association requires context and the writer reports this loss.
Zim file creation metadata maps to `meta.created`. Notebook state and resource
resolution remain outside a single-page conversion.

Task markers retain coarse ranges and custom labels. Binary Markdown/Org task
output remains compatible. Unsupported target workflow states or precision,
heading tasks, target-specific text effects, captions, table attributes/spans,
and raw source from a different dialect appear in writer diagnostics. Explicit
blockquote writing is currently supported for Atlassian macros, XWiki, and
DokuWiki; other profiles report that writer limitation. Vimwiki hard line
breaks are similarly diagnosed. These static writer gaps are distinct from
features that cannot be represented by Mark Doc at all.

Build checks: `make build` and package compilation with
`./lambda.exe --dry-run lmd/package/doc/wiki.ls`, plus the same compilation
check for `lmd/package/doc/doc_schema.ls`. No conformance or regression
suite has been run for this implementation; the acceptance criteria above are
not yet a verified coverage claim.

## References

- [Creole elements](https://wikicreole.org/wiki/Elements)
- [Creole 1.0 specification](https://www.wikicreole.org/wiki/Creole1.0)
- [Creole 1.0 test cases and their qualification](https://wikicreole.org/wiki/Creole1.0TestCases)
- [Jira wiki renderer and Confluence notation](https://confluence.atlassian.com/display/ADMINJIRASERVER100/Configuring%2Brenderers)
- [Jira static macro examples](https://confluence.atlassian.com/spaces/JIRA050/pages/284367590/Editing%2BRich-Text%2BFields)
- [Confluence Wiki Markup](https://confluence.atlassian.com/doc/confluence-wiki-markup-251003035.html)
- [TikiWiki Wiki Syntax](https://doc.tiki.org/Wiki-Syntax)
- [TikiWiki Wiki Plugins](https://doc.tiki.org/Wiki-Plugins)
- [TWiki Text Formatting Rules](https://www.twiki.org/cgi-bin/view/TWiki/TextFormattingRules)
- [Vimwiki syntax specification](https://github.com/vimwiki/vimwiki/blob/dev/doc/specification.wiki)
- [XWiki Syntax Guide](https://www.xwiki.org/xwiki/bin/view/XWiki/XWikiSyntax)
- [Zim Wiki Syntax](https://zim-wiki.org/manual/Help/Wiki_Syntax.html)
- [Zim Checkboxes](https://zim-wiki.org/manual/Help/Check_Boxes.html)
- [Zim Links](https://zim-wiki.org/manual/Help/Links.html)
- [Zim Tags](https://zim-wiki.org/manual/Help/Tags.html)
- [Mark Doc schema](../../doc/Doc_Schema.md)
- [Lightweight markup parser design](Input_Lightweight_Markup.md)
