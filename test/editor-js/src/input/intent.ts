// Input-intent dispatcher.
//
// Mirrors lambda/package/editor/mod_input_intent.ls: a single discriminated
// union representing all editor-bound intents (mapped from beforeinput events,
// keyboard shortcuts, clipboard, drag-drop, etc.) plus a `dispatchIntent`
// function that routes each one to the right command.
//
// Commands return a Transaction (or null). The dispatcher additionally
// decorates the result with bookkeeping metadata (historyGroup for typing,
// scrollIntoView=true). It does NOT commit history — the caller does.

import {
  cmdDeleteBackward,
  cmdDeleteForward,
  cmdFormatBold,
  cmdFormatItalic,
  cmdFormatUnderline,
  cmdInsertLineBreak,
  cmdInsertParagraph,
  cmdInsertText,
  cmdSelectAll,
  cmdSetBlockType,
  cmdToggleMark
} from '../commands/text-commands.js'
import { txSetMeta } from '../model/transaction.js'
import type { EditorState } from '../commands/types.js'
import type { Transaction } from '../model/types.js'

// ---------------------------------------------------------------------------
// Input intent union
// ---------------------------------------------------------------------------

export type InputIntent =
  | { type: 'insertText',           text: string }
  | { type: 'insertParagraph' }
  | { type: 'insertLineBreak' }
  | { type: 'deleteContentBackward' }
  | { type: 'deleteContentForward' }
  | { type: 'formatBold' }
  | { type: 'formatItalic' }
  | { type: 'formatUnderline' }
  | { type: 'formatCode' }
  | { type: 'formatToggleMark',     mark: string }
  | { type: 'formatBlockType',      tag: string }
  | { type: 'selectAll' }

// ---------------------------------------------------------------------------
// Decorators
// ---------------------------------------------------------------------------

function markTypingHistory(tx: Transaction | null): Transaction | null {
  if (tx === null) return null
  return txSetMeta(tx, 'historyGroup', 'typing')
}

function markScrollIntoView(tx: Transaction | null): Transaction | null {
  if (tx === null) return null
  return txSetMeta(tx, 'scrollIntoView', true)
}

// ---------------------------------------------------------------------------
// dispatchIntent
// ---------------------------------------------------------------------------

function dispatchIntentRaw(state: EditorState, intent: InputIntent): Transaction | null {
  switch (intent.type) {
    case 'insertText':            return markTypingHistory(cmdInsertText(state, intent.text))
    case 'insertParagraph':       return cmdInsertParagraph(state)
    case 'insertLineBreak':       return cmdInsertLineBreak(state)
    case 'deleteContentBackward': return cmdDeleteBackward(state)
    case 'deleteContentForward':  return cmdDeleteForward(state)
    case 'formatBold':            return cmdFormatBold(state)
    case 'formatItalic':          return cmdFormatItalic(state)
    case 'formatUnderline':       return cmdFormatUnderline(state)
    case 'formatCode':            return cmdToggleMark(state, 'code')
    case 'formatToggleMark':      return cmdToggleMark(state, intent.mark)
    case 'formatBlockType':       return cmdSetBlockType(state, intent.tag)
    case 'selectAll':             return cmdSelectAll(state)
  }
}

export function dispatchIntent(state: EditorState, intent: InputIntent): Transaction | null {
  return markScrollIntoView(dispatchIntentRaw(state, intent))
}
