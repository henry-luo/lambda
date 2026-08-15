#pragma once

// broad property-set profiling was retired; keep the source-level hook inert
// for runtime modules that still include this compatibility header.
#define JS_WEAK_PROPERTY_SET_BRANCH(label) ((void)0)
