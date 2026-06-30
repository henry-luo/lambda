// Pure editor state + reducer (React-free).
//
// Extracted from use-editor-state.ts so the plain-DOM controller (Stage 4B)
// and the future Radiant host can drive the identical reducer with no React
// import in their module graph. `use-editor-state.ts` re-exports these and adds
// the React `useReducer` wrapper.

import { historyNew, historyPush, historyRedo, historyUndo } from '../model/history.js'
import { txGetMeta } from '../model/transaction.js'
import type { Doc, History, MarkDict, Selection, Transaction } from '../model/types.js'
import type { Schema } from '../model/schema.js'

export interface EditorViewState {
  doc: Doc
  schema: Schema
  selection: Selection | null
  stored_marks: MarkDict | null
  history: History
}

export type EditorAction =
  | { type: 'apply';         tx: Transaction }
  | { type: 'set_selection'; sel: Selection | null }
  | { type: 'set_doc';       doc: Doc; selection: Selection | null }
  | { type: 'undo' }
  | { type: 'redo' }

export function initialEditorState(args: {
  doc: Doc
  schema: Schema
  selection: Selection | null
}): EditorViewState {
  return {
    doc: args.doc,
    schema: args.schema,
    selection: args.selection,
    stored_marks: null,
    history: historyNew()
  }
}

export function editorReducer(state: EditorViewState, action: EditorAction): EditorViewState {
  switch (action.type) {
    case 'apply': {
      const tx = action.tx
      const addToHistory = txGetMeta(tx, 'addToHistory') !== false
      const stored = txGetMeta(tx, 'storedMarks')
      const hist = addToHistory ? historyPush(state.history, tx) : state.history
      return {
        doc: tx.doc_after,
        schema: state.schema,
        selection: tx.sel_after,
        stored_marks: (stored as MarkDict | null) ?? state.stored_marks,
        history: hist
      }
    }
    case 'set_selection':
      return { ...state, selection: action.sel }
    case 'set_doc':
      return {
        ...state,
        doc: action.doc,
        selection: action.selection,
        history: historyNew()
      }
    case 'undo': {
      const r = historyUndo(state.history, state.doc)
      if (!r.ok) return state
      return { ...state, doc: r.doc, selection: r.sel, history: r.hist }
    }
    case 'redo': {
      const r = historyRedo(state.history, state.doc)
      if (!r.ok) return state
      return { ...state, doc: r.doc, selection: r.sel, history: r.hist }
    }
  }
}
