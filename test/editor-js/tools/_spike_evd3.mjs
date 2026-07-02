import { build } from 'esbuild'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const entry = `
import { EditorViewDom } from './src/view/editor-view-dom.js'
import { html5SubsetSchema } from './src/schemas/index.js'
import { parseHtmlToDoc } from './src/view/html-parser.js'
import { intentFromInputEvent } from './src/view/intent-from-input-event.js'
import { dispatchIntent } from './src/input/intent.js'
function mount(html) {
  const { doc, selection } = parseHtmlToDoc(html, html5SubsetSchema)
  const root = document.createElement('div'); document.body.appendChild(root)
  const seenStates = []
  const view = new EditorViewDom(root, { doc, schema: html5SubsetSchema, initialSelection: selection, onChange: s => { globalThis.__ocCalled = true; seenStates.push(s) } })
  return { root, view, seenStates, doc, selection, schema: html5SubsetSchema }
}
const testFn = () => {
  const { root, seenStates, doc, selection, schema } = mount('<doc><p>helo<cursor></cursor></p></doc>')
  // probe the pipeline pieces from THIS nested scope
  const ev = new InputEvent('beforeinput', { inputType:'insertText', data:'!', bubbles:true, cancelable:true })
  const it = intentFromInputEvent(ev)
  console.log('intent=' + JSON.stringify(it))
  const tx = dispatchIntent({ doc, schema, selection, stored_marks: null }, it)
  console.log('tx=' + (tx ? 'ok' : 'null'))
  globalThis.__ocCalled = false
  root.dispatchEvent(new InputEvent('beforeinput', { inputType:'insertText', data:'!', bubbles:true, cancelable:true }))
  console.log('ocCalled=' + globalThis.__ocCalled + ' seen=' + seenStates.length)
}
testFn()
`
await build({ stdin:{contents:entry,resolveDir:root,sourcefile:'_spike.ts',loader:'ts'}, bundle:true, format:'iife', platform:'neutral', outfile:path.resolve(root,'../../temp/4c-spikes/spike_evd3.js'), logLevel:'warning' })
console.error('bundled')
