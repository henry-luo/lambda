# Radiant React Support Proposal

Date: June 2026

## Goal

Enhance LambdaJS and Radiant so a real React application can run inside the
Radiant HTML/CSS engine using the normal React DOM entry points:

```js
import { createRoot, hydrateRoot } from "react-dom/client";

const root = createRoot(document.getElementById("root"));
root.render(<App />);
```

Full support means more than rendering a static component once. React must be
able to load through the LambdaJS module system, create and mutate Radiant DOM
nodes through `react-dom`, receive browser-like events, run scheduled updates,
control form elements, trigger Radiant style/layout/paint invalidation, and pass
a focused compatibility suite.

## Non-Goals

- Do not reimplement React in Lambda or translate React components into Lambda
  `view` templates.
- Do not make Radiant depend on V8, WebKit, Chromium, or Node at runtime.
- Do not hard-code behavior for one bundled React file. Compatibility must come
  from browser/DOM/runtime semantics.
- Do not replace Lambda Reactive UI. React support is a guest JavaScript
  framework path; Lambda templates remain a native UI model.

## Current Foundation

LambdaJS already has a strong base for React:

- JS source is parsed with Tree-sitter, lowered to MIR, and executed as native
  code through the shared Lambda runtime.
- JS values are represented as Lambda `Item` values, with objects as Lambda
  maps, functions as `JsFunction`, and DOM nodes as wrapped `DomElement*`
  objects.
- The DOM bridge exposes `document`, DOM node wrappers, element creation,
  selectors, attributes, inline style, computed style, classList, dataset,
  text nodes, comments, and document fragments.
- Radiant already executes inline and external HTML scripts with a
  `DomDocument` context before cascade/layout.
- LambdaJS has Promises, microtasks, timers, `requestAnimationFrame`,
  `queueMicrotask`, `fetch`, XHR, CommonJS `require`, ES module import,
  dynamic import, browser-ish globals, and an EventTarget implementation with
  capture/target/bubble phases.
- Radiant has layout, render, form controls, editing, selection, frame clock,
  and retained display-list infrastructure.

The missing work is compatibility detail and integration glue. React is a host
config stress test: if the DOM, events, scheduler, modules, and forms behave
like a browser, React mostly runs as ordinary JavaScript.

## React Requirements

React DOM expects these browser-facing capabilities:

| Area | Required Behavior |
|------|-------------------|
| Entry points | `createRoot`, `root.render`, `root.unmount`, `hydrateRoot` |
| JSX | JSX or automatic runtime output via `react/jsx-runtime` |
| Modules | npm-style `react`, `react-dom`, `scheduler`, package exports |
| DOM creation | `createElement`, `createTextNode`, `createComment`, fragments |
| DOM mutation | append, insert, remove, replace, text updates, attributes |
| Host props | `className`, `htmlFor`, style object, boolean attrs, custom attrs |
| Events | delegated root listeners, capture, bubbling, synthetic event inputs |
| Scheduler | microtasks, MessageChannel/task queue, timers, rAF |
| Forms | controlled input/select/textarea, checked/value/defaultValue semantics |
| Layout feedback | mutations invalidate style/layout/paint and schedule a frame |
| Hydration | existing DOM matching, comments/text nodes, mismatch recovery |
| Refs | callback/object refs receive stable DOM node wrappers |

Official React references used for this proposal:

- `react-dom/client`: https://react.dev/reference/react-dom/client
- `createRoot`: https://react.dev/reference/react-dom/client/createRoot
- `hydrateRoot`: https://react.dev/reference/react-dom/client/hydrateRoot
- DOM components: https://react.dev/reference/react-dom/components
- Common props/events: https://react.dev/reference/react-dom/components/common
- JSX: https://react.dev/learn/writing-markup-with-jsx

## Architecture

```
React app source
  -> bundle or LambdaJS module loader
  -> LambdaJS parser / MIR transpiler
  -> React reconciler running as JS
  -> react-dom host operations
  -> LambdaJS DOM bridge
  -> Radiant DomDocument / DomElement mutation
  -> style invalidation / layout / render frame
```

Radiant should not know about React internals. The only React-specific code
should be in tests, optional developer tooling, and maybe compatibility shims
for package names. The durable implementation should improve the normal Web
API surface used by any modern frontend framework.

