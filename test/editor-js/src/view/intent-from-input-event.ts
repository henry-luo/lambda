// Translate a browser `InputEvent` (the beforeinput event) into our InputIntent
// discriminated union.
//
// This is the Editor-side equivalent of the Stage-1 design's "InputIntent
// enum" (Reactive_UI / Radiant_Rich_Text_Editing §7). The browser uses
// inputType strings derived from the InputEvent Level 2 spec.

import type { InputIntent } from '../input/intent.js'

export function intentFromInputEvent(ev: InputEvent): InputIntent | null {
  switch (ev.inputType) {
    case 'insertText':
      return ev.data === null ? null : { type: 'insertText', text: ev.data }
    case 'insertParagraph':
      return { type: 'insertParagraph' }
    case 'insertLineBreak':
      return { type: 'insertLineBreak' }
    case 'deleteContentBackward':
      return { type: 'deleteContentBackward' }
    case 'deleteContentForward':
      return { type: 'deleteContentForward' }
    case 'formatBold':
      return { type: 'formatBold' }
    case 'formatItalic':
      return { type: 'formatItalic' }
    case 'formatUnderline':
      return { type: 'formatUnderline' }
    default:
      return null
  }
}
