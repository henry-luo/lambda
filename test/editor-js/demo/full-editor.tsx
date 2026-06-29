// FullEditor — a WYSIWYG editor shell with a toolbar.
//
// Composes the editor primitives (state reducer, renderDoc, input-intent
// dispatch, dom-bridge selection sync) and adds:
//   - a toolbar that dispatches commands against the live selection
//   - DOM-selection → source-selection tracking (so the toolbar follows the
//     user's caret), which the bare EditorView does not do.
//
// This file lives in demo/ and does NOT modify src/. It is the integration
// surface a real application would build on top of the editor package.

import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useReducer,
  useRef,
  useState,
  type KeyboardEvent,
  type MouseEvent as ReactMouseEvent,
  type ReactNode
} from 'react'
import {
  editorReducer,
  initialEditorState,
  type EditorViewState
} from '../src/view/use-editor-state'
import { renderDoc } from '../src/view/render'
import { dispatchIntent } from '../src/input/intent'
import { intentFromInputEvent } from '../src/view/intent-from-input-event'
import {
  findElementByPath,
  getSourceSelectionFromDom,
  nearestPathOwner,
  pathOf,
  setDomSelectionFromSource
} from '../src/view/dom-bridge'
import {
  cmdFormatBold,
  cmdFormatCode,
  cmdFormatItalic,
  cmdFormatUnderline,
  cmdSetBlockType,
  cmdToggleMark,
  cmdSetMark,
  cmdRemoveMark
} from '../src/commands/text-commands'
import { cmdResizeImage, cmdInsertImage, cmdIndentListItem, cmdOutdentListItem } from '../src/commands/structural-commands'
import { cmdPasteSlice } from '../src/commands/paste'
import { parseHtmlToDoc, serializeDocToHtml } from '../src/view/html-parser'
import { isNode, isText, nodeAt } from '../src/model/doc'
import type { EditorState } from '../src/commands/types'
import type {
  AttrValue,
  Doc,
  MarkDict,
  Selection,
  SourcePath,
  Transaction
} from '../src/model/types'
import type { Schema } from '../src/model/schema'

export interface FullEditorProps {
  doc: Doc
  schema: Schema
  initialSelection: Selection | null
}

type Command = (state: EditorState) => Transaction | null

function selKey(sel: Selection | null): string {
  return sel === null ? 'null' : JSON.stringify(sel)
}

// Pull the meaningful fragment out of clipboard text/html (strip the wrapper
// chrome and the StartFragment/EndFragment comments browsers add).
function extractClipboardFragment(html: string): string {
  const frag = html.match(/<!--StartFragment-->([\s\S]*?)<!--EndFragment-->/)
  if (frag) return frag[1]!
  const body = html.match(/<body[^>]*>([\s\S]*)<\/body>/i)
  return (body ? body[1]! : html).trim()
}

// Convert pasted plain text to HTML: blank lines split paragraphs, single
// newlines become <br>.
function plainTextToHtml(text: string): string {
  if (text === '') return ''
  const esc = (s: string) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  return text.split(/\n\n+/).map(p => `<p>${esc(p).replace(/\n/g, '<br>')}</p>`).join('')
}

// Active marks at the current selection (read from the text leaf the anchor
// sits in). Powers toolbar button highlighting.
function activeMarks(doc: Doc, sel: Selection | null): MarkDict {
  if (sel === null || sel.kind !== 'text') return {}
  const leaf = nodeAt(doc, sel.anchor.path)
  if (leaf !== null && isText(leaf)) return leaf.marks
  return {}
}

// Current block tag at the selection (the block ancestor of the anchor leaf).
function activeBlock(doc: Doc, sel: Selection | null): string {
  if (sel === null || sel.kind !== 'text') return ''
  const path = sel.anchor.path
  if (path.length < 1) return ''
  const blockPath = path.slice(0, path.length - 1)
  const block = nodeAt(doc, blockPath)
  return block !== null && block.kind === 'node' ? block.tag : ''
}

