// FullEditorDom — the vanilla-DOM (Stage 4B) port of full-editor.tsx.
//
// Same WYSIWYG shell — toolbar, contenteditable surface, DOM-selection →
// source tracking, paste/copy, block drag-reorder, image resize, gap caret,
// link popover, table column resize — built with plain DOM and the keyed
// reconciler instead of React. Runs in the browser and (Phase 2) under Radiant.
// The model/commands/dom-bridge it composes are all framework-free already.

import {
  editorReducer,
  initialEditorState,
  type EditorAction,
  type EditorViewState
} from '../src/view/editor-state.js'
import { renderDoc } from '../src/view/render-vnode.js'
import { reconcileDoc } from '../src/view/reconcile.js'
import { dispatchIntent } from '../src/input/intent.js'
import { intentFromInputEvent } from '../src/view/intent-from-input-event.js'
import {
  findElementByPath,
  getSourceSelectionFromDom,
  nearestPathOwner,
  pathOf,
  setDomSelectionFromSource,
  domBoundaryToSourcePos,
  parsePath
} from '../src/view/dom-bridge.js'
import {
  cmdFormatBold, cmdFormatCode, cmdFormatItalic, cmdFormatUnderline,
  cmdSetBlockType, cmdToggleMark, cmdSetMark, cmdRemoveMark,
  cmdInsertText, cmdInsertParagraph, cmdDeleteBackward, cmdDeleteForward
} from '../src/commands/text-commands.js'
import { cmdMoveCaret } from '../src/commands/caret.js'
import { gapSelection, multiNodeSelection, pos, textSelection } from '../src/model/source-pos.js'
import {
  cmdResizeImage, cmdInsertImage, cmdIndentListItem, cmdOutdentListItem,
  cmdInsertInlineAtom, cmdMergeCells, cmdSplitCell, cmdMoveNode, cmdSetColumnWidth
} from '../src/commands/structural-commands.js'
import { cmdPasteSlice } from '../src/commands/paste.js'
import { parseHtmlToDoc, serializeDocToHtml } from '../src/view/html-parser.js'
import { isNode, isText, nodeAt } from '../src/model/doc.js'
import type { EditorState } from '../src/commands/types.js'
import type { Doc, MarkDict, Selection, SourcePath, Transaction } from '../src/model/types.js'
import type { Schema } from '../src/model/schema.js'

export interface FullEditorDomOptions {
  doc: Doc
  schema: Schema
  initialSelection: Selection | null
}

type Command = (state: EditorState) => Transaction | null

// ---------------------------------------------------------------------------
// Small DOM helper for the chrome (toolbar, overlays). The editing SURFACE is
// rendered via the keyed reconciler; the chrome is built/updated imperatively.
// ---------------------------------------------------------------------------

type Props = Record<string, unknown>

function h(tag: string, props: Props = {}, children: (Node | string)[] = []): HTMLElement {
  const e = document.createElement(tag)
  for (const k in props) {
    const v = props[k]
    if (v === null || v === undefined || v === false) continue
    if (k === 'class') e.setAttribute('class', String(v))        // content attr; keeps classList in sync under Radiant (vs the .className property)
    else if (k === 'title') e.setAttribute('title', String(v))   // content attr (reflects under Radiant; [title=…] selectors)
    else if (k === 'disabled') (e as HTMLButtonElement).disabled = Boolean(v)
    else if (k === 'html') e.innerHTML = String(v)
    else if (k === 'style' && typeof v === 'object') Object.assign(e.style, v as object)
    else if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2).toLowerCase(), v as EventListener)
    else e.setAttribute(k, String(v))
  }
  for (const c of children) e.appendChild(typeof c === 'string' ? document.createTextNode(c) : c)
  return e
}

// ---------------------------------------------------------------------------
// Framework-free helpers (copied verbatim from full-editor.tsx).
// ---------------------------------------------------------------------------

function selKey(sel: Selection | null): string {
  return sel === null ? 'null' : JSON.stringify(sel)
}

function extractClipboardFragment(html: string): string {
  const frag = html.match(/<!--StartFragment-->([\s\S]*?)<!--EndFragment-->/)
  if (frag) return frag[1]!
  const body = html.match(/<body[^>]*>([\s\S]*)<\/body>/i)
  return (body ? body[1]! : html).trim()
}

function plainTextToHtml(text: string): string {
  if (text === '') return ''
  const esc = (s: string) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  return text.split(/\n\n+/).map(p => `<p>${esc(p).replace(/\n/g, '<br>')}</p>`).join('')
}

function activeMarks(doc: Doc, sel: Selection | null): MarkDict {
  if (sel === null || sel.kind !== 'text') return {}
  const leaf = nodeAt(doc, sel.anchor.path)
  if (leaf !== null && isText(leaf)) return leaf.marks
  return {}
}

