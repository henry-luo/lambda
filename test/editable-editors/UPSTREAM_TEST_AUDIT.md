# Upstream test portability audit

This audit covers the pinned source checkouts in `ref/`:

- CodeMirror view `6.43.6`
- ProseMirror view `1.42.2`
- Editor.js `2.31.6`

`make editable-editor-e2e` executes every fixture listed as **ported** below.
A port drives the real installed upstream bundle through Radiant's public
pointer, keyboard, IME, selection, clipboard, and timer routes. It must assert
the editor's own saved/model state, public selection state, or a documented
public DOM contract. It does not copy upstream private test helpers into the
application.

## CodeMirror view

| Upstream source | Outcome | Radiant coverage or reason |
| --- | --- | --- |
| `test/tempview.ts` | Helper only | Upstream's detached-view factory is test infrastructure, not a product behavior. |
| `test/test-heightmap.ts` | Excluded | Validates package-private `HeightMap` data structures, not browser/Radiant integration. |
| `test/webtest-bidi.ts` | Excluded | Its UAX visual-order oracle validates CodeMirror's internal `bidiSpans` implementation. Radiant layout bidi behavior is covered by its own layout baseline rather than duplicating that private oracle. |
| `test/webtest-composition.ts` | Ported | `editable-editors-codemirror-selection-composition.json` and `editable-editors-codemirror-domchange.json` cover empty-line, selected-range, commit, and cancel behavior through the real observer. |
| `test/webtest-coords.ts` | Excluded | Exact browser glyph rectangles and widget sides are renderer/font dependent; the portable host-geometry assertion remains in `editable-editors-codemirror-full.json`. |
| `test/webtest-direction.ts` | Ported | `editable-editors-codemirror-direction.json` uses a real `EditorView.theme` and checks `view.textDirection` after the render checkpoint. |
| `test/webtest-domchange.ts` | Ported | `editable-editors-codemirror-domchange.json` covers replacement, backward/forward delete, multiline paste, composition, and the read-only case is in `editable-editors-codemirror-readonly.json`. |
| `test/webtest-draw-decoration.ts` | Excluded | Decoration/wrapper/widget reuse requires synthetic extension implementations; it tests CodeMirror's renderer internals rather than the common editable host. |
| `test/webtest-draw.ts` | Excluded | Virtual viewport and scroll stabilization depend on browser measurement and test-only view setup. |
| `test/webtest-events.ts` | Ported | `editable-editors-codemirror-events.json` checks real public `domEventHandlers` for key, beforeinput, input, paste, copy, and cut. |
| `test/webtest-extension.ts` | Excluded | Plugin update/effect lifecycle is a package-extension unit contract and needs custom plugin code, not a stock editable application. |
| `test/webtest-hover.ts` | Excluded | Tooltip placement and asynchronous hover extensions are configuration-specific UI, outside the installed minimal editable bundle. |
| `test/webtest-motion.ts` | Ported | The selection portions are covered by `editable-editors-codemirror-full.json` and `editable-editors-codemirror-selection-composition.json`, which prove DOM selection-to-EditorState reconciliation. |

## ProseMirror view

| Upstream source | Outcome | Radiant coverage or reason |
| --- | --- | --- |
| `test/view.ts` | Excluded | Upstream's `tempEditor`, builders, and direct view API probes are unit harness infrastructure. The stock mounted view is exercised by every ProseMirror fixture. |
| `test/webtest-clipboard.ts` | Ported in public default scope | `editable-editors-prosemirror-clipboard-html.json` verifies ordinary external HTML block parsing. Callback transforms and custom serializers remain application configuration, not a default editor behavior. |
| `test/webtest-composition.ts` | Ported | `editable-editors-prosemirror-selection-composition.json` covers an empty paragraph commit/cancel and `editable-editors-prosemirror-marked-composition.json` preserves an `em` mark. |
| `test/webtest-decoration.ts` | Excluded | Pure `DecorationSet` mapping structures are package-unit behavior and need direct model construction. |
| `test/webtest-domchange.ts` | Ported in editable scope | `editable-editors-prosemirror-domchange.json` drives physical replacement, delete/backspace, paste, and selected-word IME reconciliation. Deep node-view and surrogate-pair matrices require custom schemas/node views. |
| `test/webtest-draw-decoration.ts` | Excluded | Requires extension-provided decorations and widgets. |
| `test/webtest-draw.ts` | Excluded | Validates internal DOM reuse and plugin-view lifecycle through custom `EditorView` props. |
| `test/webtest-endOfTextblock.ts` | Excluded | Exact coordinate/navigation oracle depends on browser layout and custom block/RTL fixtures. |
| `test/webtest-markview.ts` | Excluded | Requires a user-defined mark view implementation. |
| `test/webtest-nodeview.ts` | Excluded | Requires a user-defined node view implementation. |
| `test/webtest-selection.ts` | Ported in editable scope | `editable-editors-prosemirror-full.json` asserts mouse/DOM selection reflected as model `from/to`; exact rectangle and selectable-node matrices require custom schema/layout fixtures. |
| `test/webtest-view.ts` | Excluded | Tests direct prop/state replacement and package callbacks; standard mounting, editable/read-only, destruction, and dispatch are covered by the stock fixture. |