export function FullEditor(props: FullEditorProps) {
  const [state, dispatch] = useReducer(
    editorReducer,
    { doc: props.doc, schema: props.schema, selection: props.initialSelection },
    initialEditorState
  )
  const rootRef = useRef<HTMLDivElement | null>(null)
  const stateRef = useRef<EditorViewState>(state)
  useEffect(() => { stateRef.current = state }, [state])

  // When the selection update originated from the user's own DOM selection,
  // the DOM is already correct — re-pushing source→DOM here would fight an
  // active drag (each removeAllRanges/addRange resets the in-progress
  // selection). This flag tells the projection effect to skip exactly once.
  const selectionFromDom = useRef(false)

  const [, forceTick] = useState(0)

  // After a transaction (doc or programmatic selection change), project the
  // source selection onto the DOM. Skip when the change came from the DOM.
  useEffect(() => {
    if (rootRef.current === null) return
    if (selectionFromDom.current) {
      selectionFromDom.current = false
      return
    }
    setDomSelectionFromSource(rootRef.current, state.selection)
  }, [state.selection, state.doc])

  // Focus the surface on mount so the caret is live immediately.
  useEffect(() => {
    rootRef.current?.focus()
  }, [])

  // Native beforeinput → InputIntent → command → transaction.
  useEffect(() => {
    const el = rootRef.current
    if (el === null) return
    const handler = (ev: Event) => {
      const intent = intentFromInputEvent(ev as InputEvent)
      if (intent === null) return
      ev.preventDefault()
      const cur = stateRef.current
      const tx = dispatchIntent(
        { doc: cur.doc, schema: cur.schema, selection: cur.selection, stored_marks: cur.stored_marks },
        intent
      )
      if (tx !== null) dispatch({ type: 'apply', tx })
    }
    el.addEventListener('beforeinput', handler)
    return () => el.removeEventListener('beforeinput', handler)
  }, [])

  // Native paste → parse clipboard HTML (or plain text) into a slice → cmdPasteSlice.
  useEffect(() => {
    const el = rootRef.current
    if (el === null) return
    const handler = (ev: ClipboardEvent) => {
      const cb = ev.clipboardData
      if (cb === null) return
      // An image file on the clipboard (copied image / screenshot) → insert <img>.
      const imgItem = Array.from(cb.items ?? []).find(it => it.kind === 'file' && it.type.startsWith('image/'))
      if (imgItem !== undefined) {
        const file = imgItem.getAsFile()
        if (file !== null) {
          ev.preventDefault()
          const reader = new FileReader()
          reader.onload = () => {
            const cur = stateRef.current
            const tx = cmdInsertImage(
              { doc: cur.doc, schema: cur.schema, selection: cur.selection, stored_marks: cur.stored_marks },
              String(reader.result), ''
            )
            if (tx !== null) dispatch({ type: 'apply', tx })
          }
          reader.readAsDataURL(file)
          return
        }
      }
      const html = cb.getData('text/html')
      const fragment = html.trim() !== ''
        ? extractClipboardFragment(html)
        : plainTextToHtml(cb.getData('text/plain'))
      if (fragment === '') return
      ev.preventDefault()
      const cur = stateRef.current
      const parsed = parseHtmlToDoc(`<doc>${fragment}</doc>`, cur.schema)
      const tx = cmdPasteSlice(
        { doc: cur.doc, schema: cur.schema, selection: cur.selection, stored_marks: cur.stored_marks },
        parsed.doc.content
      )
      if (tx !== null) dispatch({ type: 'apply', tx })
    }
    el.addEventListener('paste', handler)
    return () => el.removeEventListener('paste', handler)
  }, [])

  // Native copy/cut → serialize a selected node (e.g. an image) to the clipboard
  // as HTML so it can be pasted back. (Text selections use the browser default.)
  useEffect(() => {
    const el = rootRef.current
    if (el === null) return
    const onCopy = (ev: ClipboardEvent) => {
      const cur = stateRef.current
      const sel = cur.selection
      if (sel === null || sel.kind !== 'node') return
      const n = nodeAt(cur.doc, sel.path)
      if (n === null || !isNode(n) || ev.clipboardData === null) return
      ev.preventDefault()
      ev.clipboardData.setData('text/html', serializeDocToHtml(n, { schema: cur.schema }))
      ev.clipboardData.setData('text/plain', '')
    }
    el.addEventListener('copy', onCopy)
    return () => el.removeEventListener('copy', onCopy)
  }, [])

  // DOM selection → source selection. Lets the toolbar follow the caret.
  useEffect(() => {
    const onSelChange = () => {
      const root = rootRef.current
      if (root === null) return
      const dom = window.getSelection()
      if (dom === null || dom.rangeCount === 0) return
      // Only track selections that live inside the editor root.
      if (!root.contains(dom.anchorNode)) return
      const src = getSourceSelectionFromDom()
      if (src === null) return
      const next: Selection = { kind: 'text', anchor: src.anchor, head: src.head }
      if (selKey(next) === selKey(stateRef.current.selection)) {
        // No model change, but refresh toolbar active-state.
        forceTick(t => t + 1)
        return
      }
      // The DOM selection is already correct — don't re-project it back.
      selectionFromDom.current = true
      dispatch({ type: 'set_selection', sel: next })
    }
    document.addEventListener('selectionchange', onSelChange)
    return () => document.removeEventListener('selectionchange', onSelChange)
  }, [])

  const handleKeyDown = useCallback((e: KeyboardEvent<HTMLDivElement>) => {
    // Tab / Shift+Tab indent/outdent the current list item (from any caret
    // position inside it). No-op outside a list item.
    if (e.key === 'Tab') {
      e.preventDefault()
      runCmd(e.shiftKey ? cmdOutdentListItem : cmdIndentListItem)
      return
    }
    const meta = e.metaKey || e.ctrlKey
    if (!meta) return
    if (e.key === 'z' && !e.shiftKey) { e.preventDefault(); dispatch({ type: 'undo' }) }
    else if ((e.key === 'z' && e.shiftKey) || e.key === 'y') { e.preventDefault(); dispatch({ type: 'redo' }) }
    else if (e.key === 'b') { e.preventDefault(); runCmd(cmdFormatBold) }
    else if (e.key === 'i') { e.preventDefault(); runCmd(cmdFormatItalic) }
    else if (e.key === 'u') { e.preventDefault(); runCmd(cmdFormatUnderline) }
  }, [])

  const runCmd = useCallback((cmd: Command) => {
    const cur = stateRef.current
    const tx = cmd({ doc: cur.doc, schema: cur.schema, selection: cur.selection, stored_marks: cur.stored_marks })
    if (tx !== null) dispatch({ type: 'apply', tx })
  }, [])

  const shellRef = useRef<HTMLDivElement | null>(null)

  // Click on an image → select it as a node (so it can be resized/deleted).
  const handleSurfaceMouseDown = useCallback((e: ReactMouseEvent<HTMLDivElement>) => {
    const t = e.target as HTMLElement
    if (t.tagName === 'IMG') {
      const owner = nearestPathOwner(t)
      if (owner !== null) {
        e.preventDefault()
        dispatch({ type: 'set_selection', sel: { kind: 'node', path: pathOf(owner) } })
      }
    }
  }, [])

  // The image currently selected as a node (path), if any.
  const selectedImagePath: SourcePath | null =
    state.selection?.kind === 'node'
      ? (() => {
          const n = nodeAt(state.doc, state.selection.path)
          return n !== null && isNode(n) && n.tag === 'img' ? state.selection.path : null
        })()
      : null

  const marks = activeMarks(state.doc, state.selection)
  const block = activeBlock(state.doc, state.selection)

  return (
    <div className="rdt-shell" ref={shellRef}>
      <Toolbar
        marks={marks}
        block={block}
        onMark={(name, value) => runCmd(s => cmdToggleMark(s, name, value))}
        onBold={() => runCmd(cmdFormatBold)}
        onItalic={() => runCmd(cmdFormatItalic)}
        onUnderline={() => runCmd(cmdFormatUnderline)}
        onCode={() => runCmd(cmdFormatCode)}
        onBlock={tag => runCmd(s => cmdSetBlockType(s, tag))}
        onLink={() => {
          const url = window.prompt('Link URL:', 'https://')
          if (url) runCmd(s => cmdToggleMark(s, 'link', url))
        }}
        onColor={value => runCmd(s => cmdSetMark(s, 'color', value))}
        onClearColor={() => runCmd(s => cmdRemoveMark(s, 'color'))}
        onUndo={() => dispatch({ type: 'undo' })}
        onRedo={() => dispatch({ type: 'redo' })}
        canUndo={state.history.undo.length > 0}
        canRedo={state.history.redo.length > 0}
      />
      <div
        ref={rootRef}
        className="rdt-surface"
        contentEditable
        suppressContentEditableWarning
        spellCheck={false}
        onKeyDown={handleKeyDown}
        onMouseDown={handleSurfaceMouseDown}
      >
        {renderDoc(state.doc)}
      </div>
      {selectedImagePath !== null && (
        <ImageResizeOverlay
          shellRef={shellRef}
          surfaceRef={rootRef}
          imagePath={selectedImagePath}
          docVersion={state.doc}
          onResize={(w, h) => runCmd(s => cmdResizeImage(s, selectedImagePath, w, h))}
        />
      )}
      <StatusBar state={state} />
    </div>
  )
}

