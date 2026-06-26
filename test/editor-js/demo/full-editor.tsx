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
  useReducer,
  useRef,
  useState,
  type KeyboardEvent,
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
  getSourceSelectionFromDom,
  setDomSelectionFromSource
} from '../src/view/dom-bridge'
import {
  cmdFormatBold,
  cmdFormatCode,
  cmdFormatItalic,
  cmdFormatUnderline,
  cmdSetBlockType,
  cmdToggleMark
} from '../src/commands/text-commands'
import { isText, nodeAt } from '../src/model/doc'
import type { EditorState } from '../src/commands/types'
import type {
  AttrValue,
  Doc,
  MarkDict,
  Selection,
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

  const [, forceTick] = useState(0)

  // After any state change, project the source selection onto the DOM.
  useEffect(() => {
    if (rootRef.current === null) return
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
      dispatch({ type: 'set_selection', sel: next })
    }
    document.addEventListener('selectionchange', onSelChange)
    return () => document.removeEventListener('selectionchange', onSelChange)
  }, [])

  const handleKeyDown = useCallback((e: KeyboardEvent<HTMLDivElement>) => {
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

  const marks = activeMarks(state.doc, state.selection)
  const block = activeBlock(state.doc, state.selection)

  return (
    <div className="rdt-shell">
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
        onColor={value => runCmd(s => cmdToggleMark(s, 'color', value))}
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
      >
        {renderDoc(state.doc)}
      </div>
      <StatusBar state={state} />
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

      <label className="rdt-color" title="Text color">
        <span style={{ color: typeof p.marks['color'] === 'string' ? (p.marks['color'] as string) : '#111' }}>A</span>
        <input type="color" onChange={e => p.onColor(e.target.value)} defaultValue="#e11d48" />
      </label>
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
