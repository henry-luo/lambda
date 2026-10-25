# React ecosystem compatibility fixtures

`../react.js`, `../react.html` and `../react.txt` are a normal auto-discovered
JS GTest. The shared build also generates these independent fixtures:

| Bundle | Pinned library | Readable source | Coverage |
|---|---|---|---|
| `../react.js` | React/React DOM 19.3.1, scheduler 0.28.0 | `suite.jsx` | Core rendering, events, forms, scheduling, hydration and cleanup |
| `../redux_toolkit.js` | Redux Toolkit 2.13.0 | `redux_toolkit.js` | Immer, slices, selectors, entities, middleware, async thunks and cancellation |
| `../react_redux.js` | React Redux 9.3.0 | `react_redux.jsx` | Provider, hooks, connect, selector equality, batching and unsubscribe |
| `../zustand.js` | Zustand 5.0.16 | `zustand.jsx` | Vanilla/React stores, selector middleware, persistence and cleanup |
| `../react_router.js` | React Router 8.5.0 | `react_router.jsx` | Memory routes, links, search/state/history, loaders, redirects, actions and errors |

Bundles include unmodified upstream code and Lambda-owned assertions. Upstream
notices remain at the end of each bundle; `THIRD_PARTY_LICENSES.txt` collects
the complete license of each bundled package. `package-lock.json` pins all
transitive dependencies. No network or npm install is needed to run the tests.

Regenerate after changing the suite:

```sh
cd test/js/react
npm ci
npm run build
```

From the repository root:

```sh
JS_GTEST_MODE=ast ./test/test_js_gtest.exe --gtest_filter='*Run/react'
JS_GTEST_MODE=mir ./test/test_js_gtest.exe --gtest_filter='*Run/react'
JS_EXEC_BACKEND=auto ./lambda.exe js test/js/react.js --document test/js/react.html --no-log

# the four ecosystem suites, including their DOM fixtures, on AST and MIR
JS_GTEST_MODE=ast ./test/test_js_gtest.exe --gtest_filter='*Run/redux_toolkit:*Run/react_redux:*Run/zustand:*Run/react_router'
JS_GTEST_MODE=mir ./test/test_js_gtest.exe --gtest_filter='*Run/redux_toolkit:*Run/react_redux:*Run/zustand:*Run/react_router'
# AUTO is selected directly; the GTest's default mode deliberately mixes AST/MIR
JS_EXEC_BACKEND=auto ./lambda.exe js test/js/redux_toolkit.js --no-log
JS_EXEC_BACKEND=auto ./lambda.exe js test/js/react_router.js --document test/js/react_router.html --no-log
```

Tests use real DOM properties and React commit completion. Native setters
simulate an external text edit without updating React's own value tracker.
No framework compatibility shims are installed. The JSX/npm authoring step
is external; this fixture qualifies bundled browser JavaScript (D8.1.3v42).
The implementation record and remaining scope are in
`vibe/impl/Lambda_Impl_React.md` and
`vibe/impl/Lambda_Impl_React_Ecosystem.md`.

Router coverage uses memory routing; it does not qualify browser history,
server rendering or React Router's framework/build tooling. Data loaders and
actions run locally through native Request/Response values, without network
access. Zustand persistence supplies its documented custom storage adapter
backed by a Map, so it is independent of browser storage state. All asynchronous
tests await actions or destination commits instead of fixed sleeps.

`test/ui/react_compatibility.json` loads the same bundle through an HTML script
and replays physical clicks on a retained counter and controlled checkbox.