function activeBlock(doc: Doc, sel: Selection | null): string {
  if (sel === null || sel.kind !== 'text') return ''
  const path = sel.anchor.path
  if (path.length < 1) return ''
  const block = nodeAt(doc, path.slice(0, path.length - 1))
  return block !== null && block.kind === 'node' ? block.tag : ''
}

const TEXT_COLORS = [
  '#0f172a', '#475569', '#94a3b8', '#ffffff', '#ef4444', '#f97316', '#eab308', '#84cc16',
  '#22c55e', '#14b8a6', '#06b6d4', '#3b82f6', '#2563eb', '#8b5cf6', '#d946ef', '#ec4899'
]
const EMOJI = [
  '😀', '😄', '😁', '😂', '🤣', '😊', '😍', '😎', '🤔', '😴',
  '😉', '😅', '😇', '🥳', '🤩', '😜', '🤗', '😢', '😡', '🤯',
  '👍', '👎', '👏', '🙏', '💪', '👋', '✌️', '🤝', '🙌', '👀',
  '❤️', '🔥', '⭐', '✨', '🎉', '🎊', '💯', '✅', '❌', '⚠️',
  '🚀', '💡', '📌', '📝', '📎', '🔗', '📅', '⏰', '🔒', '🔑'
]

// ---------------------------------------------------------------------------
// FullEditorDom
// ---------------------------------------------------------------------------

export class FullEditorDom {
  readonly shell: HTMLElement
  readonly surface: HTMLElement
  private readonly toolbar: HTMLElement
  private state: EditorViewState

  // toolbar control refs (updated on selection change)
  private btn: Record<string, HTMLButtonElement> = {}
  private blockSelect!: HTMLSelectElement
  private colorSwatch!: HTMLElement

  // transient drag state
  private selectionFromDom = false
  private hoverBlockIdx: number | null = null
  private dragSrcIdx: number | null = null

  private readonly onSelChangeBound: () => void

  constructor(host: HTMLElement, opts: FullEditorDomOptions) {
    this.state = initialEditorState({ doc: opts.doc, schema: opts.schema, selection: opts.initialSelection })

    this.toolbar = this.buildToolbar()
    // `data-script-edit` opts this contenteditable host into Stage 4B Phase 3
    // script-managed routing: Radiant delivers `beforeinput` here and performs
    // no native editing — this controller owns the model and every edit.
    this.surface = h('div', { class: 'rdt-surface', contenteditable: 'true', spellcheck: 'false', 'data-script-edit': 'true' })

    this.shell = h('div', { class: 'rdt-shell' }, [this.toolbar, this.surface])
    host.appendChild(this.shell)

    // surface event wiring (native events, no synthetic)
    this.surface.addEventListener('beforeinput', this.onBeforeInput)
    this.surface.addEventListener('paste', this.onPaste as EventListener)
    this.surface.addEventListener('copy', this.onCopy as EventListener)
    this.surface.addEventListener('keydown', this.onKeyDown as EventListener)
    this.surface.addEventListener('mousedown', this.onMouseDown as EventListener)
    this.surface.addEventListener('click', this.onClick as EventListener)
    this.surface.addEventListener('mousemove', this.onMouseMove as EventListener)
    this.surface.addEventListener('mouseleave', this.onMouseLeave)
    this.surface.addEventListener('dragstart', this.onDragStart as EventListener)
    this.surface.addEventListener('dragover', this.onDragOver as EventListener)
    this.surface.addEventListener('drop', this.onDrop as EventListener)

    this.onSelChangeBound = this.onSelectionChange
    document.addEventListener('selectionchange', this.onSelChangeBound)

    this.render()
    this.surface.focus()
  }

  destroy(): void {
    document.removeEventListener('selectionchange', this.onSelChangeBound)
    this.shell.remove()
  }

  getState(): EditorViewState {
    return this.state
  }

  // -------------------------------------------------------------------------
  // Dispatch + render
  // -------------------------------------------------------------------------

  private dispatch = (action: EditorAction): void => {
    this.state = editorReducer(this.state, action)
    this.render()
  }

  private runCmd = (cmd: Command): void => {
    this.syncSelectionFromDom()
    const c = this.state
    const tx = cmd({ doc: c.doc, schema: c.schema, selection: c.selection, stored_marks: c.stored_marks })
    if (tx !== null) this.dispatch({ type: 'apply', tx })
  }

  // Pull the live DOM selection into the model just before running a toolbar
  // command. The browser keeps the model synced via `selectionchange`; under
  // Radiant that event isn't delivered for native caret moves in the editor's
  // nested contenteditable, so we read it on demand here (window.getSelection()
  // works). Only text selections; node/multi-node/gap selections are kept.
  private syncSelectionFromDom(): void {
    const cur = this.state.selection
    if (cur !== null && cur.kind !== 'text') return
    const src = getSourceSelectionFromDom()
    if (src === null) return
    const next: Selection = { kind: 'text', anchor: src.anchor, head: src.head }
    if (selKey(next) !== selKey(cur)) this.state = { ...this.state, selection: next }
  }

