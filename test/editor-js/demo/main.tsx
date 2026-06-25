import { StrictMode, useState } from 'react'
import { createRoot } from 'react-dom/client'
import { html5SubsetSchema } from '../src/schemas/index'
import { EditorView } from '../src/view/EditorView'
import { parseHtmlToDoc } from '../src/view/html-parser'
import type { EditorViewState } from '../src/view/use-editor-state'

const SAMPLE = `
<doc>
  <h1>Lambda Editor</h1>
  <p>Hello <strong>world</strong>. Type into the editor below.</p>
  <p><cursor></cursor>The caret should be at the start of this paragraph.</p>
  <ul>
    <li>List items work</li>
    <li>So do <em>marks</em></li>
  </ul>
  <blockquote>
    <p>Quotes preserve structure.</p>
  </blockquote>
</doc>
`

function App() {
  const initial = parseHtmlToDoc(SAMPLE, html5SubsetSchema)
  const [state, setState] = useState<EditorViewState | null>(null)

  return (
    <main style={{ fontFamily: 'system-ui', padding: 24, maxWidth: 720, margin: '0 auto' }}>
      <h1 style={{ marginTop: 0 }}>Lambda Editor — JS reference</h1>
      <p style={{ color: '#666' }}>
        A demo of the Stage-4 reactive editor architecture. The document below is editable;
        every keystroke routes through <code>beforeinput → InputIntent → command → Transaction</code>.
      </p>
      <div
        style={{
          border: '1px solid #ddd',
          borderRadius: 4,
          padding: 16,
          minHeight: 200
        }}
      >
        <EditorView
          doc={initial.doc}
          schema={html5SubsetSchema}
          initialSelection={initial.selection}
          onChange={setState}
        />
      </div>
      <details style={{ marginTop: 16 }}>
        <summary style={{ cursor: 'pointer' }}>Editor state (debug)</summary>
        <pre style={{ fontSize: 11, background: '#f6f6f6', padding: 8, overflow: 'auto' }}>
          {JSON.stringify({
            doc: state?.doc ?? initial.doc,
            selection: state?.selection ?? initial.selection,
            stored_marks: state?.stored_marks ?? null,
            history_depth: state?.history?.undo.length ?? 0
          }, null, 2)}
        </pre>
      </details>
    </main>
  )
}

const root = document.getElementById('root')
if (root) {
  createRoot(root).render(
    <StrictMode>
      <App />
    </StrictMode>
  )
}
