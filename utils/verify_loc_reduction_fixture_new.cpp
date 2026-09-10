#include "fixture.hpp"

// this comment contains code-shaped text: int ignored = 0;
const char* markers = "// /* not a comment */";
#if FIXTURE_ENABLED
const char* url = "https://example.test/a/*b*/";
/*
 * this block comment spans multiple lines
 * and must not contribute to the code count
 */
int fixture_kept = 1;
#endif
