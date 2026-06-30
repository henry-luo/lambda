// Vanilla-DOM editor controller (Stage 4B) — the framework-free twin of
// EditorView.tsx. It owns a contenteditable root, attaches NATIVE `beforeinput`
// and `keydown` listeners, drives the same pure `editorReducer`, and commits
// via the keyed reconciler. Selection is synced source→DOM after each commit.
// No React, no synthetic events — this is the controller that runs under
// Radiant on the LambdaJS DOM.

import { dispatchIntent } from '../input/intent.js'
import { intentFromInputEvent } from './intent-from-input-event.js'
import { renderDoc } from './render-vnode.js'
import { reconcileDoc } from './reconcile.js'
import { setDomSelectionFromSource } from './dom-bridge.js'
import {
  editorReducer,
  initialEditorState,
  type EditorAction,
  type EditorViewState
} from './editor-state.js'
import type { Doc, Selection } from '../model/types.js'
import type { Schema } from '../model/schema.js'

export interface EditorViewDomOptions {
  doc: Doc
  schema: Schema
  initialSelection: Selection | null
  onChange?: (state: EditorViewState) => void
}

export class EditorViewDom {
  readonly root: HTMLElement
  private state: EditorViewState
  private readonly onChange: ((s: EditorViewState) => void) | undefined

  constructor(root: HTMLElement, opts: EditorViewDomOptions) {
    this.root = root
    this.onChange = opts.onChange
    this.state = initialEditorState({
      doc: opts.doc,
      schema: opts.schema,
      selection: opts.initialSelection
    })
    root.setAttribute('contenteditable', 'true')
    root.addEventListener('beforeinput', this.handleBeforeInput)
    root.addEventListener('keydown', this.handleKeyDown as EventListener)
    this.render()
  }

  destroy(): void {
    this.root.removeEventListener('beforeinput', this.handleBeforeInput)
    this.root.removeEventListener('keydown', this.handleKeyDown as EventListener)
  }

  getState(): EditorViewState {
    return this.state
  }

  // Apply an action through the pure reducer, re-render, notify.
  dispatch = (action: EditorAction): void => {
    this.state = editorReducer(this.state, action)
    this.render()
    this.onChange?.(this.state)
  }

  private render(): void {
    reconcileDoc(this.root, renderDoc(this.state.doc))
    setDomSelectionFromSource(this.root, this.state.selection)
  }

  // beforeinput → InputIntent → command → Transaction → state. We intercept
  // and preventDefault — the editor, not the browser, performs every edit.
  private handleBeforeInput = (ev: Event): void => {
    const intent = intentFromInputEvent(ev as InputEvent)
    if (intent === null) return
    ev.preventDefault()
    const cur = this.state
    const tx = dispatchIntent(
      { doc: cur.doc, schema: cur.schema, selection: cur.selection, stored_marks: cur.stored_marks },
      intent
    )
    if (tx !== null) this.dispatch({ type: 'apply', tx })
  }

  // Undo/redo via Cmd/Ctrl+Z (+Shift) / Ctrl+Y — handled here rather than via
  // beforeinput because not all platforms emit historyUndo input events.
  private handleKeyDown = (ev: KeyboardEvent): void => {
    const isMeta = ev.metaKey || ev.ctrlKey
    if (!isMeta) return
    if (ev.key === 'z' && !ev.shiftKey) {
      ev.preventDefault()
      this.dispatch({ type: 'undo' })
    } else if ((ev.key === 'z' && ev.shiftKey) || ev.key === 'y') {
      ev.preventDefault()
      this.dispatch({ type: 'redo' })
    }
  }
}
