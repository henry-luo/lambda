// Vanilla-DOM demo bootstrap (Stage 4B) — no React.
//
// Mounts the framework-free EditorViewDom controller on a contenteditable
// surface. This is the single-file page that runs in the browser AND under
// Radiant (`./lambda.exe view test/html/editor-dom.html`). It deliberately
// keeps the chrome minimal — the rich toolbar (FullEditor) is a separate
// vanilla port; the goal here is the core editing surface on plain DOM.

import { html5SubsetSchema } from '../src/schemas/index.js'
import { parseHtmlToDoc } from '../src/view/html-parser.js'
import { EditorViewDom } from '../src/view/editor-view-dom.js'
import type { EditorViewState } from '../src/view/editor-state.js'

const SAMPLE = `
<doc>
  <h1>Lambda Editor — plain-DOM</h1>
  <p>Hello <span style="font-weight: bold">world</span>. Type into the editor below.</p>
  <p><cursor></cursor>The caret should be at the start of this paragraph.</p>
  <ul>
    <li>List items work</li>
    <li>So do <span style="font-style: italic">marks</span></li>
  </ul>
  <blockquote>
    <p>Quotes preserve structure.</p>
  </blockquote>
</doc>
`

function main(): void {
  const root = document.getElementById('root')
  if (root === null) return

  const surface = document.createElement('div')
  surface.className = 'rdt-surface'
  root.appendChild(surface)

  const debug = document.createElement('pre')
  debug.className = 'rdt-debug'

  const { doc, selection } = parseHtmlToDoc(SAMPLE, html5SubsetSchema)
  const view = new EditorViewDom(surface, {
    doc,
    schema: html5SubsetSchema,
    initialSelection: selection,
    onChange: renderDebug
  })

  function renderDebug(state: EditorViewState): void {
    debug.textContent = JSON.stringify(
      {
        selection: state.selection,
        stored_marks: state.stored_marks,
        history_depth: state.history.undo.length
      },
      null,
      2
    )
  }

  root.appendChild(debug)
  renderDebug(view.getState())
}

main()