  private render(): void {
    reconcileDoc(this.surface, renderDoc(this.state.doc))
    this.projectSelection()
    this.syncCellHighlight()
    this.syncToolbar()
    this.syncOverlays()
  }

  // source selection → DOM (skip once when the change came from the DOM)
  private projectSelection(): void {
    if (this.selectionFromDom) { this.selectionFromDom = false; return }
    if (this.state.selection?.kind === 'gap') {
      this.surface.focus()
      window.getSelection()?.removeAllRanges()
      return
    }
    setDomSelectionFromSource(this.surface, this.state.selection)
  }

  private syncCellHighlight(): void {
    this.surface.querySelectorAll('.rdt-cell-selected').forEach(el => el.classList.remove('rdt-cell-selected'))
    if (this.state.selection?.kind === 'multi-node') {
      for (const p of this.state.selection.paths) {
        findElementByPath(this.surface, p)?.classList.add('rdt-cell-selected')
      }
    }
  }

  // -------------------------------------------------------------------------
  // Toolbar
  // -------------------------------------------------------------------------

  private buildToolbar(): HTMLElement {
    const mkBtn = (key: string, title: string, label: string | Node, onClick: () => void): HTMLButtonElement => {
      const b = h('button', {
        type: 'button', class: 'rdt-btn', title,
        onmousedown: (e: Event) => e.preventDefault(),
        onclick: onClick
      }, [typeof label === 'string' ? document.createTextNode(label) : label]) as HTMLButtonElement
      this.btn[key] = b
      return b
    }
    const rich = (tag: string, txt: string) => h(tag, {}, [txt])

    this.blockSelect = h('select', {
      class: 'rdt-block', title: 'Block type',
      onmousedown: (e: Event) => e.stopPropagation(),
      onchange: (e: Event) => this.runCmd(s => cmdSetBlockType(s, (e.target as HTMLSelectElement).value))
    }, [
      h('option', { value: 'p' }, ['Paragraph']),
      h('option', { value: 'h1' }, ['Heading 1']),
      h('option', { value: 'h2' }, ['Heading 2']),
      h('option', { value: 'h3' }, ['Heading 3']),
      h('option', { value: 'h4' }, ['Heading 4']),
      h('option', { value: 'h5' }, ['Heading 5']),
      h('option', { value: 'h6' }, ['Heading 6']),
      h('option', { value: 'blockquote' }, ['Quote'])
    ]) as HTMLSelectElement

    const sep = () => h('span', { class: 'rdt-sep' })

    return h('div', { class: 'rdt-toolbar', onmousedown: (e: Event) => e.preventDefault() }, [
      this.blockSelect,
      sep(),
      mkBtn('bold', 'Bold (Cmd+B)', rich('b', 'B'), () => this.runCmd(cmdFormatBold)),
      mkBtn('italic', 'Italic (Cmd+I)', rich('i', 'I'), () => this.runCmd(cmdFormatItalic)),
      mkBtn('underline', 'Underline (Cmd+U)', rich('u', 'U'), () => this.runCmd(cmdFormatUnderline)),
      mkBtn('strikethrough', 'Strikethrough', rich('s', 'S'), () => this.runCmd(s => cmdToggleMark(s, 'strikethrough'))),
      mkBtn('code', 'Code', rich('code', '<>'), () => this.runCmd(cmdFormatCode)),
      mkBtn('link', 'Link', '🔗', () => {
        const url = window.prompt('Link URL:', 'https://')
        if (url) this.runCmd(s => cmdToggleMark(s, 'link', url))
      }),
      mkBtn('mention', 'Insert mention', '@', () => {
        const name = window.prompt('Mention:', 'alice')
        if (name) this.runCmd(s => cmdInsertInlineAtom(s, 'mention', [{ name: 'label', value: name }]))
      }),
      this.buildEmojiPicker(),
      sep(),
      mkBtn('merge', 'Merge selected cells (shift-click cells)', '⊞', () => this.runCmd(cmdMergeCells)),
      mkBtn('split', 'Split a merged cell', '⊟', () => this.runCmd(cmdSplitCell)),
      sep(),
      this.buildColorPalette(),
      mkBtn('highlight', 'Highlight', '🖍️', () => this.runCmd(s => cmdToggleMark(s, 'background', '#fef08a'))),
      sep(),
      mkBtn('undo', 'Undo (Cmd+Z)', '↶', () => this.dispatch({ type: 'undo' })),
      mkBtn('redo', 'Redo (Cmd+Shift+Z)', '↷', () => this.dispatch({ type: 'redo' }))
    ])
  }