## Phase 0: Compatibility Harness

Build a repeatable React smoke harness before changing runtime behavior.

### Work

1. Add `test/react/` fixtures:
   - `react_umd_static.html`
   - `react_umd_state.html`
   - `react_umd_events.html`
   - `react_umd_forms.html`
   - `react_esm_basic/`
   - `react_hydrate_basic.html`
2. Add small prebundled React artifacts checked into `test/react/vendor/`.
   Use development builds first for diagnostics, production builds later for
   performance.
3. Add a runner command:
   - `make test-react`
   - or a GTest suite `test/test_react_gtest.cpp`
4. Add snapshot extraction helpers:
   - DOM text snapshot
   - serialized HTML snapshot
   - event log snapshot
   - optional rendered pixel snapshot for visual cases

### Acceptance

- A no-op page loads React and reports `React.version`.
- `createRoot(...).render(React.createElement("div", null, "hello"))`
  produces the expected DOM.
- Failures preserve `./temp/react/*` artifacts, never `/tmp`.

## Phase 1: UMD React Client Rendering

Target a bundled browser React build loaded by classic `<script>` tags. This
avoids npm package resolution and JSX initially.

### Work

1. Ensure global aliases are browser-like:
   - `window === globalThis`
   - `self === window`
   - `document.defaultView === window`
   - `navigator.userAgent` exists
2. Tighten `document` and node identity:
   - `Node`, `Element`, `HTMLElement`, `HTMLDivElement`, `Text`,
     `Comment`, `DocumentFragment`
   - `instanceof` behavior for wrapped DOM nodes
   - `nodeType`, `nodeName`, `tagName`, `ownerDocument`, `parentNode`,
     `parentElement`, `isConnected`
3. Verify React can call:
   - `document.createElement`
   - `document.createTextNode`
   - `document.createComment`
   - `appendChild`, `insertBefore`, `removeChild`, `replaceChild`
   - `setAttribute`, `removeAttribute`
   - `textContent`, `nodeValue`, `innerHTML`
4. Ensure DOM wrappers are stable enough for refs and equality checks.
   Repeated wrapping of the same `DomNode*` should either return stable object
   identity or provide a wrapper cache where React-visible identity requires it.

### Likely Files

| File | Work |
|------|------|
| `lambda/js/js_dom.cpp` | DOM properties, methods, wrapper identity cache |
| `lambda/js/js_dom.h` | New DOM API declarations |
| `lambda/js/js_globals.cpp` | DOM constructors and browser globals |
| `lambda/js/js_class.h` | JS class IDs for DOM wrapper classes |
| `lambda/input/css/dom_element.cpp` | Missing node mutation primitives |
| `radiant/script_runner.cpp` | Script load/execution ordering if needed |

### Acceptance

- React UMD static render creates nested DOM.
- Function component render works.
- Class component render works.
- `ref` callback receives a DOM wrapper and later receives `null` on unmount.

## Phase 2: Mutation Invalidation and Frame Scheduling

React mutates the existing DOM tree. Radiant must observe those mutations and
schedule the same work a browser would do: recascade when styles/attributes
change, relayout when tree/layout-affecting values change, and repaint when
visual state changes.

### Work

1. Add a central DOM mutation notification API:

```c
void radiant_dom_mark_mutated(DomDocument* doc, DomNode* node,
                              RdtDomMutationKind kind);
```

2. Route all JS DOM mutators through it:
   - child insertion/removal/replacement
   - text/comment data changes
   - `setAttribute` / `removeAttribute`
   - `className`, `id`, `style`, `dataset`
   - form value/checked/selected changes
3. Track dirty categories:
   - selector dependency dirty
   - inline style dirty
   - layout tree dirty
   - paint dirty
   - accessibility/event target dirty
4. Connect JS scheduling to Radiant frame clock:
   - after a task or event dispatch, flush microtasks
   - if DOM is dirty, schedule style/layout/render
   - rAF callbacks run before paint with updated timestamp

### Likely Files

