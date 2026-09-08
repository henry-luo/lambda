import { describe, expect, it } from 'vitest'
import { historyShortcutForKey } from '../../src/view/history-shortcut.js'

function key(key: string, shiftKey = false): Pick<KeyboardEvent, 'key' | 'metaKey' | 'ctrlKey' | 'shiftKey'> {
  return { key, metaKey: true, ctrlKey: false, shiftKey }
}

describe('history shortcut key normalization', () => {
  it('recognizes shifted Cmd+Z when the event key is uppercase', () => {
    expect(historyShortcutForKey(key('z'))).toBe('undo')
    expect(historyShortcutForKey(key('Z', true))).toBe('redo')
  })
})