  private buildEmojiPicker(): HTMLElement {
    let palette: HTMLElement | null = null
    const wrap = h('span', { class: 'rdt-emoji-wrap' })
    const close = (): void => { palette?.remove(); palette = null; document.removeEventListener('mousedown', onDocDown) }
    const onDocDown = (e: Event): void => { if (!wrap.contains(e.target as Node)) close() }
    const open = (): void => {
      palette = h('div', { class: 'rdt-emoji-palette', onmousedown: (e: Event) => e.preventDefault() },
        EMOJI.map(em => h('button', {
          type: 'button', class: 'rdt-emoji', title: em,
          onclick: () => this.runCmd(s => cmdInsertText(s, em))
        }, [em])))
      wrap.appendChild(palette)
      document.addEventListener('mousedown', onDocDown)
    }
    const toggle = h('button', {
      type: 'button', class: 'rdt-btn', title: 'Insert emoji',
      onmousedown: (e: Event) => e.preventDefault(),
      onclick: () => (palette ? close() : open())
    }, ['😀'])
    wrap.appendChild(toggle)
    return wrap
  }

  private buildColorPalette(): HTMLElement {
    let pal: HTMLElement | null = null
    const wrap = h('span', { class: 'rdt-color-wrap' })
    this.colorSwatch = h('span', { class: 'rdt-a', style: { color: '#111827' } }, ['A'])
    const close = (): void => { pal?.remove(); pal = null; document.removeEventListener('mousedown', onDocDown) }
    const onDocDown = (e: Event): void => { if (!wrap.contains(e.target as Node)) close() }
    const open = (): void => {
      const swatches = TEXT_COLORS.map(c => h('button', {
        type: 'button', class: 'rdt-swatch', title: c, style: { background: c },
        onclick: () => { this.runCmd(s => cmdSetMark(s, 'color', c)); close() }
      }))
      const custom = h('label', { class: 'rdt-custom' }, [
        'Custom',
        h('input', { type: 'color', value: '#e11d48', oninput: (e: Event) => this.runCmd(s => cmdSetMark(s, 'color', (e.target as HTMLInputElement).value)) })
      ])
      const clear = h('button', { type: 'button', class: 'rdt-clear', onclick: () => { this.runCmd(s => cmdRemoveMark(s, 'color')); close() } }, ['Default color'])
      pal = h('div', { class: 'rdt-palette', onmousedown: (e: Event) => e.preventDefault() }, [...swatches, custom, clear])
      wrap.appendChild(pal)
      document.addEventListener('mousedown', onDocDown)
    }
    const toggle = h('button', {
      type: 'button', class: 'rdt-color', title: 'Text color',
      onmousedown: (e: Event) => e.preventDefault(),
      onclick: () => (pal ? close() : open())
    }, [this.colorSwatch])
    wrap.appendChild(toggle)
    return wrap
  }