| File | Work |
|------|------|
| `radiant/frame_clock.cpp` | rAF/frame integration |
| `radiant/browsing_session.cpp` | document-level scheduling |
| `radiant/cmd_layout.cpp` | headless/test drain behavior |
| `radiant/resolve_css_style.cpp` | recascade entry point |
| `lambda/js/js_event_loop.cpp` | microtask/task/rAF checkpoints |
| `lambda/js/js_dom.cpp` | mutation notifications |

### Acceptance

- `setState` after a click changes rendered text without reloading the page.
- DOM mutations from timers and Promise callbacks are visible after event-loop
  drain.
- `requestAnimationFrame` callbacks see the updated DOM before paint.
- No stale layout when React toggles class/style.

## Phase 3: React Event Compatibility

React listens at the root and builds synthetic events from native DOM events.
Radiant must generate enough browser events with correct propagation and
ordering.

### Work

1. Ensure EventTarget coverage:
   - add/remove/dispatch listener options: capture, once, passive, signal
   - `currentTarget`, `target`, `eventPhase`, `bubbles`, `cancelable`
   - `preventDefault`, `stopPropagation`, `stopImmediatePropagation`
   - `composedPath`
2. Route Radiant user input into DOM events:
   - click, dblclick
   - mousedown, mouseup, mousemove, mouseenter, mouseleave
   - pointerdown, pointerup, pointermove, pointerenter, pointerleave
   - keydown, keyup
   - beforeinput, input, change
   - focus, blur, focusin, focusout
   - compositionstart, compositionupdate, compositionend
   - wheel
3. Preserve browser event ordering:
   - checkbox click toggles checkedness at the correct point
   - input event fires when value changes
   - change event timing matches controls
   - focus/blur do not bubble, focusin/focusout do
4. Add default actions:
   - button click
   - form submit/reset
   - label activation
   - checkbox/radio toggling
   - select option selection

### Likely Files

| File | Work |
|------|------|
| `lambda/js/js_dom_events.cpp` | Event construction, dispatch, options |
| `radiant/event.cpp` | Native event mapping |
| `radiant/event_sim.cpp` | Test event synthesis |
| `radiant/form_control.hpp` | Default actions and control state |
| `radiant/text_control.cpp` | Text input/edit events |
| `radiant/editing_dispatch.cpp` | beforeinput/input integration |

### Acceptance

- React `onClick`, `onClickCapture`, `onMouseDown`, `onKeyDown` fire in order.
- `preventDefault` affects cancelable default actions.
- Controlled checkbox and text input update React state and DOM state.
- Event handlers receive stable `target` and correct `currentTarget`.

## Phase 4: Scheduler and MessageChannel

React's scheduler uses the browser task model. Timers and microtasks exist, but
`MessageChannel` must become a real task source rather than a no-op stub.

### Work

1. Implement `MessageChannel` and `MessagePort`:
   - paired ports
   - `postMessage`
   - queued task delivery
   - `onmessage`
   - `addEventListener("message", ...)`
   - `start`, `close`
2. Define event-loop checkpoints:
   - classic script end
   - timer callback end
   - DOM event dispatch end
   - MessagePort task end
   - rAF frame end
3. Add browser task ordering tests:
   - sync code
   - Promise microtasks
   - MessageChannel tasks
   - timers
   - rAF

### Likely Files

| File | Work |
|------|------|
| `lambda/js/js_event_loop.cpp` | Task queue and checkpoints |
| `lambda/js/js_globals.cpp` | Real MessageChannel/MessagePort constructors |
| `lambda/js/js_dom_events.cpp` | `message` Event construction |
| `lib/uv_loop.c` | uv integration if a native async wakeup is needed |

### Acceptance

- React scheduler progresses without falling back to pathological timer loops.
- Promise updates and MessageChannel updates flush in deterministic order.
- No pending task leaks when a document is destroyed.

## Phase 5: JSX and Package Loading

Once UMD works, support normal React app authoring.

### Work

1. JSX/TSX lowering:
   - parse JSX in JavaScript files, not only as a markup input format
   - support fragments
   - support automatic runtime imports from `react/jsx-runtime`
   - support development runtime `jsxDEV` for diagnostics
2. Package resolution:
   - `node_modules`
   - `package.json` `main`, `module`, `browser`, `exports`
   - conditional exports: `browser`, `import`, `require`, `development`,
     `production`, `default`
   - extension resolution: `.js`, `.mjs`, `.cjs`, `.jsx`, `.ts`, `.tsx`
