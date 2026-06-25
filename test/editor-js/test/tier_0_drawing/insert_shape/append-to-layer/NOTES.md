# Note: shape ID counter is process-local

`makeRectShape` allocates a fresh ID via `nextShapeId('r')` which depends on a
module-level counter. The test runner resets the counter via
`_resetShapeIdCounter()` at the start of each fixture (see fixture-runner). Don't
depend on a specific id value across multiple fixtures in one process.
