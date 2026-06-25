// Top-level React editor component.
//
// Renders a Doc as contenteditable HTML. Attaches a NATIVE `beforeinput`
// listener (rather than React's onBeforeInput) — gives us reliable access to
// the InputEvent object and works consistently across jsdom + browsers. The
// listener translates beforeinput → InputIntent → command → Transaction →
// state update. Selection sync is best-effort: after each commit we set the
// DOM selection to match state.selection.

import { useCallback, useEffect, useRef, type KeyboardEvent } from 'react'
import { dispatchIntent } from '../input/intent.js'
import { renderDoc } from './render.js'
import { useEditorState } from './use-editor-state.js'
import { intentFromInputEvent } from './intent-from-input-event.js'
import { setDomSelectionFromSource } from './dom-bridge.js'
import type { EditorViewState } from './use-editor-state.js'
import type { Doc, Selection } from '../model/types.js'
import type { Schema } from '../model/schema.js'

export interface EditorViewProps {
  doc: Doc
  schema: Schema
  initialSelection: Selection | null
  onChange?: (state: EditorViewState) => void
}

export function EditorView(props: EditorViewProps) {
  const [state, dispatch] = useEditorState({
    doc: props.doc,
    schema: props.schema,
    selection: props.initialSelection
  })
  const rootRef = useRef<HTMLDivElement | null>(null)

  // Keep a ref to current state so the native event listener sees the latest
  // value without needing to re-attach on every render.
  const stateRef = useRef(state)
  useEffect(() => { stateRef.current = state }, [state])

  // Fire onChange whenever state moves.
  useEffect(() => {
    props.onChange?.(state)
  }, [state, props.onChange])

  // After every render, sync DOM selection back to state.selection.
  useEffect(() => {
    if (rootRef.current === null) return
    setDomSelectionFromSource(rootRef.current, state.selection)
  }, [state.selection, state.doc])

  // Attach native beforeinput listener once on mount.
  useEffect(() => {
    const el = rootRef.current
    if (el === null) return
    const handler = (ev: Event) => {
      const inputEv = ev as InputEvent
      const intent = intentFromInputEvent(inputEv)
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
  }, [dispatch])

  const handleKeyDown = useCallback((e: KeyboardEvent<HTMLDivElement>) => {
    // Cmd/Ctrl+Z and Shift-variant for undo/redo. We handle these here rather
    // than via beforeinput because not all platforms emit historyUndo intents
    // via beforeinput.
    const isMeta = e.metaKey || e.ctrlKey
    if (!isMeta) return
    if (e.key === 'z' && !e.shiftKey) {
      e.preventDefault()
      dispatch({ type: 'undo' })
    } else if ((e.key === 'z' && e.shiftKey) || e.key === 'y') {
      e.preventDefault()
      dispatch({ type: 'redo' })
    }
  }, [dispatch])

  return (
    <div
      ref={rootRef}
      contentEditable
      suppressContentEditableWarning
      onKeyDown={handleKeyDown}
    >
      {renderDoc(state.doc)}
    </div>
  )
}
