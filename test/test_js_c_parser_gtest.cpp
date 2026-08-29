#include <gtest/gtest.h>

#include <string.h>

extern "C" {
#include "../lambda/js/parser/js_parser.h"
}

static int lex_source(const char* source, JsToken* tokens, int capacity) {
    JsLexer lexer;
    js_lexer_init(&lexer, source, strlen(source));
    int count = 0;
    while (count < capacity) {
        tokens[count] = js_lexer_next(&lexer);
        count++;
        if (tokens[count - 1].kind == JS_TOK_EOF ||
                tokens[count - 1].kind == JS_TOK_ERROR) break;
        js_lexer_set_goal(&lexer,
            tokens[count - 1].kind == JS_TOK_IDENTIFIER ||
            tokens[count - 1].kind == JS_TOK_NUMBER ||
            tokens[count - 1].kind == JS_TOK_STRING
                ? JS_LEX_DIV : JS_LEX_REGEXP);
    }
    return count;
}

struct ReductionRecorder {
    int reductions;
    int binary;
    int calls;
    int types;
    SourceSpan last_span;
    JsReductionForm forms[64];
    uint32_t child_counts[64];
};

static bool record_reduction(void* context, const JsParseReduction* reduction) {
    ReductionRecorder* recorder = (ReductionRecorder*)context;
    recorder->reductions++;
    recorder->last_span = reduction->span;
    if (recorder->reductions <= 64) {
        recorder->forms[recorder->reductions - 1] = reduction->form;
        recorder->child_counts[recorder->reductions - 1] =
            reduction->child_count;
    }
    if (reduction->form == JS_REDUCTION_BINARY) recorder->binary++;
    if (reduction->form == JS_REDUCTION_CALL) recorder->calls++;
    if (reduction->kind == JS_REDUCE_TYPE) recorder->types++;
    return true;
}

TEST(JsCParserLexer, UsesParserSelectedRegexGoal) {
    JsToken tokens[32];
    int count = lex_source("let value = /a[\\/]*/giu; value / 2", tokens, 32);
    ASSERT_EQ(count, 9);
    EXPECT_EQ(tokens[0].kind, JS_TOK_LET);
    EXPECT_EQ(tokens[1].kind, JS_TOK_IDENTIFIER);
    EXPECT_EQ(tokens[3].kind, JS_TOK_REGEXP);
    EXPECT_EQ(tokens[5].kind, JS_TOK_IDENTIFIER);
    EXPECT_EQ(tokens[6].kind, JS_TOK_SLASH);
    EXPECT_EQ(tokens[7].kind, JS_TOK_NUMBER);
    EXPECT_EQ(tokens[8].kind, JS_TOK_EOF);
    EXPECT_EQ(tokens[3].span.start_byte, 12u);
    EXPECT_EQ(tokens[3].span.end_byte, 23u);
}

TEST(JsCParserLexer, PreservesCommentsLineTerminatorsAndTemplateSpan) {
    JsToken tokens[16];
    int count = lex_source("a /* comment\n */\n `x${{y: 1}}z`", tokens, 16);
    ASSERT_EQ(count, 10);
    EXPECT_EQ(tokens[0].kind, JS_TOK_IDENTIFIER);
    EXPECT_EQ(tokens[1].kind, JS_TOK_TEMPLATE);
    EXPECT_TRUE(tokens[1].line_terminator_before);
    EXPECT_EQ(tokens[1].span.start_byte, 18u);
    EXPECT_EQ(tokens[1].span.end_byte, 22u);
    EXPECT_EQ(tokens[2].kind, JS_TOK_LBRACE);
    EXPECT_EQ(tokens[3].kind, JS_TOK_IDENTIFIER);
    EXPECT_EQ(tokens[4].kind, JS_TOK_COLON);
    EXPECT_EQ(tokens[5].kind, JS_TOK_NUMBER);
    EXPECT_EQ(tokens[6].kind, JS_TOK_RBRACE);
    EXPECT_EQ(tokens[7].kind, JS_TOK_RBRACE);
    EXPECT_EQ(tokens[8].kind, JS_TOK_TEMPLATE);
    EXPECT_EQ(tokens[8].span.start_byte, 29u);
    EXPECT_EQ(tokens[8].span.end_byte, 31u);
    EXPECT_EQ(tokens[9].kind, JS_TOK_EOF);
}

TEST(JsCParserLexer, RejectsMalformedLiteralsWithoutStalling) {
    JsToken tokens[4];
    int count = lex_source("/unterminated", tokens, 4);
    ASSERT_EQ(count, 1);
    EXPECT_EQ(tokens[0].kind, JS_TOK_ERROR);
    EXPECT_GT(tokens[0].span.end_byte, tokens[0].span.start_byte);
}

