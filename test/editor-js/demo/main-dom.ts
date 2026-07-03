// Vanilla-DOM demo bootstrap (Stage 4B) — no React.
//
// Mounts the full WYSIWYG shell (FullEditorDom: toolbar + contenteditable
// surface + overlays) entirely on plain DOM. This is the single-file page that
// runs in the browser AND under Radiant (`./lambda.exe view editor-dom.html`).

import { docSchema } from '../src/schemas/doc.js'
import { parseHtmlToDoc } from '../src/view/html-parser.js'
import { FullEditorDom } from './full-editor-dom.js'

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
  <h1>Lambda Editor <cursor></cursor>— plain-DOM (Stage 4B)</h1>
  <p>
    A <span style="font-weight: bold">WYSIWYG editor</span> running on plain DOM —
    no React. Try <span style="font-style: italic">italic</span>,
    <span style="text-decoration: underline">underline</span>,
    <span style="color: #e11d48">colored text</span>,
    <span style="background-color: #fef08a">highlights</span>,
    <code>inline code</code>, and
    <a href="https://github.com/henry-luo/lambda">links</a>.
    Select text and use the toolbar, or press <code>Cmd/Ctrl+B / I / U</code>.
  </p>
  <blockquote><p>Quotes preserve structure. Tab/Shift-Tab indent list items.</p></blockquote>
  <h2>Lists</h2>
  <ul>
    <li>Bulleted item one</li>
    <li>Bulleted item with <span style="font-weight: bold">bold</span> inside</li>
    <li>Bulleted item three</li>
  </ul>
  <h2>Image</h2>
  <figure><img src="${SAMPLE_IMG}" alt="Sample"></img></figure>
  <p>Click the image to select and resize it.</p>
</doc>
`

const TABLE_DOC = `
<doc>
  <h1>Table <cursor></cursor>column resize</h1>
  <p>Drag a column border to resize.</p>
  <table><tbody>
    <tr><td>Alpha</td><td>Beta</td><td>Gamma</td></tr>
    <tr><td>One</td><td>Two</td><td>Three</td></tr>
  </tbody></table>
  <p>Trailing paragraph.</p>
</doc>
`

function main(): void {
  const root = document.getElementById('root')
  if (root === null) return
  const seed = (window as unknown as { __RDT_SEED?: string }).__RDT_SEED
  const src = seed === 'table' ? TABLE_DOC : RICH_DOC
  const { doc, selection } = parseHtmlToDoc(src, docSchema)
  new FullEditorDom(root, { doc, schema: docSchema, initialSelection: selection })
}

main()
