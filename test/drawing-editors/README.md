# Radiant editable-drawing compatibility fixtures

This package pins unmodified Raphaël, maxGraph core, and JointJS core for the
SVG-only drawing probes described in
[`Radiant_Design_Editable_Drawing.md`](../../vibe/radiant/Radiant_Design_Editable_Drawing.md).

Run `npm install && npm run build` to refresh local browser bundles in
`build/`, then run `make test-drawing` from the repository root. The repository
targets never install packages. Each entrypoint uses only the upstream public
API and publishes library-owned state in `#drawing-state`; it does not patch a
library or emulate missing SVG APIs.

The focused gate builds the bundles and runs the direct SVG DOM contract plus
the Raphaël, maxGraph, and JointJS event fixtures. It asserts SVG-only output,
ordinary pointer/wheel/keyboard event delivery, library model state, and
destroy/recreate cleanup. The precise supported surface and deliberate
exclusions live in `capability-manifest.json`.

The existing Editor.js compatibility fixture owns the narrow
`document.execCommand("insertHTML")` contract. These drawing pages never call
that legacy API and report its global presence only to prevent the drawing
contract from silently changing the established editable suite.
