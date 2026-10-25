# React compatibility fixture

`../react.js`, `../react.html` and `../react.txt` are a normal auto-discovered
JS GTest. The bundle contains unmodified React/React DOM 19.3.1 and scheduler
0.28.0, plus Lambda-owned tests from `suite.jsx`. Upstream MIT license notices
are preserved at the end of the generated bundle. No network or npm install is
needed to run it.

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
```

Tests use real DOM properties and React commit completion. Native setters
simulate an external text edit without updating React's own value tracker.
No framework compatibility shims are installed. The JSX/npm authoring step
is external; this fixture qualifies bundled browser JavaScript (D8.1.3v42).
The implementation record and remaining scope are in
`vibe/impl/Lambda_Impl_React.md`.

`test/ui/react_compatibility.json` loads the same bundle through an HTML script
and replays physical clicks on a retained counter and controlled checkbox.