3. React package aliases:
   - `react`
   - `react-dom`
   - `react-dom/client`
   - `react-dom/server` if hydration tests need generated markup
   - `scheduler`
   - `react/jsx-runtime`
   - `react/jsx-dev-runtime`
4. Bundled mode:
   - accept Vite/esbuild/Babel output as plain JS
   - ensure source maps are optional and ignored safely

### Likely Files

| File | Work |
|------|------|
| `lambda/js/build_js_ast.cpp` | JSX AST nodes inside JS mode |
| `lambda/js/js_ast.hpp` | JSX node definitions |
| `lambda/js/transpile_js_mir.cpp` and split lowering files | JSX lowering |
| `lambda/js/js_mir_entrypoints_require.cpp` | npm/package resolution |
| `lambda/js/js_mir_module_batch_lowering.cpp` | static import graph |
| `doc/dev/Node_Runtime.md` | package resolution docs |

### Acceptance

- This app runs without a prebundle:

```jsx
import React, { useState } from "react";
import { createRoot } from "react-dom/client";

function App() {
  const [count, setCount] = useState(0);
  return <button onClick={() => setCount(count + 1)}>{count}</button>;
}

createRoot(document.getElementById("root")).render(<App />);
```

- The same app runs from a bundled production artifact.

## Phase 6: Forms, Controlled Components, and Selection

React form support is one of the highest-risk areas because React synchronizes
DOM state and component state tightly.

### Work

1. Implement browser-like IDL properties:
   - `input.value`, `defaultValue`, `checked`, `defaultChecked`, `type`,
     `name`, `disabled`, `readOnly`
   - `textarea.value`, `defaultValue`
   - `select.value`, `selectedIndex`, `multiple`
   - `option.value`, `selected`, `disabled`
2. Selection APIs for text controls:
   - `selectionStart`, `selectionEnd`, `selectionDirection`
   - `setSelectionRange`
   - active element tracking
3. Event sequences:
   - text typing: keydown -> beforeinput -> DOM value update -> input
   - checkbox: click/default toggle -> input/change
   - select: selection update -> input/change
4. React controlled warnings are not required, but controlled behavior is.

### Likely Files

| File | Work |
|------|------|
| `lambda/js/js_dom.cpp` | IDL property get/set |
| `radiant/form_control.hpp` | Form state |
| `radiant/text_control.cpp` | Text editing state |
| `radiant/dom_range.cpp` | Selection integration |
| `lambda/js/js_dom_selection.cpp` | JS selection APIs |

### Acceptance

- Controlled text input mirrors React state.
- Controlled checkbox/radio mirrors React state.
- Controlled select/textarea works.
- Cursor position is preserved across React re-render for common input cases.

## Phase 7: Hydration

Hydration is required for SSR/SSG compatibility. It is more fragile than client
rendering because React expects existing nodes to match its virtual tree.

### Work

1. Preserve comment and text nodes accurately during HTML parse/build.
2. Expose enough DOM traversal:
   - `firstChild`, `nextSibling`, `previousSibling`, `childNodes`
   - stable text/comment node wrappers
3. Implement `hydrateRoot` test cases:
   - exact match
   - text update after hydration
   - event handler attachment
   - mismatch logging/recovery
4. Ensure resource/metadata nodes in `head` are not reordered unexpectedly.

### Acceptance

- Server-like HTML hydrates without clearing the root.
- Clicking a hydrated button updates state.
- Expected mismatch cases log recoverable errors but do not crash.

## Phase 8: Advanced DOM and Platform APIs

These are not needed for the first React app, but are needed for "full" support
across real libraries.

| API | Reason |
|-----|--------|
| `MutationObserver` | Libraries observe DOM side effects |
| `ResizeObserver` | Layout-aware components |
| `IntersectionObserver` | Lazy loading and virtualization |
| History API | Routers |
| `localStorage` / `sessionStorage` | App state and libraries |
| `DOMParser` / `XMLSerializer` | Markup manipulation |
| `CSSStyleDeclaration` completeness | Style feature detection |
| `getBoundingClientRect` | Positioning, popovers, virtualization |
| Portal support | Modals/tooltips rendered outside root |

