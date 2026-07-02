import { build } from 'esbuild'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const entry = `
import { EditorViewDom } from './src/view/editor-view-dom.js'
import { html5SubsetSchema } from './src/schemas/index.js'
import { parseHtmlToDoc } from './src/view/html-parser.js'
import { intentFromInputEvent } from './src/view/intent-from-input-event.js'
const { doc, selection } = parseHtmlToDoc('<doc><p>helo<cursor></cursor></p></doc>', html5SubsetSchema)
const root = document.createElement('div'); document.body.appendChild(root)
const seen = []
const view = new EditorViewDom(root, { doc, schema: html5SubsetSchema, initialSelection: selection, onChange: (s)=>seen.push(s) })
const ev = new InputEvent('beforeinput', { inputType:'insertText', data:'!', bubbles:true, cancelable:true })
try { const it = intentFromInputEvent(ev); console.log('direct intent=' + JSON.stringify(it)) }
catch(e){ console.log('direct intent THREW: ' + (e&&e.message)) }
root.dispatchEvent(new InputEvent('beforeinput', { inputType:'insertText', data:'!', bubbles:true, cancelable:true }))
console.log('seen.length=' + seen.length)
`
await build({ stdin:{contents:entry,resolveDir:root,sourcefile:'_spike.ts',loader:'ts'}, bundle:true, format:'iife', platform:'neutral', outfile:path.resolve(root,'../../temp/4c-spikes/spike_evd.js'), logLevel:'warning' })
console.error('bundled')
