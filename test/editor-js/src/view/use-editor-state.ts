// React `useReducer` wrapper over the pure editor reducer.
//
// The reducer, actions, and state type live in editor-state.ts (React-free) so
// the plain-DOM controller and the Radiant host can drive the same reducer
// without importing React. This module re-exports them and adds the hook.
//
// State =
//   { doc, selection, schema, stored_marks, history }
// Callers:
//   1. Build a transaction via dispatchIntent or a command
//   2. Pass it to dispatch({type:'apply', tx})
// History push, stored_marks update, and metadata consumption happen inside
// the reducer.

import { useReducer } from 'react'
import {
  editorReducer,
  initialEditorState,
  type EditorAction,
  type EditorViewState
} from './editor-state.js'
import type { Doc, Selection } from '../model/types.js'
import type { Schema } from '../model/schema.js'

export {
  editorReducer,
  initialEditorState,
  type EditorAction,
  type EditorViewState
} from './editor-state.js'

export function useEditorState(args: {
  doc: Doc
  schema: Schema
  selection: Selection | null
}): [EditorViewState, (a: EditorAction) => void] {
  return useReducer(editorReducer, args, initialEditorState)
}
