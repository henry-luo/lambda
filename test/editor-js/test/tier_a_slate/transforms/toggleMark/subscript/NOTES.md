# Note: subscript fixture

`subscript` is in our canonical mark vocabulary (Inline_Formatting §5.1) but
the renderer doesn't yet emit a CSS style for it — so the fixture's expected
output is a bare `<span>` with the mark in the dict but no visible style.
The model carries `marks: { subscript: true }` correctly; only the visual
projection is incomplete. Fixture passes as far as doc/selection equality.
