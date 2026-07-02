import { build } from 'esbuild'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const entry = `
import { EditorViewDom } from './src/view/editor-view-dom.js'
import { html5SubsetSchema } from './src/schemas/index.js'
import { parseHtmlToDoc } from './src/view/html-parser.js'
// module-level mount(), exactly like the real test
function mount(html) {
  const { doc, selection } = parseHtmlToDoc(html, html5SubsetSchema)
  const root = document.createElement('div'); document.body.appendChild(root)
  const seenStates = []
  const view = new EditorViewDom(root, { doc, schema: html5SubsetSchema, initialSelection: selection, onChange: s => seenStates.push(s) })
  return { root, view, seenStates }
}
function fireBeforeInput(el, init) {
  el.dispatchEvent(new InputEvent('beforeinput', { inputType: init.inputType, data: init.data ?? null, bubbles: true, cancelable: true }))
}
// call from a nested closure (mimics it(() => {...}))
const testFn = () => {
  const { root, seenStates } = mount('<doc><p>helo<cursor></cursor></p></doc>')
  fireBeforeInput(root, { inputType: 'insertText', data: '!' })
  console.log('seen.length=' + seenStates.length)
  if (seenStates.length) console.log('doc text=' + JSON.stringify(seenStates[seenStates.length-1].doc.content[0].content))
}
testFn()
`
await build({ stdin:{contents:entry,resolveDir:root,sourcefile:'_spike.ts',loader:'ts'}, bundle:true, format:'iife', platform:'neutral', outfile:path.resolve(root,'../../temp/4c-spikes/spike_evd2.js'), logLevel:'warning' })
console.error('bundled')
