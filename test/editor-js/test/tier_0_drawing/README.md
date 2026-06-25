# Tier 0 — Native Stage-4 drawing tests

No external precedent — drawing blocks are Lambda/Radiant's own work. Cases derived from the Stage-4 design doc §14.2 catalog, ~150 fixtures across:

| Category | Cases | Cf. Stage-4 §14.2 |
|---|---|---|
| Schema validation             | 10 | PM model tests pattern |
| `SourcePos` over drawings     | 6 |
| Step apply/invert/map         | 10 per step kind |
| Tool: select                  | 8 | tldraw select tool |
| Tool: shape tools             | 5 per tool (rect, ellipse, line, polyline, path, freehand, text) |
| Tool: connector               | 8 | maxGraph mxConnector tests |
| Routing                       | 12 | maxGraph EdgeStyle |
| Snap                          | 6 | tldraw snap |
| Align / distribute            | 10 |
| Multi-select                  | 6 |
| Mode switch                   | 6 |
| Group / ungroup               | 5 |
| Z-order                       | 4 |
| History coalesce              | 8 |
| Clipboard                     | 8 |
| `text-frame` interop          | 5 |
| Performance budgets           | 4 |

## Example fixture shape

See `_example/` — one fully fleshed-out case showing the HTML + events.json + expected-output format for a drag-rect operation. Every other Tier 0 fixture follows the same shape.
