// Keep the first-party parser entry points in the same archive member as the
// existing Lambda parser factory. Static-link extraction is member-based: the
// AST builder and the RD/Pratt implementation are otherwise invisible to the
// linker when both symbols live only inside lambda-rt-cpp.a.
#include "parser/lambda_lexer.c"
#include "parser/lambda_parser.c"
