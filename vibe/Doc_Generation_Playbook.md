# Documentation Generation Playbook

A reusable recipe for producing a structured detailed-design doc set for a subsystem of the Lambda/Radiant repo, the way the LambdaJS runtime set in `doc/dev/js/` was produced. Paste the prompt below (with the placeholders filled), or hand it to an agent.

The output is: one doc per subsystem area, each grounded in the actual code with a "Known Issues" section, plus an overview/index doc with C4 architecture diagrams — all cross-linked, all rendered with embedded SVG diagrams.

---

## The prompt

````text
Develop structured detailed-design documentation for the <SUBSYSTEM> of the Lambda/Radiant repo,
following EXACTLY the process, structure, and conventions used for the LambdaJS runtime docs in
doc/dev/js/. Before doing anything, read doc/dev/js/JS_00_Overview.md plus two detail docs
(JS_01_Compilation_Pipeline.md and JS_06_Objects_Properties_Prototypes.md) as the gold-standard
template for structure, tone, density, diagrams, cross-linking, and the per-doc "Known Issues" section.

SCOPE / INPUTS
- Subsystem: <SUBSYSTEM>. Primary code under: <CODE_DIRS>.
- Source material to mine: <SOURCE_MATERIAL> (past dev/vibe notes), plus existing canonical docs to
  ABSORB into the new set and then DELETE at the end: <EXISTING_DOCS> (confirm before deleting).
- Output: a new set under <OUTPUT_DIR>, files named <PREFIX>_NN_Topic.md (00 = overview/index, last).

PROCESS (4 phases, with a checkpoint)
1. SCAN — read all the source material (fan out parallel subagents; each digests a chunk and returns
   data structures, algorithms, code/data flow, design decisions + rationale, and especially
   known issues / tech-debt / TODOs). Consolidate into a temp working outline
   <OUTPUT_DIR>/<PREFIX>_Design_Outline.md, plus a current source inventory (file -> responsibility)
   for <CODE_DIRS>, plus a migration map from <EXISTING_DOCS>.
2. PLAN — propose a structured multi-doc breakdown grouped into parts, each doc mapped to its code
   files and source material. Then PAUSE for my approval of the breakdown before writing anything.
3. WRITE — write 1-2 representative sample docs FIRST for my style sign-off, then batch the rest
   (one subagent per doc). GROUND EVERY DOC IN A FRESH READ OF THE ACTUAL CURRENT CODE — not the
   logs/old docs, which may be stale; follow the code and note where it diverges from old claims.
   Cite file:line + exact symbol names; do NOT quote code blocks. Every detailed doc ends with a
   "## Known Issues & Future Improvements" section grounded in what you actually find (messy /
   duplicate / inefficient / incomplete / fragile code, TODO/FIXME/HACK comments, hard-coded caps,
   perf concerns). Cross-link sibling docs by exact filename. Each doc also gets an
   "Appendix: Source map" (file -> responsibility) and "Related documents" list.
4. SUMMARY — write <PREFIX>_00_Overview.md LAST: index of the set, architecture, cross-cutting design
   themes, a synthesized maturity/known-issues overview, and a glossary, with C4 architecture diagrams.

CONVENTIONS (match the JS set exactly)
- Markdown: NO manual line breaks within prose — one physical line per paragraph / list-item /
  blockquote-line (the reader uses Obsidian, where a single newline renders as a visible break).
  Run `python3 utils/reflow_md.py FILE...` as a final safety-net pass.
- Diagrams: hybrid. Mermaid (.mmd) for flow/sequence/state/class diagrams; Structurizr DSL
  (architecture.dsl) for the C4 system-context/container/component views. Keep all sources in
  <OUTPUT_DIR>/diagram/, render to SVG, and embed with
  <img alt="..." src="diagram/NAME.svg" width="N"> at the SVG's natural width capped at ~720px
  (do NOT use ![](...) — it renders stretched to 100% of the pane). Render with
  `bash utils/render_md_diagrams.sh <OUTPUT_DIR>/diagram` (mmdc + structurizr-cli already installed:
  JDK at /opt/homebrew/opt/openjdk, structurizr-cli at temp/tools/structurizr-cli). In the .dsl,
  name each C4 view so its key is the embedded filename (e.g. view key "c4_context" -> c4_context.svg).
  GOTCHA: this mmdc build rejects "Note over" in Mermaid sequence diagrams — use plain messages.
  Structurizr DSL is line-oriented (one statement per line; no inline ';').