TEST(JsCParserLexer, AcceptsGeneratedEcmaScriptIdentifierContinueRanges) {
    JsToken tokens[8];
    int count = lex_source("var _\xE2\x80\x8C\xE2\x80\x8D\xE3\x83\xBB\xEF\xBD\xA5;",
        tokens, 8);
    ASSERT_EQ(count, 4);
    EXPECT_EQ(tokens[0].kind, JS_TOK_VAR);
    EXPECT_EQ(tokens[1].kind, JS_TOK_IDENTIFIER);
    EXPECT_EQ(tokens[2].kind, JS_TOK_SEMICOLON);
    EXPECT_EQ(tokens[3].kind, JS_TOK_EOF);
}

TEST(JsCParserLexer, AcceptsUnicode17IdentifierStartAndContinueRanges) {
    JsToken tokens[8];
    int count = lex_source("var ࢏᫏;", tokens, 8);
    ASSERT_EQ(count, 4);
    EXPECT_EQ(tokens[0].kind, JS_TOK_VAR);
    EXPECT_EQ(tokens[1].kind, JS_TOK_IDENTIFIER);
    EXPECT_EQ(tokens[2].kind, JS_TOK_SEMICOLON);
    EXPECT_EQ(tokens[3].kind, JS_TOK_EOF);
}

TEST(JsCParser, ParsesModernJavaScriptWithOneReductionStream) {
    const char* source =
        "const make = (x = 1, ...rest) => ({value: x + rest.length});\n"
        "const value = () => { return 1; };\n"
        "class Box extends Base { static count = 0; #value; get value() { return this.#value; } }\n"
        "for (const item of list) { if (item?.ok ?? false) continue; }\n"
        "for (const key in object) { value; }\n"
        "label: for (;;) { break label; }\n"
        "try { make(); } catch (error) { throw error; }\n";
    ReductionRecorder recorder = {};
    JsParseSink sink = {record_reduction};
    JsParseMetrics metrics = {};
    JsParseError error = {};
    JsParseStatus status = js_parser_parse_source(source, strlen(source),
        JS_PARSE_SCRIPT, &sink, &recorder, &metrics, &error);
    EXPECT_EQ(status, JS_PARSE_OK) << (error.message ? error.message : "")
        << " at byte " << error.span.start_byte;
    EXPECT_GT(metrics.token_count, 20u);
    EXPECT_GT(metrics.reduction_count, 10u);
    EXPECT_GT(recorder.binary, 0);
    EXPECT_GT(recorder.calls, 0);
    EXPECT_EQ(recorder.last_span.end_byte, strlen(source));
}

TEST(JsCParser, RescansRegexAtStatementBoundaries) {
    const char* source = "value;\n/ready/.test(value);\nif (value)\n/also/.test(value);";
    JsParseError error = {};
    EXPECT_EQ(js_parser_parse_source(source, strlen(source), JS_PARSE_SCRIPT,
        NULL, NULL, NULL, &error), JS_PARSE_OK)
        << (error.message ? error.message : "") << " at byte "
        << error.span.start_byte;
}

TEST(JsCParser, KeepsControlStatementsInsideStatementBlocks) {
    const char* source = "function f() { if (ready) { return; } while (again) { continue; } }";
    ReductionRecorder recorder = {};
    JsParseSink sink = {record_reduction};
    JsParseError error = {};
    EXPECT_EQ(js_parser_parse_source(source, strlen(source), JS_PARSE_SCRIPT,
        &sink, &recorder, NULL, &error), JS_PARSE_OK)
        << (error.message ? error.message : "") << " at byte "
        << error.span.start_byte;
    for (int i = 0; i < recorder.reductions && i < 64; i++) {
        EXPECT_NE(recorder.forms[i], JS_REDUCTION_OBJECT_METHOD);
    }
}

TEST(JsCParser, KeepsAssignmentsInsideStatementBlocks) {
    const char* source = "function f() { value = value + 1; }";
    ReductionRecorder recorder = {};
    JsParseSink sink = {record_reduction};
    JsParseError error = {};
    EXPECT_EQ(js_parser_parse_source(source, strlen(source), JS_PARSE_SCRIPT,
        &sink, &recorder, NULL, &error), JS_PARSE_OK)
        << (error.message ? error.message : "") << " at byte "
        << error.span.start_byte;
    for (int i = 0; i < recorder.reductions && i < 64; i++) {
        EXPECT_NE(recorder.forms[i], JS_REDUCTION_OBJECT_METHOD);
    }
}

