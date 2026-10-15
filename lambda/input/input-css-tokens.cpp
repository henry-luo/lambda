#include "input.hpp"
#include "input-context.hpp"
#include "css/css_parser.hpp"

// Token values decode CSS escapes; raw slices preserve comments and authored spelling.
void input_css_tokens(Input* input, const char* source, size_t length) {
    lambda::InputContext ctx(input, source, length);
    if (length > 4 * 1024 * 1024) {
        ctx.addError("css tokens: stylesheet exceeds 4 MiB");
    } else {
        size_t count = 0;
        CssToken* tokens = css_tokenize(source, length, input->pool, &count);
        auto out = ctx.builder.array();
        if (!tokens || count > 262144) ctx.addError("css tokens: token budget exceeded");
        else for (size_t i = 0; i < count; i++) {
            const CssToken& t = tokens[i];
            if (t.type == CSS_TOKEN_EOF) continue;
            if (t.type == CSS_TOKEN_BAD_URL || t.type == CSS_TOKEN_BAD_STRING) {
                ctx.addError("css tokens: malformed string or URL");
                break;
            }
            const char* kind = t.type == CSS_TOKEN_URL ? "url" :
                t.type == CSS_TOKEN_STRING ? "string" :
                t.type == CSS_TOKEN_AT_KEYWORD ? "at" :
                t.type == CSS_TOKEN_FUNCTION ? "function" :
                t.type == CSS_TOKEN_WHITESPACE || t.type == CSS_TOKEN_COMMENT ? "space" : "token";
            out.append(ctx.builder.map().put("kind", kind)
                .put("raw", ctx.builder.createStringItem(t.start, t.length))
                .put("value", t.value ? t.value : "").final());
        }
        input->root = out.final();
    }
    if (ctx.hasErrors()) { input->root = ItemError; input->parse_failed = true; ctx.logErrors(); }
}
