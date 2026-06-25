// Shared types for the command layer.
//
// A Command takes an EditorState and returns a Transaction (or null if the
// command does not apply to the current state). The caller decides whether
// to commit the Tx and update history.

import type {
  Doc,
  MarkDict,
  Selection,
  Transaction
} from '../model/types.js'
import type { Schema } from '../model/schema.js'

export interface EditorState {
  doc: Doc
  schema: Schema
  selection: Selection | null
  stored_marks: MarkDict | null
}

export type Command = (state: EditorState) => Transaction | null

export type StringCommand = (state: EditorState, text: string) => Transaction | null
export type MarkCommand   = (state: EditorState, name: string) => Transaction | null
export type TagCommand    = (state: EditorState, tag: string) => Transaction | null