// ---------------------------------------------------------------------------
// Image resize overlay — 8 handles around a node-selected image.
// ---------------------------------------------------------------------------

interface OverlayProps {
  shellRef: React.RefObject<HTMLDivElement | null>
  surfaceRef: React.RefObject<HTMLDivElement | null>
  imagePath: SourcePath
  docVersion: Doc
  onResize: (w: number, h: number) => void
}

interface Box { left: number; top: number; width: number; height: number }

function ImageResizeOverlay({ shellRef, surfaceRef, imagePath, docVersion, onResize }: OverlayProps) {
  const [box, setBox] = useState<Box | null>(null)
  const [preview, setPreview] = useState<Box | null>(null)
  const drag = useRef<{ corner: string; startX: number; startY: number; w: number; h: number; ratio: number } | null>(null)

  // Measure the image's position relative to the shell after each render, and
  // keep it aligned while the surface scrolls or the window resizes.
  useLayoutEffect(() => {
    const shell = shellRef.current
    const surface = surfaceRef.current
    if (shell === null || surface === null) return
    const measure = () => {
      const el = findElementByPath(surface, imagePath)
      if (el === null) { setBox(null); return }
      const sRect = shell.getBoundingClientRect()
      const r = el.getBoundingClientRect()
      setBox({ left: r.left - sRect.left, top: r.top - sRect.top, width: r.width, height: r.height })
    }
    measure()
    setPreview(null)
    surface.addEventListener('scroll', measure)
    window.addEventListener('resize', measure)
    return () => { surface.removeEventListener('scroll', measure); window.removeEventListener('resize', measure) }
  }, [imagePath, docVersion, shellRef, surfaceRef])

  useEffect(() => {
    function onMove(e: globalThis.MouseEvent) {
      const d = drag.current
      if (d === null || box === null) return
      const dx = e.clientX - d.startX
      // Corner drag: bottom-right grows with +dx; keep aspect ratio.
      const sign = d.corner.includes('e') ? 1 : -1
      const newW = Math.max(24, d.w + sign * dx)
      const newH = newW / d.ratio
      setPreview({ left: box.left, top: box.top, width: newW, height: newH })
    }
    function onUp() {
      const d = drag.current
      drag.current = null
      if (d !== null && preview !== null) onResize(preview.width, preview.height)
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => { window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp) }
  }, [box, preview, onResize])

  if (box === null) return null
  const shown = preview ?? box
  const startDrag = (corner: string) => (e: ReactMouseEvent) => {
    e.preventDefault(); e.stopPropagation()
    drag.current = { corner, startX: e.clientX, startY: e.clientY, w: box.width, h: box.height, ratio: box.width / box.height || 1 }
  }
  const handles = ['nw', 'ne', 'sw', 'se'] as const
  return (
    <div className="rdt-img-overlay" style={{ left: shown.left, top: shown.top, width: shown.width, height: shown.height }}>
      {handles.map(c => (
        <span key={c} className={'rdt-img-handle rdt-h-' + c} onMouseDown={startDrag(c)} />
      ))}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

interface ToolbarProps {
  marks: MarkDict
  block: string
  onMark: (name: string, value?: AttrValue) => void
  onBold: () => void
  onItalic: () => void
  onUnderline: () => void
  onCode: () => void
  onBlock: (tag: string) => void
  onLink: () => void
  onColor: (value: string) => void
  onClearColor: () => void
  onUndo: () => void
  onRedo: () => void
  canUndo: boolean
  canRedo: boolean
}

function Toolbar(p: ToolbarProps) {
  // Keep editor focus when clicking toolbar controls.
  const keepFocus = (e: React.MouseEvent) => e.preventDefault()
  return (
    <div className="rdt-toolbar" onMouseDown={keepFocus}>
      <select
        className="rdt-block"
        value={['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'blockquote'].includes(p.block) ? p.block : 'p'}
        onChange={e => p.onBlock(e.target.value)}
        // The toolbar's onMouseDown preventDefault (keepFocus) would stop a native
        // <select> from opening — let this control receive its own mousedown.
        onMouseDown={e => e.stopPropagation()}
        title="Block type"
      >
        <option value="p">Paragraph</option>
        <option value="h1">Heading 1</option>
        <option value="h2">Heading 2</option>
        <option value="h3">Heading 3</option>
        <option value="h4">Heading 4</option>
        <option value="h5">Heading 5</option>
        <option value="h6">Heading 6</option>
        <option value="blockquote">Quote</option>
      </select>

      <span className="rdt-sep" />

      <Btn active={p.marks['bold'] === true} onClick={p.onBold} title="Bold (Cmd+B)"><b>B</b></Btn>
      <Btn active={p.marks['italic'] === true} onClick={p.onItalic} title="Italic (Cmd+I)"><i>I</i></Btn>
      <Btn active={p.marks['underline'] === true} onClick={p.onUnderline} title="Underline (Cmd+U)"><u>U</u></Btn>
      <Btn active={p.marks['strikethrough'] === true} onClick={() => p.onMark('strikethrough')} title="Strikethrough"><s>S</s></Btn>
      <Btn active={p.marks['code'] === true} onClick={p.onCode} title="Code"><code>{'<>'}</code></Btn>
      <Btn active={'link' in p.marks} onClick={p.onLink} title="Link">🔗</Btn>

      <span className="rdt-sep" />

      <ColorPalette
        current={typeof p.marks['color'] === 'string' ? (p.marks['color'] as string) : null}
        onPick={p.onColor}
        onClear={p.onClearColor}
      />
      <Btn onClick={() => p.onMark('background', '#fef08a')} title="Highlight">🖍️</Btn>

      <span className="rdt-sep" />

      <Btn onClick={p.onUndo} disabled={!p.canUndo} title="Undo (Cmd+Z)">↶</Btn>
      <Btn onClick={p.onRedo} disabled={!p.canRedo} title="Redo (Cmd+Shift+Z)">↷</Btn>
    </div>
  )
}

function Btn(props: { active?: boolean; disabled?: boolean; onClick: () => void; title: string; children: ReactNode }) {
  return (
    <button
      type="button"
      className={'rdt-btn' + (props.active ? ' is-active' : '')}
      disabled={props.disabled}
      title={props.title}
      onMouseDown={e => e.preventDefault()}
      onClick={props.onClick}
    >
      {props.children}
    </button>
  )
}

// Text-color control: a swatch palette popover + a custom picker + a "default"
// (clear-color) action. Picking always SETS the colour (cmdSetMark), so re-picking
// replaces rather than toggling off.
const TEXT_COLORS = [
  '#0f172a', '#475569', '#94a3b8', '#ffffff', '#ef4444', '#f97316', '#eab308', '#84cc16',
  '#22c55e', '#14b8a6', '#06b6d4', '#3b82f6', '#2563eb', '#8b5cf6', '#d946ef', '#ec4899'
]

function ColorPalette(p: { current: string | null; onPick: (c: string) => void; onClear: () => void }) {
  const [open, setOpen] = useState(false)
  const wrapRef = useRef<HTMLSpanElement | null>(null)
  // Close when clicking outside the control.
  useEffect(() => {
    if (!open) return
    const onDown = (e: globalThis.MouseEvent) => {
      if (wrapRef.current !== null && !wrapRef.current.contains(e.target as Node)) setOpen(false)
    }
    document.addEventListener('mousedown', onDown)
    return () => document.removeEventListener('mousedown', onDown)
  }, [open])
  return (
    <span className="rdt-color-wrap" ref={wrapRef}>
      <button
        type="button"
        className="rdt-color"
        title="Text color"
        onMouseDown={e => e.preventDefault()}
        onClick={() => setOpen(o => !o)}
      >
        <span className="rdt-a" style={{ color: p.current ?? '#111827' }}>A</span>
      </button>
      {open && (
        <div className="rdt-palette" onMouseDown={e => e.preventDefault()}>
          {TEXT_COLORS.map(c => (
            <button
              key={c}
              type="button"
              className={'rdt-swatch' + (p.current === c ? ' is-current' : '')}
              style={{ background: c }}
              title={c}
              onClick={() => { p.onPick(c); setOpen(false) }}
            />
          ))}
          <label className="rdt-custom">
            Custom
            <input type="color" defaultValue={p.current ?? '#e11d48'} onChange={e => p.onPick(e.target.value)} />
          </label>
          <button type="button" className="rdt-clear" onClick={() => { p.onClear(); setOpen(false) }}>
            Default color
          </button>
        </div>
      )}
    </span>
  )
}

// ---------------------------------------------------------------------------
// Status bar (debug aid)
// ---------------------------------------------------------------------------

function StatusBar({ state }: { state: EditorViewState }) {
  const sel = state.selection
  const where = sel === null ? '—'
    : sel.kind === 'text'
      ? `[${sel.anchor.path.join(',')}]:${sel.anchor.offset}` +
        (selKey({ kind: 'text', anchor: sel.anchor, head: sel.anchor }) === selKey(sel) ? '' : ` → [${sel.head.path.join(',')}]:${sel.head.offset}`)
      : sel.kind
  return (
    <div className="rdt-status">
      <span>selection: <code>{where}</code></span>
      <span>history: {state.history.undo.length}↶ / {state.history.redo.length}↷</span>
    </div>
  )
}
