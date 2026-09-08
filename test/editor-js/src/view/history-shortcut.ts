// Normalize printable shortcut keys because Shift changes KeyboardEvent.key case.
export function historyShortcutForKey(event: Pick<KeyboardEvent, 'key' | 'metaKey' | 'ctrlKey' | 'shiftKey'>): 'undo' | 'redo' | null {
  if (!event.metaKey && !event.ctrlKey) return null
  const key = event.key.toLowerCase()
  if (key === 'z') return event.shiftKey ? 'redo' : 'undo'
  return key === 'y' ? 'redo' : null
}