TEST(JsCParser, KeepsAwaitInAsyncArrowBodiesContextual) {
    const char* source =
        "const first = async () => await value;\n"
        "const second = async item => await item.next;\n";
    JsParseError error = {};
    EXPECT_EQ(js_parser_parse_source(source, strlen(source), JS_PARSE_SCRIPT,
        NULL, NULL, NULL, &error), JS_PARSE_OK)
        << (error.message ? error.message : "") << " at byte "
        << error.span.start_byte;
}

TEST(JsCParser, EmitsArrowReductionAfterItsParameterAndBodyChildren) {
    const char* source = "x => x + 1; (left, right = 2) => left * right;";
    ReductionRecorder recorder = {};
    JsParseSink sink = {record_reduction};
    JsParseError error = {};
    EXPECT_EQ(js_parser_parse_source(source, strlen(source), JS_PARSE_SCRIPT,
        &sink, &recorder, NULL, &error), JS_PARSE_OK)
        << (error.message ? error.message : "") << " at byte "
        << error.span.start_byte;
    EXPECT_EQ(recorder.forms[4], JS_REDUCTION_ARROW);
    EXPECT_EQ(recorder.child_counts[4], 2u);
}

TEST(JsCParser, ParsesDecoratedMethodsAndImportAttributes) {
    const char* source =
        "import data from 'data.json' with {type: 'json'};\n"
        "@sealed class Service { get() { return 1; } async run() { return await work(); } }\n"
        "const obj = {get value() { return 1; }, async run() { return 2; }};\n";
    JsParseError error = {};
    EXPECT_EQ(js_parser_parse_source(source, strlen(source),
        JS_PARSE_MODULE, NULL, NULL, NULL, &error), JS_PARSE_OK)
        << (error.message ? error.message : "") << " at byte "
        << error.span.start_byte;
}

TEST(JsCParser, ParsesModuleAndTypeScriptExtensions) {
    const char* source =
        "import {read as load, type Value} from 'pkg';\n"
        "export interface Box<T extends object> { value: T; }\n"
        "export type Result<T> = T | null;\n"
        "export const make = <T>(value: T): Result<T> => value satisfies T;\n";
    JsParseMetrics metrics = {};
    JsParseError error = {};
    JsParseStatus status = js_parser_parse_source(source, strlen(source),
        (JsParseMode)(JS_PARSE_MODULE | JS_PARSE_TYPESCRIPT), NULL, NULL,
        &metrics, &error);
    EXPECT_EQ(status, JS_PARSE_OK) << (error.message ? error.message : "")
        << " at byte " << error.span.start_byte;
    EXPECT_GT(metrics.reduction_count, 10u);
}

TEST(JsCParser, ParsesTypeScriptUnaryExpressionsAndTypedFunctions) {
    const char* source =
        "console.log(typeof 42);\n"
        "function add(x: number, y: number): number { return x + y; }\n"
        "const multiply = (a: number, b: number): number => a * b;\n";
    JsParseError error = {};
    JsParseStatus status = js_parser_parse_source(source, strlen(source),
        (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT), NULL, NULL,
        NULL, &error);
    EXPECT_EQ(status, JS_PARSE_OK) << (error.message ? error.message : "")
        << " at byte " << error.span.start_byte;
}

TEST(JsCParser, ReportsContextAndLineTerminatorErrors) {
    JsParseError error = {};
    EXPECT_EQ(js_parser_parse_source("throw\nvalue", 11, JS_PARSE_SCRIPT,
        NULL, NULL, NULL, &error), JS_PARSE_ERROR);
    EXPECT_EQ(error.code, JS_PARSE_ERROR_LINE_TERMINATOR);

    memset(&error, 0, sizeof(error));
    EXPECT_EQ(js_parser_parse_source("import 'x';", 11, JS_PARSE_SCRIPT,
        NULL, NULL, NULL, &error), JS_PARSE_ERROR);
    EXPECT_EQ(error.code, JS_PARSE_ERROR_CONTEXT);
}

TEST(JsCParser, RejectsUnclosedAndInvalidSourcesAsIncompleteOrError) {
    JsParseError error = {};
    EXPECT_EQ(js_parser_parse_source("function f() {", 14, JS_PARSE_SCRIPT,
        NULL, NULL, NULL, &error), JS_PARSE_INCOMPLETE);
    EXPECT_EQ(error.code, JS_PARSE_ERROR_UNEXPECTED_EOF);

    memset(&error, 0, sizeof(error));
    EXPECT_EQ(js_parser_parse_source("const = 1;", 10, JS_PARSE_SCRIPT,
        NULL, NULL, NULL, &error), JS_PARSE_ERROR);
    EXPECT_EQ(error.actual_kind, JS_TOK_EQUAL);
}
