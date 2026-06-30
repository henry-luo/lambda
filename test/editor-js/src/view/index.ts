export { EditorView, type EditorViewProps } from './EditorView.js'
export { renderDoc } from './render.js'
export { parseHtmlToDoc, serializeDocToHtml } from './html-parser.js'
export {
  domBoundaryToSourcePos,
  findElementByPath,
  getSourceSelectionFromDom,
  pathOf,
  parsePath,
  setDomSelectionFromSource,
  sourcePosToDomBoundary,
  stringifyPath,
  SOURCE_PATH_ATTR
} from './dom-bridge.js'
export {
  editorReducer,
  initialEditorState,
  useEditorState,
  type EditorAction,
  type EditorViewState
} from './use-editor-state.js'
export { intentFromInputEvent } from './intent-from-input-event.js'

// Stage 4B — plain-DOM (framework-free) view: VNode renderer, keyed reconciler,
// and the vanilla controller that runs under Radiant.
export { renderDoc as renderDocVNode } from './render-vnode.js'
export { reconcile, reconcileDoc } from './reconcile.js'
export { el, txt, type VNode, type VEl, type VText, type VAttrs } from './vnode.js'
export { EditorViewDom, type EditorViewDomOptions } from './editor-view-dom.js'
