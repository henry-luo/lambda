// Bootstrap for the WYSIWYG editor test page.
//
// Authors a rich preloaded document (styled text, lists, a table, an image,
// and an embedded drawing) as an HTML string, parses it into the editor's
// Mark-tree model, and mounts the toolbar + editor surface.

import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { parseHtmlToDoc } from '../src/view/html-parser'
import { docSchema } from '../src/schemas/doc'
import { FullEditor } from './full-editor'

// A small offline SVG used for the embedded <img> (data URI so the single-file
// build needs no network).
const SAMPLE_IMG =
  "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='260' height='130'%3E" +
  "%3Cdefs%3E%3ClinearGradient id='g' x1='0' y1='0' x2='1' y2='1'%3E" +
  "%3Cstop offset='0' stop-color='%2360a5fa'/%3E%3Cstop offset='1' stop-color='%23a78bfa'/%3E" +
  "%3C/linearGradient%3E%3C/defs%3E" +
  "%3Crect width='260' height='130' rx='10' fill='url(%23g)'/%3E" +
  "%3Ctext x='130' y='72' font-family='system-ui' font-size='20' font-weight='600' text-anchor='middle' fill='white'%3ESample Image%3C/text%3E" +
  "%3C/svg%3E"

const RICH_DOC = `
<doc>
  <h1>Lambda Editor <cursor></cursor>— Rich Document Demo</h1>

  <p>
    This is a <span style="font-weight: bold">WYSIWYG editor</span> built on the
    Lambda/Radiant editor package. It supports
    <span style="font-style: italic">italic</span>,
    <span style="text-decoration: underline">underline</span>,
    <span style="text-decoration: line-through">strikethrough</span>,
    <span style="color: #e11d48">colored text</span>,
    <span style="background-color: #fef08a">highlights</span>,
    <code>inline code</code>, and
    <a href="https://github.com/henry-luo/lambda">links</a>. Select any text and
    use the toolbar above, or press <code>Cmd/Ctrl+B</code> / <code>I</code> /
    <code>U</code>.
  </p>

  <blockquote>
    <p>Marks are stored as a flat dictionary on each text leaf — no nested
    inline elements. Toggling bold over a range splits the leaf and sets the
    mark on the middle piece.</p>
  </blockquote>

  <h2>Lists</h2>
  <ul>
    <li>Bulleted item one</li>
    <li>Bulleted item with <span style="font-weight: bold">bold</span> inside</li>
    <li>Bulleted item three</li>
  </ul>
  <ol>
    <li>Numbered step one</li>
    <li>Numbered step two</li>
    <li>Numbered step three</li>
  </ol>

  <h2>Embedded Image</h2>
  <figure>
    <img src="${SAMPLE_IMG}" alt="Sample gradient image"></img>
    <figcaption>An inline image embedded in the document.</figcaption>
  </figure>

  <h2>Table</h2>
  <table>
    <tr>
      <th>Shape</th>
      <th>Kind</th>
      <th>Selectable</th>
    </tr>
    <tr>
      <td>Rectangle</td>
      <td><code>rect</code></td>
      <td>yes</td>
    </tr>
    <tr>
      <td>Ellipse</td>
      <td><code>ellipse</code></td>
      <td>yes</td>
    </tr>
    <tr>
      <td>Connector</td>
      <td><code>connector</code></td>
      <td>yes</td>
    </tr>
  </table>

  <h2>Embedded Drawing</h2>
  <p>The block below is a <span style="font-weight: bold">drawing</span> — an
  SVG-rendered canvas of shapes and a routed connector, stored in the same
  document tree as the text.</p>
  <drawing id="D1" width="460" height="200" bg="#f8fafc">
    <layer id="L1">
      <shape id="S1" kind="rect" x="30" y="60" width="130" height="70"
             fill="#dbeafe" stroke="#3b82f6" stroke-width="2"></shape>
      <shape id="S2" kind="ellipse" x="300" y="55" width="130" height="80"
             fill="#fce7f3" stroke="#db2777" stroke-width="2"></shape>
      <shape id="S3" kind="rect" x="180" y="20" width="100" height="40"
             fill="#dcfce7" stroke="#16a34a" stroke-width="2"></shape>
      <connector id="C1" from-shape="S1" to-shape="S2" routing="orthogonal"
                 stroke="#475569" stroke-width="2" end-arrow="arrow"></connector>
      <connector id="C2" from-shape="S3" to-shape="S2" routing="curved"
                 stroke="#16a34a" stroke-width="2" end-arrow="arrow"></connector>
    </layer>
  </drawing>

  <p>Keep typing below — every keystroke is a typed transaction over the source
  tree, with full undo/redo (<code>Cmd/Ctrl+Z</code>).</p>

  <hr>
  <p>The rule above is a block atom: arrow into it (or click it) and a <strong>gap
  cursor</strong> appears — a caret position between blocks where no text lives.
  Type there to insert a paragraph, or press Backspace/Delete to remove the rule.</p>
</doc>
`

function mount() {
  const root = document.getElementById('root')
  if (root === null) return
  let parsed
  try {
    parsed = parseHtmlToDoc(RICH_DOC, docSchema)
  } catch (err) {
    root.textContent = 'Failed to parse document: ' + String(err)
    return
  }
  createRoot(root).render(
    <StrictMode>
      <FullEditor
        doc={parsed.doc}
        schema={docSchema}
        initialSelection={parsed.selection}
      />
    </StrictMode>
  )
}

mount()