- Execution: use parallel subagents for the scan and for per-doc drafting; you (the orchestrator)
  review each draft, render diagrams, run the reflow pass, verify every embed + cross-link resolves,
  and keep terminology/cross-links consistent.
- Cleanup (after the set is complete and verified): delete the absorbed old docs, fix any dangling
  references to them elsewhere in the repo, and link <PREFIX>_00_Overview.md from README.md,
  CLAUDE.md, and AGENTS.md (doc list + a Key Entry Points row).

Before starting, ask me clarifying questions — in particular confirm: (a) exact subsystem
scope/boundaries, (b) which existing docs to absorb+delete, (c) doc granularity (how many docs),
(d) the diagram-tooling choice (reuse the JS Mermaid+Structurizr hybrid), and (e) whether to
checkpoint after the breakdown.
````

---

## Filling the placeholders

| Placeholder | Radiant layout engine | Lambda core runtime | Input / Output |
|---|---|---|---|
| `<SUBSYSTEM>` | Radiant CSS layout & rendering engine | Lambda core language runtime | Lambda input parsers & formatters |
| `<CODE_DIRS>` | `radiant/` | `lambda/` (excl. `js/`, `input/`, `format/`, `validator/`) | `lambda/input/`, `lambda/format/` |
| `<EXISTING_DOCS>` | `doc/dev/Radiant_Layout_Design.md`, `doc/dev/Radiant_View_Design.md` | `doc/dev/Lamdba_Runtime.md`, `doc/dev/Lambda_Transpiler.md` | (none, or relevant ones) |
| `<OUTPUT_DIR>` | `doc/dev/radiant/` | `doc/dev/lambda/` | `doc/dev/io/` |
| `<PREFIX>` | `RAD` | `LR` | `IO` |
| `<SOURCE_MATERIAL>` | `vibe/` notes on radiant / layout / CSS | `vibe/` notes on runtime / transpiler / MIR | `vibe/` notes per format |

Other candidate subsystems: the CSS engine (`lambda/input/css/` + Radiant CSS resolution), the schema validator (`lambda/validator/`), the polyglot Jube runtimes (`lambda/` Python/Bash/Ruby paths), the build system (`build_lambda_config.json` → `utils/generate_premake.py`).

---

## Tooling (already set up in this repo)

- **Diagram render:** `bash utils/render_md_diagrams.sh <diagram-dir> [name ...]` — Mermaid `.mmd` → `<name>.svg`; Structurizr `.dsl` → `<view-key>.svg` per view. Reads `JAVA_HOME` (default `/opt/homebrew/opt/openjdk`) and `STRUCTURIZR_CLI` (default `temp/tools/structurizr-cli/structurizr.sh`); skips `.dsl` with a notice if structurizr-cli is absent. Shared puppeteer config is `utils/puppeteer-config.json`.
- **Line-break reflow:** `python3 utils/reflow_md.py FILE...` — joins soft-wrapped prose into one physical line per paragraph (preserves tables, code fences, headings, list boundaries, and `**Label:**` lines).
- **Installed prerequisites:** Node/npx (mmdc auto-fetched via npx, downloads Chromium once), a JDK (`brew install openjdk`, keg-only), and structurizr-cli under `temp/tools/`. Docker is not used.
- **Known quirks:** the current mmdc build fails to parse `Note over` in Mermaid sequence diagrams — use plain self-messages instead. Structurizr DSL must be line-oriented (no inline `;`).

---

## Why these conventions

- **Ground in code, not logs.** The dev/vibe notes are a development history and drift from the code; several "facts" in them were already wrong by the time the JS set was written. Each doc is written against a fresh read of the current source, and notes where it diverges.
- **No code quotes, only `file:line` + symbols.** Keeps docs durable as code moves and forces the writer to point at the real thing rather than paraphrase a stale snippet.
- **A Known Issues section per doc.** Surfaces messy/duplicate/inefficient/incomplete areas as concrete future-improvement targets, grounded in the code rather than speculation.
- **Diagrams as separate sources → SVG → `<img width>`.** Sources stay diffable and re-renderable; `<img width="N">` (not `![]()`) prevents small diagrams from being stretched to full pane width, and the `width` attribute survives GitHub's HTML sanitizer (inline `style` does not).