These APIs should be implemented as normal Web APIs, not React-only shims.

## Testing Strategy

### Test Matrix

| Level | Tests |
|-------|-------|
| Runtime | js262/web-platform-style tests for JS and Web APIs |
| DOM bridge | DOM mutation and property tests in `test/js` |
| Events | dispatch order, capture/bubble, default actions |
| React smoke | UMD static, state, event, form, unmount |
| React package | ESM/CJS imports from `react` and `react-dom/client` |
| Hydration | exact SSR markup and mismatch recovery |
| Layout | rendered snapshots after React mutations |
| Memory | repeated React pages in layout batch mode |

### Gates

1. `make build-test`
2. `make test-lambda-baseline`
3. `make test-radiant-baseline`
4. `make test-react`
5. A batch memory run with at least 100 React-bearing documents

For JS/runtime-sensitive changes, also run the existing js262 baseline gate
used by LambdaJS work.

## Risks

### 1. Wrapper Identity

React refs and equality checks may fail if every DOM access creates a fresh JS
wrapper for the same `DomNode*`.

Mitigation: add a per-document wrapper cache keyed by `DomNode*`, with cleanup
when the document dies. Keep wrappers rooted while the document/runtime is live.

### 2. Static Runtime State

React keeps closures, scheduled callbacks, roots, and event listeners alive
after initial script execution. Batch-mode cleanup must not destroy JS pools
while a live document still owns React state.

Mitigation: document-owned JS runtime state must be retained until
`free_document()`. Batch cleanup should destroy it only when the document is
destroyed, not immediately after script execution for interactive pages.

### 3. Event Ordering

Controlled components depend on precise browser event ordering. A nearly-right
event sequence can produce stale input values or double updates.

Mitigation: build event sequence tests before broad form work. Use React's own
observable behavior as the acceptance target.

### 4. Scheduler Semantics

A no-op or timer-only scheduler may work for simple demos but fail for
concurrent updates, transitions, and batching.

Mitigation: implement MessageChannel as a real task source and define
microtask/task/rAF checkpoints explicitly.

### 5. Hydration Fragility

Hydration requires byte-level DOM shape fidelity around comments, text nodes,
and whitespace.

Mitigation: postpone hydration until client rendering is solid. Add hydration
fixtures with explicit serialized DOM expectations.

### 6. Performance

React can generate many small DOM mutations. Full recascade/re-layout after
every mutation would be slow.

Mitigation: batch dirty flags through event-loop checkpoints. Multiple DOM
mutations during one React commit should produce one style/layout/render pass.

## Implementation Order

Recommended order:

1. UMD static render harness.
2. DOM identity and missing node/element properties.
3. DOM mutation invalidation and frame scheduling.
4. Click/state update.
5. MessageChannel/task queue.
6. Form controls and event ordering.
7. JSX lowering and npm package resolution.
8. Hydration.
9. Advanced observers/history/storage/portal-adjacent APIs.

This order proves the host DOM first. Package and JSX work should not hide host
bugs behind a bundler or transform.

## Success Definition

React support is complete enough for a first release when:

1. A bundled React app can render, update state, handle events, use refs, and
   unmount.
2. A source React app using JSX and `import { createRoot } from "react-dom/client"`
   runs through LambdaJS without an external browser runtime.
3. Controlled inputs, checkboxes, selects, and textareas work.
4. Radiant layout/render updates after React commits.
5. Hydration works for simple SSR markup.
6. Repeated React documents do not leak JS runtime, DOM wrappers, timers, or
   event listeners in batch mode.
7. Existing Lambda and Radiant baseline tests still pass.

## Open Questions

1. Should `lambda view` retain JS runtime state for every script-bearing page,
   or only pages that register long-lived listeners/timers/React roots?
2. Should React vendor bundles be checked into `test/react/vendor/`, or should
   the test runner build them from local npm packages when available?
3. Should JSX lowering target automatic runtime by default, or preserve a
   classic `React.createElement` option?
4. How much package resolution should LambdaJS implement before using an
   external bundler as the recommended production path?
5. Should `MutationObserver` be implemented before hydration, or only after
   basic React app support?