## Editor.js

| Upstream source | Outcome | Radiant coverage or reason |
| --- | --- | --- |
| `test/cypress/tests/api/block.cy.ts` | Excluded | Requires direct test-only calls to `BlockAPI.dispatchChange`; public `onChange` mutation delivery is covered instead. |
| `test/cypress/tests/api/blocks.cy.ts` | Excluded | Tests programmatic `blocks` insertion/conversion with many custom Tool conversion contracts. |
| `test/cypress/tests/api/caret.cy.ts` | Excluded | Direct `Caret` API offsets require fixture-specific nested custom Tool markup. |
| `test/cypress/tests/api/toolbar.cy.ts` | Excluded | Toolbar toggling is a UI API fixture requiring a toolbox-specific configuration. |
| `test/cypress/tests/api/tools.cy.ts` | Excluded | Sanitizer/toolbox/tune matrix needs custom Tool classes. |
| `test/cypress/tests/api/tunes.cy.ts` | Excluded | Requires custom tune classes and popover configuration. |
| `test/cypress/tests/block-ids.cy.ts` | Ported in default block scope | `editable-editors-editorjs-block-ids.json` verifies the generated DOM block identifiers are present and unique before and after keyboard insertion. Exhaustive generated-ID algorithm tests remain upstream unit behavior. |
| `test/cypress/tests/copy-paste.cy.ts` | Ported in default Tool scope | `editable-editors-editorjs-paste.json` covers plain text, HTML, paragraphs, and heading parsing; `editable-editors-editorjs-clipboard.json` covers selected block copy/cut. Custom MIME, patterns, and paste configs need custom Tools. |
| `test/cypress/tests/i18n.cy.ts` | Excluded | Translation strings require i18n/toolbox configuration. |
| `test/cypress/tests/initialization.cy.ts` | Ported in stock scope | The fixture initializes visible real UI and `editable-editors-editorjs-readonly.json` verifies configured read-only behavior. CSP nonce is browser-document policy, outside headless editable interaction. |
| `test/cypress/tests/inline-tools/link.cy.ts` | Excluded | Requires the Link inline Tool and its dialog/selection configuration. |
| `test/cypress/tests/modules/BlockEvents/ArrowLeft.cy.ts` | Ported in default paragraph scope | `editable-editors-editorjs-arrow.json` observes the real DOM selection move from the second paragraph start to the first paragraph end. Whitespace and contentless custom Tool cases need those Tools. |
| `test/cypress/tests/modules/BlockEvents/ArrowRight.cy.ts` | Ported in default paragraph scope | `editable-editors-editorjs-arrow.json` observes the real DOM selection move across a paragraph boundary. |
| `test/cypress/tests/modules/BlockEvents/Backspace.cy.ts` | Ported in default paragraph scope | `editable-editors-editorjs-operations.json` checks Enter plus Backspace merge. Custom multi-input/media/whitespace cases need custom Tools. |
| `test/cypress/tests/modules/BlockEvents/Delete.cy.ts` | Ported in default paragraph scope | `editable-editors-editorjs-delete.json` checks Delete merges a following paragraph and emits removal. |
| `test/cypress/tests/modules/BlockEvents/Enter.cy.ts` | Ported | `editable-editors-editorjs-operations.json` and `editable-editors-editorjs-onchange-index.json` cover split/end insertion and insertion above the current block. |
| `test/cypress/tests/modules/BlockEvents/Slash.cy.ts` | Excluded | Slash toolbox and command-slash tunes require toolbox/tune setup. |
| `test/cypress/tests/modules/BlockEvents/Tab.cy.ts` | Excluded | Multi-input and contentless navigation requires custom Tools and external focus fixtures. |
| `test/cypress/tests/modules/InlineToolbar.cy.ts` | Excluded | Requires configured inline tools and geometry-specific toolbar expectations. |
| `test/cypress/tests/modules/Renderer.cy.ts` | Ported in stock rendering scope | `editable-editors-editorjs.json` and lifecycle tests cover initial stock block rendering; stub/error Tool cases require deliberately failing Tools. |
| `test/cypress/tests/modules/Saver.cy.ts` | Ported | HTML heading paste replaces a Tool root and `editable-editors-editorjs-paste.json` asserts it survives `save()`. The underlying `replaceChild` observer identity contract is in `dom_mutation_replacechild_notifies.json`. |
| `test/cypress/tests/modules/Tools.cy.ts` | Excluded | Tool registries, preparation, and ordering are unit contracts for custom Tools. |
| `test/cypress/tests/modules/Ui.cy.ts` | Ported in stock scope | `editable-editors-editorjs-clipboard.json` covers selected block deletion and standard pointer interaction covers current-block updates. |
| `test/cypress/tests/onchange.cy.ts` | Ported in default event scope | `editable-editors-editorjs-onchange.json`, `-onchange-index.json`, `-delete.json`, and `-readonly.json` cover changed/added/removed events, index, and read-only silence. Batching/tunes/render APIs require custom programmatic operations. |
| `test/cypress/tests/readOnly.cy.ts` | Ported for construction | `editable-editors-editorjs-readonly.json` verifies the initialized read-only state. Runtime toggling is not exposed by this minimal public fixture. |
| `test/cypress/tests/sanitisation.cy.ts` | Ported in default paste scope | Paragraph and heading HTML paste is saved through real default Tool sanitizers. Script/custom sanitizer cases need custom Tool configuration. |
| `test/cypress/tests/selection.cy.ts` | Ported | `editable-editors-editorjs-clipboard.json` uses real select-all block selection followed by copy/cut. |
| `test/cypress/tests/tools/BlockTool.cy.ts` | Excluded | Internal Tool wrapper metadata/constructor tests. |
| `test/cypress/tests/tools/BlockTune.cy.ts` | Excluded | Internal tune wrapper/constructor tests. |
| `test/cypress/tests/tools/InlineTool.cy.ts` | Excluded | Internal inline Tool wrapper/constructor tests. |
| `test/cypress/tests/tools/ToolsCollection.cy.ts` | Excluded | Internal collection unit tests. |
| `test/cypress/tests/tools/ToolsFactory.cy.ts` | Excluded | Internal factory unit tests. |
| `test/cypress/tests/ui/BlockTunes.cy.ts` | Excluded | Requires tune/conversion configuration. |
| `test/cypress/tests/ui/DataEmpty.cy.ts` | Ported | `editable-editors-editorjs-dataempty.json` covers initial empty, typed, cleared, and new default paragraph state. |
| `test/cypress/tests/ui/InlineToolbar.cy.ts` | Excluded | Requires configured inline Tool popovers. |
| `test/cypress/tests/ui/Placeholders.cy.ts` | Excluded | Requires placeholder/autofocus configuration. |
| `test/cypress/tests/ui/toolbox.cy.ts` | Excluded | Requires toolbox shortcut and conversion configuration. |
| `test/cypress/tests/utils.cy.ts` | Excluded | Pure upstream utility unit tests. |
| `test/cypress/tests/utils/flipper.cy.ts` | Excluded | Private keyboard-navigation helper tests. |
| `test/cypress/tests/utils/popover.cy.ts` | Excluded | Private popover component tests. |

## Platform regressions retained by the port

The upstream-derived Editor.js HTML paste case found two Radiant platform
contracts that now have focused regressions:

- `Document.execCommand("insertHTML")` parses the fragment, mutates the live
  range, updates selection, and notifies observers. This is the Editor.js core
  inline-paste path, not an Editor.js-specific workaround.
- `replaceChild` sends one `childList` `MutationRecord` containing both the
  added and removed node. The record preserves wrapper identity, allowing
  Editor.js to replace a Tool root and subsequently save the new root.