  private syncToolbar(): void {
    const marks = activeMarks(this.state.doc, this.state.selection)
    const block = activeBlock(this.state.doc, this.state.selection)
    // Toggle the active class via setAttribute('class', …) rather than
    // classList.toggle: under Radiant, classList mutations on JS-created
    // elements don't reflect to the `class` content attribute (the same
    // property-vs-attribute split as .title/.className), so the highlight would
    // never appear. Rebuilding the class string keeps it consistent everywhere.
    const setActive = (key: string, on: boolean) => {
      const b = this.btn[key]
      if (!b) return
      const cls = (b.getAttribute('class') ?? '').split(/\s+/).filter(c => c && c !== 'is-active')
      if (on) cls.push('is-active')
      b.setAttribute('class', cls.join(' '))
    }
    setActive('bold', marks['bold'] === true)
    setActive('italic', marks['italic'] === true)
    setActive('underline', marks['underline'] === true)
    setActive('strikethrough', marks['strikethrough'] === true)
    setActive('code', marks['code'] === true)
    setActive('link', 'link' in marks)
    setActive('highlight', 'background' in marks)
    this.blockSelect.value = ['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'blockquote'].includes(block) ? block : 'p'
    this.colorSwatch.style.color = typeof marks['color'] === 'string' ? (marks['color'] as string) : '#111827'
    this.btn['undo'] && ((this.btn['undo'] as HTMLButtonElement).disabled = this.state.history.undo.length === 0)
    this.btn['redo'] && ((this.btn['redo'] as HTMLButtonElement).disabled = this.state.history.redo.length === 0)
  }

  // -------------------------------------------------------------------------
  // Surface input handlers
  // -------------------------------------------------------------------------

  private onBeforeInput = (ev: Event): void => {
    const intent = intentFromInputEvent(ev as InputEvent)
    if (intent === null) return
    ev.preventDefault()
    const c = this.state
    const tx = dispatchIntent({ doc: c.doc, schema: c.schema, selection: c.selection, stored_marks: c.stored_marks }, intent)
    if (tx !== null) this.dispatch({ type: 'apply', tx })
  }

  private onPaste = (ev: ClipboardEvent): void => {
    const cb = ev.clipboardData
    if (cb === null) return
    const imgItem = Array.from(cb.items ?? []).find(it => it.kind === 'file' && it.type.startsWith('image/'))
    if (imgItem !== undefined) {
      const file = imgItem.getAsFile()
      if (file !== null) {
        ev.preventDefault()
        const reader = new FileReader()
        reader.onload = () => {
          const c = this.state
          const tx = cmdInsertImage({ doc: c.doc, schema: c.schema, selection: c.selection, stored_marks: c.stored_marks }, String(reader.result), '')
          if (tx !== null) this.dispatch({ type: 'apply', tx })
        }
        reader.readAsDataURL(file)
        return
      }
    }
    const html = cb.getData('text/html')
    const fragment = html.trim() !== '' ? extractClipboardFragment(html) : plainTextToHtml(cb.getData('text/plain'))
    if (fragment === '') return
    ev.preventDefault()
    const c = this.state
    const parsed = parseHtmlToDoc(`<doc>${fragment}</doc>`, c.schema)
    const tx = cmdPasteSlice({ doc: c.doc, schema: c.schema, selection: c.selection, stored_marks: c.stored_marks }, parsed.doc.content)
    if (tx !== null) this.dispatch({ type: 'apply', tx })
  }

  private onCopy = (ev: ClipboardEvent): void => {
    const sel = this.state.selection
    if (sel === null || sel.kind !== 'node') return
    const n = nodeAt(this.state.doc, sel.path)
    if (n === null || !isNode(n) || ev.clipboardData === null) return
    ev.preventDefault()
    ev.clipboardData.setData('text/html', serializeDocToHtml(n, { schema: this.state.schema }))
    ev.clipboardData.setData('text/plain', '')
  }

  private onSelectionChange = (): void => {
    const dom = window.getSelection()
    if (dom === null || dom.rangeCount === 0) return
    if (!this.surface.contains(dom.anchorNode)) return
    const src = getSourceSelectionFromDom()
    if (src === null) return
    const next: Selection = { kind: 'text', anchor: src.anchor, head: src.head }
    if (selKey(next) === selKey(this.state.selection)) { this.syncToolbar(); return }
    this.selectionFromDom = true
    this.dispatch({ type: 'set_selection', sel: next })
  }

  private onKeyDown = (e: KeyboardEvent): void => {
    const c = this.state
    if (c.selection?.kind === 'gap') {
      if (e.key.length === 1 && !e.metaKey && !e.ctrlKey && !e.altKey) { e.preventDefault(); this.runCmd(s => cmdInsertText(s, e.key)); return }
      if (e.key === 'Enter') { e.preventDefault(); this.runCmd(cmdInsertParagraph); return }
      if (e.key === 'Backspace') { e.preventDefault(); this.runCmd(cmdDeleteBackward); return }
      if (e.key === 'Delete') { e.preventDefault(); this.runCmd(cmdDeleteForward); return }
    }
    if (e.key.startsWith('Arrow') && !e.metaKey && !e.ctrlKey && !e.altKey) {
      const dir = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 'forward' : 'backward'
      const tx = cmdMoveCaret({ doc: c.doc, schema: c.schema, selection: c.selection, stored_marks: c.stored_marks }, e.shiftKey ? 'extend' : 'move', dir, 'character')
      if (tx !== null && (c.selection?.kind === 'gap' || tx.sel_after?.kind === 'gap')) { e.preventDefault(); this.dispatch({ type: 'apply', tx }); return }
    }
    if (e.key === 'Tab') { e.preventDefault(); this.runCmd(e.shiftKey ? cmdOutdentListItem : cmdIndentListItem); return }
    const meta = e.metaKey || e.ctrlKey
    if (!meta) return
    if (e.key === 'z' && !e.shiftKey) { e.preventDefault(); this.dispatch({ type: 'undo' }) }
    else if ((e.key === 'z' && e.shiftKey) || e.key === 'y') { e.preventDefault(); this.dispatch({ type: 'redo' }) }
    else if (e.key === 'b') { e.preventDefault(); this.runCmd(cmdFormatBold) }
    else if (e.key === 'i') { e.preventDefault(); this.runCmd(cmdFormatItalic) }
    else if (e.key === 'u') { e.preventDefault(); this.runCmd(cmdFormatUnderline) }
  }

  private onMouseDown = (e: MouseEvent): void => {
    const t = e.target as HTMLElement
    const cellEl = t.closest<HTMLElement>('td, th')
    if (e.shiftKey && cellEl !== null) {
      const owner = nearestPathOwner(cellEl)
      if (owner !== null) {
        e.preventDefault()
        const clicked = pathOf(owner)
        const key = clicked.join(',')
        const cur = this.state.selection
        const paths = cur?.kind === 'multi-node' ? [...cur.paths] : []
        if (paths.length === 0) {
          const dom = window.getSelection()
          const anchorEl = dom?.anchorNode != null
            ? (dom.anchorNode.nodeType === 3 ? dom.anchorNode.parentElement : (dom.anchorNode as HTMLElement))
            : null
          const anchorCell = anchorEl?.closest<HTMLElement>('td, th') ?? null
          if (anchorCell !== null && anchorCell !== cellEl) {
            const ao = nearestPathOwner(anchorCell)
            if (ao !== null) paths.push(pathOf(ao))
          }
        }
        if (!paths.some(p => p.join(',') === key)) paths.push(clicked)
        this.dispatch({ type: 'set_selection', sel: multiNodeSelection(paths) })
        window.getSelection()?.removeAllRanges()
      }
      return
    }
    if (t.tagName === 'IMG') return   // leave image mousedown alone so native drag can start
    const atomEl = t.tagName === 'HR' ? t : t.closest<HTMLElement>('.rdt-drawing')
    if (atomEl !== null) {
      const owner = nearestPathOwner(atomEl)
      if (owner !== null) {
        e.preventDefault()
        this.dispatch({ type: 'set_selection', sel: gapSelection(pathOf(owner)) })
      }
    }
  }

  private onClick = (e: MouseEvent): void => {
    const t = e.target as HTMLElement
    if (t.tagName !== 'IMG') return
    const owner = nearestPathOwner(t)
    if (owner === null) return
    this.dispatch({ type: 'set_selection', sel: { kind: 'node', path: pathOf(owner) } })
    window.getSelection()?.removeAllRanges()
  }

  // -------------------------------------------------------------------------
  // Block drag-reorder + image-file drop
  // -------------------------------------------------------------------------

  private topBlocks(): HTMLElement[] {
    const ed = this.surface.querySelector('.rdt-editor')
    return ed ? (Array.from(ed.children) as HTMLElement[]) : []
  }

  private dropAt(clientY: number): { index: number; y: number } {
    const blocks = this.topBlocks()
    const sTop = this.shell.getBoundingClientRect().top
    for (let i = 0; i < blocks.length; i++) {
      const r = blocks[i]!.getBoundingClientRect()
      if (clientY < r.top + r.height / 2) return { index: i, y: r.top - sTop }
    }
    const last = blocks[blocks.length - 1]
    return { index: blocks.length, y: last ? last.getBoundingClientRect().bottom - sTop : 0 }
  }

  private caretAtPoint(x: number, y: number): Selection | null {
    const d = document as Document & {
      caretRangeFromPoint?: (x: number, y: number) => Range | null
      caretPositionFromPoint?: (x: number, y: number) => { offsetNode: Node; offset: number } | null
    }
    let node: Node | null = null
    let offset = 0
    if (typeof d.caretRangeFromPoint === 'function') {
      const r = d.caretRangeFromPoint(x, y)
      if (r !== null) { node = r.startContainer; offset = r.startOffset }
    } else if (typeof d.caretPositionFromPoint === 'function') {
      const p = d.caretPositionFromPoint(x, y)
      if (p !== null) { node = p.offsetNode; offset = p.offset }
    }
    if (node === null) return null
    const sp = domBoundaryToSourcePos({ node, offset })
    return sp === null ? null : textSelection(pos(sp.path, sp.offset), pos(sp.path, sp.offset))
  }

  private onMouseMove = (e: MouseEvent): void => {
    if (this.dragSrcIdx !== null) return
    const block = (e.target as HTMLElement).closest<HTMLElement>('.rdt-editor > *')
    if (block === null) { this.removeDragHandle(); return }
    this.hoverBlockIdx = this.topBlocks().indexOf(block)
    this.showDragHandle(block.getBoundingClientRect().top - this.shell.getBoundingClientRect().top)
  }

  private onMouseLeave = (): void => {
    if (this.dragSrcIdx === null) this.removeDragHandle()
  }

  private onDragStart = (e: DragEvent): void => {
    const t = e.target as HTMLElement
    if (t.tagName !== 'IMG') return
    const block = t.closest<HTMLElement>('.rdt-editor > *')
    const idx = block !== null ? this.topBlocks().indexOf(block) : -1
    if (idx >= 0 && e.dataTransfer !== null) {
      this.dragSrcIdx = idx
      e.dataTransfer.effectAllowed = 'move'
      e.dataTransfer.setData('text/plain', 'block')
    }
  }

  private onDragOver = (e: DragEvent): void => {
    const hasFile = e.dataTransfer !== null && Array.from(e.dataTransfer.types).includes('Files')
    if (this.dragSrcIdx === null && !hasFile) return
    e.preventDefault()
    this.showDropLine(this.dropAt(e.clientY).y)
  }

  private onDrop = (e: DragEvent): void => {
    const { index } = this.dropAt(e.clientY)
    const file = e.dataTransfer !== null ? Array.from(e.dataTransfer.files).find(f => f.type.startsWith('image/')) : undefined
    if (file !== undefined) {
      e.preventDefault()
      const dropSel = this.caretAtPoint(e.clientX, e.clientY) ?? this.state.selection
      const reader = new FileReader()
      reader.onload = () => {
        const c = this.state
        const tx = cmdInsertImage({ doc: c.doc, schema: c.schema, selection: dropSel, stored_marks: c.stored_marks }, String(reader.result), '')
        if (tx !== null) this.dispatch({ type: 'apply', tx })
      }
      reader.readAsDataURL(file)
    } else if (this.dragSrcIdx !== null) {
      e.preventDefault()
      const src = this.dragSrcIdx
      this.runCmd(s => cmdMoveNode(s, [src], index))
    }
    this.dragSrcIdx = null
    this.removeDropLine()
  }

  // -------------------------------------------------------------------------
  // Overlays (positioned chrome rebuilt after each render)
  // -------------------------------------------------------------------------

  private overlay(cls: string): HTMLElement {
    this.shell.querySelectorAll('.' + cls).forEach(el => el.remove())
    return h('div', { class: cls })
  }

  private showDragHandle(top: number): void {
    let handle = this.shell.querySelector('.rdt-drag-handle') as HTMLElement | null
    if (handle === null) {
      handle = h('div', {
        class: 'rdt-drag-handle', title: 'Drag to reorder this block', draggable: 'true',
        ondragstart: (ev: DragEvent) => {
          this.dragSrcIdx = this.hoverBlockIdx
          if (ev.dataTransfer !== null) { ev.dataTransfer.effectAllowed = 'move'; ev.dataTransfer.setData('text/plain', 'block') }
        },
        ondragend: () => { this.dragSrcIdx = null; this.removeDropLine(); this.removeDragHandle() }
      }, ['⠿'])
      this.shell.appendChild(handle)
    }
    handle.style.top = `${top}px`
  }
  private removeDragHandle(): void { this.shell.querySelector('.rdt-drag-handle')?.remove() }
  private showDropLine(top: number): void {
    let line = this.shell.querySelector('.rdt-drop-line') as HTMLElement | null
    if (line === null) { line = h('div', { class: 'rdt-drop-line' }); this.shell.appendChild(line) }
    line.style.top = `${top}px`
  }
  private removeDropLine(): void { this.shell.querySelector('.rdt-drop-line')?.remove() }

  private syncOverlays(): void {
    this.syncImageOverlay()
    this.syncGapCaret()
    this.syncLinkPopover()
    this.syncColResizers()
  }

  private selectedImagePath(): SourcePath | null {
    const sel = this.state.selection
    if (sel?.kind !== 'node') return null
    const n = nodeAt(this.state.doc, sel.path)
    return n !== null && isNode(n) && n.tag === 'img' ? sel.path : null
  }

  private syncImageOverlay(): void {
    const path = this.selectedImagePath()
    if (path === null) { this.shell.querySelectorAll('.rdt-img-overlay').forEach(el => el.remove()); return }
    const el = findElementByPath(this.surface, path)
    if (el === null) { this.shell.querySelectorAll('.rdt-img-overlay').forEach(o => o.remove()); return }
    const sRect = this.shell.getBoundingClientRect()
    const r = el.getBoundingClientRect()
    const box = { left: r.left - sRect.left, top: r.top - sRect.top, width: r.width, height: r.height }
    const overlay = this.overlay('rdt-img-overlay')
    Object.assign(overlay.style, { left: `${box.left}px`, top: `${box.top}px`, width: `${box.width}px`, height: `${box.height}px` })
    for (const corner of ['nw', 'ne', 'sw', 'se'] as const) {
      overlay.appendChild(h('span', {
        class: 'rdt-img-handle rdt-h-' + corner,
        onmousedown: (e: MouseEvent) => this.startImageResize(e, corner, box, path)
      }))
    }
    this.shell.appendChild(overlay)
  }

  private startImageResize(e: MouseEvent, corner: string, box: { width: number; height: number }, path: SourcePath): void {
    e.preventDefault(); e.stopPropagation()
    const ratio = box.width / box.height || 1
    const startX = e.clientX
    let last = { w: box.width, h: box.height }
    const onMove = (ev: MouseEvent) => {
      const dx = ev.clientX - startX
      const sign = corner.includes('e') ? 1 : -1
      const w = Math.max(24, box.width + sign * dx)
      last = { w, h: w / ratio }
      const overlay = this.shell.querySelector('.rdt-img-overlay') as HTMLElement | null
      if (overlay !== null) { overlay.style.width = `${last.w}px`; overlay.style.height = `${last.h}px` }
    }
    const onUp = () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
      this.runCmd(s => cmdResizeImage(s, path, last.w, last.h))
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }

  private syncGapCaret(): void {
    this.shell.querySelectorAll('.rdt-gap-caret').forEach(el => el.remove())
    const sel = this.state.selection
    if (sel?.kind !== 'gap') return
    const index = sel.path[sel.path.length - 1] ?? 0
    const container = sel.path.slice(0, -1)
    let el = findElementByPath(this.surface, sel.path)
    let atTop = true
    if (el === null && index > 0) { el = findElementByPath(this.surface, [...container, index - 1]); atTop = false }
    if (el === null) return
    const sRect = this.shell.getBoundingClientRect()
    const r = el.getBoundingClientRect()
    const caret = h('div', { class: 'rdt-gap-caret' })
    Object.assign(caret.style, { left: `${r.left - sRect.left}px`, top: `${(atTop ? r.top : r.bottom) - sRect.top - 2}px`, width: `${r.width}px` })
    this.shell.appendChild(caret)
  }

  private syncLinkPopover(): void {
    this.shell.querySelectorAll('.rdt-link-popover').forEach(el => el.remove())
    const sel = this.state.selection
    if (sel?.kind !== 'text') return
    const marks = activeMarks(this.state.doc, sel)
    if (typeof marks['link'] !== 'string') return
    const href = marks['link'] as string
    const leafPath = sel.anchor.path
    const el = findElementByPath(this.surface, leafPath)
    if (el === null) return
    const sRect = this.shell.getBoundingClientRect()
    const r = el.getBoundingClientRect()
    const pop = h('div', { class: 'rdt-link-popover', onmousedown: (e: Event) => e.preventDefault() }, [
      h('a', { class: 'rdt-link-url', href, target: '_blank', rel: 'noreferrer', title: href }, [href]),
      h('button', { type: 'button', class: 'rdt-btn', title: 'Edit link', onclick: () => { const u = window.prompt('Edit link URL:', href); if (u !== null) this.editLinkLeaf(leafPath, u.trim() === '' ? null : u) } }, ['✎']),
      h('button', { type: 'button', class: 'rdt-btn', title: 'Remove link', onclick: () => this.editLinkLeaf(leafPath, null) }, ['✕'])
    ])
    Object.assign(pop.style, { left: `${r.left - sRect.left}px`, top: `${r.bottom - sRect.top + 4}px` })
    this.shell.appendChild(pop)
  }

  private editLinkLeaf(leafPath: SourcePath, href: string | null): void {
    const c = this.state
    const leaf = nodeAt(c.doc, leafPath)
    if (leaf === null || !isText(leaf)) return
    const sel = textSelection(pos(leafPath, 0), pos(leafPath, leaf.text.length))
    const s = { doc: c.doc, schema: c.schema, selection: sel, stored_marks: c.stored_marks }
    const tx = href === null ? cmdRemoveMark(s, 'link') : cmdSetMark(s, 'link', href)
    if (tx !== null) this.dispatch({ type: 'apply', tx })
  }

  private syncColResizers(): void {
    this.shell.querySelectorAll('.rdt-col-resizer').forEach(el => el.remove())
    const sRect = this.shell.getBoundingClientRect()
    this.surface.querySelectorAll('table').forEach(tbl => {
      const firstRow = tbl.querySelector('tr')
      if (firstRow === null) return
      const tRect = (tbl as HTMLElement).getBoundingClientRect()
      Array.from(firstRow.children).forEach(cellEl => {
        const sp = cellEl.getAttribute('data-source-path')
        if (sp === null) return
        const cr = cellEl.getBoundingClientRect()
        const cellPath = parsePath(sp)
        const handle = h('div', {
          class: 'rdt-col-resizer',
          style: { left: `${cr.right - sRect.left - 3}px`, top: `${tRect.top - sRect.top}px`, height: `${tRect.height}px` },
          onmousedown: (e: MouseEvent) => this.startColResize(e, cellPath)
        })
        this.shell.appendChild(handle)
      })
    })
  }

  private startColResize(e: MouseEvent, cellPath: SourcePath): void {
    e.preventDefault()
    const cellEl = findElementByPath(this.surface, cellPath)
    const startW = cellEl !== null ? cellEl.getBoundingClientRect().width : 80
    const startX = e.clientX
    const sLeft = this.shell.getBoundingClientRect().left
    const guide = h('div', { class: 'rdt-col-guide' })
    this.shell.appendChild(guide)
    const onMove = (ev: MouseEvent) => { guide.style.left = `${ev.clientX - sLeft}px` }
    const onUp = (ev: MouseEvent) => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
      guide.remove()
      this.runCmd(s => cmdSetColumnWidth(s, cellPath, startW + (ev.clientX - startX)))
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }
}

// Convenience factory mirroring how the React FullEditor is constructed in demos.
export function mountFullEditor(host: HTMLElement, opts: FullEditorDomOptions): FullEditorDom {
  return new FullEditorDom(host, opts)
}
