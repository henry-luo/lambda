#include <gtest/gtest.h>

extern "C" {
#include "../lambda/runtime/parser/lambda_rd_parser.h"
}

#include <string.h>

static int lex_all(const char* source, LambdaToken* tokens, int capacity) {
    LambdaLexer lexer;
    lambda_lexer_init(&lexer, source, strlen(source));
    int count = 0;
    while (count < capacity) {
        LambdaToken token = lambda_lexer_next(&lexer);
        tokens[count++] = token;
        if (token.kind == LAMBDA_TOK_EOF || token.kind == LAMBDA_TOK_ERROR) break;
    }
    return count;
}

static void expect_kinds(const char* source, const LambdaTokenKind* expected,
        int expected_count) {
    LambdaToken actual[96];
    int actual_count = lex_all(source, actual, (int)(sizeof(actual) / sizeof(actual[0])));
    ASSERT_EQ(actual_count, expected_count) << source;
    for (int i = 0; i < expected_count; i++) {
        EXPECT_EQ(actual[i].kind, expected[i]) << "token " << i << " in " << source;
    }
}

struct ParserSeamRecorder {
    LambdaSourceSpan type_span;
    LambdaSourceSpan path_span;
    int type_count;
    int path_count;
};

struct ReductionMetadataRecorder {
    LambdaParseReduction atom;
    LambdaParseReduction binary;
    LambdaParseReduction postfix;
    int atom_count;
    int binary_count;
    int postfix_count;
};

static LambdaParseValue record_parser_seam(void* context,
        const LambdaParseReduction* reduction) {
    ParserSeamRecorder* recorder = (ParserSeamRecorder*)context;
    if (reduction->kind == LAMBDA_REDUCE_TYPE_SLOT) {
        recorder->type_span = reduction->span;
        recorder->type_count++;
    }
    if (reduction->kind == LAMBDA_REDUCE_PATH_SLOT) {
        recorder->path_span = reduction->span;
        recorder->path_count++;
    }
    return 0;
}

static LambdaParseValue record_reduction_metadata(void* context,
        const LambdaParseReduction* reduction) {
    ReductionMetadataRecorder* recorder = (ReductionMetadataRecorder*)context;
    if (reduction->kind == LAMBDA_REDUCE_ATOM &&
            reduction->form == LAMBDA_REDUCTION_FORM_TOKEN) {
        recorder->atom = *reduction;
        recorder->atom_count++;
    }
    if (reduction->kind == LAMBDA_REDUCE_BINARY &&
            reduction->form == LAMBDA_REDUCTION_FORM_TOKEN) {
        recorder->binary = *reduction;
        recorder->binary_count++;
    }
    if (reduction->kind == LAMBDA_REDUCE_POSTFIX &&
            reduction->form == LAMBDA_REDUCTION_FORM_CALL) {
        recorder->postfix = *reduction;
        recorder->postfix_count++;
    }
    return 0;
}

TEST(LambdaRdLexerPoc, PreservesLambdaKeywordsLiteralsAndPostfixOperators) {
    static const LambdaTokenKind expected[] = {
        LAMBDA_TOK_LET, LAMBDA_TOK_IDENTIFIER, LAMBDA_TOK_COLON,
        LAMBDA_TOK_BASE_TYPE, LAMBDA_TOK_EQ, LAMBDA_TOK_IDENTIFIER,
        LAMBDA_TOK_LPAREN, LAMBDA_TOK_IDENTIFIER, LAMBDA_TOK_COMMA,
        LAMBDA_TOK_LBRACKET, LAMBDA_TOK_INTEGER, LAMBDA_TOK_COMMA,
        LAMBDA_TOK_SIZED_FLOAT, LAMBDA_TOK_RBRACKET, LAMBDA_TOK_COMMA,
        LAMBDA_TOK_LBRACE, LAMBDA_TOK_IDENTIFIER, LAMBDA_TOK_COLON,
        LAMBDA_TOK_SYMBOL, LAMBDA_TOK_RBRACE, LAMBDA_TOK_RPAREN,
        LAMBDA_TOK_NEWLINE, LAMBDA_TOK_EOF,
    };
    expect_kinds("let value: int = start(worker, [1, 2.5f64], {mode: 'async'})\n",
        expected, (int)(sizeof(expected) / sizeof(expected[0])));
}

TEST(LambdaRdLexerPoc, PreservesStatementNewlineAfterLineComment) {
    static const LambdaTokenKind expected[] = {
        LAMBDA_TOK_IDENTIFIER, LAMBDA_TOK_NEWLINE, LAMBDA_TOK_IDENTIFIER,
        LAMBDA_TOK_PLUS, LAMBDA_TOK_IDENTIFIER, LAMBDA_TOK_EOF,
    };
    expect_kinds("left // trailing comment\n right /* hidden\ncomment */ + value",
        expected, (int)(sizeof(expected) / sizeof(expected[0])));
}

TEST(LambdaRdLexerPoc, RecognizesOpaquePatternAndPathBuildingBlocks) {
    static const LambdaTokenKind expected[] = {
        LAMBDA_TOK_BINARY, LAMBDA_TOK_DATETIME, LAMBDA_TOK_STRING,
        LAMBDA_TOK_SYMBOL, LAMBDA_TOK_PATTERN_ISLAND, LAMBDA_TOK_DOT_QUESTION,
        LAMBDA_TOK_PARENT, LAMBDA_TOK_TILDE_INDEX, LAMBDA_TOK_ELLIPSIS,
        LAMBDA_TOK_STAR_STAR, LAMBDA_TOK_PIPE_FORWARD, LAMBDA_TOK_EOF,
    };
    expect_kinds("b'AA==' t'2026-08-19T01:02Z' \"x\\n\" 'y' \\(d[3]) .? ~~ ~# ... ** |>",
        expected, (int)(sizeof(expected) / sizeof(expected[0])));
}

TEST(LambdaRdLexerPoc, KeepsUnicodeAndEscapedIdentifiersAsOneToken) {
    static const LambdaTokenKind expected[] = {
        LAMBDA_TOK_IDENTIFIER, LAMBDA_TOK_IDENTIFIER, LAMBDA_TOK_EOF,
    };
    expect_kinds("\\u0061 α", expected, (int)(sizeof(expected) / sizeof(expected[0])));
}

TEST(LambdaRdLexerPoc, RejectsMalformedLiteralWithoutStalling) {
    LambdaToken tokens[4];
    int count = lex_all("\"bad\\q\"", tokens, (int)(sizeof(tokens) / sizeof(tokens[0])));
    ASSERT_EQ(count, 1);
    EXPECT_EQ(tokens[0].kind, LAMBDA_TOK_ERROR);
    EXPECT_GT(tokens[0].span.end_byte, tokens[0].span.start_byte);
}

TEST(LambdaRdLexerPoc, RecordsByteSpansAndLineColumns) {
    LambdaToken tokens[8];
    int count = lex_all("a\r\n  beta", tokens, (int)(sizeof(tokens) / sizeof(tokens[0])));
    ASSERT_EQ(count, 4);
    EXPECT_EQ(tokens[0].span.start_byte, 0u);
    EXPECT_EQ(tokens[0].span.end_byte, 1u);
    EXPECT_EQ(tokens[1].kind, LAMBDA_TOK_NEWLINE);
    EXPECT_EQ(tokens[1].span.start_byte, 1u);
    EXPECT_EQ(tokens[1].span.end_byte, 3u);
    EXPECT_EQ(tokens[2].kind, LAMBDA_TOK_IDENTIFIER);
    EXPECT_EQ(tokens[2].line, 2u);
    EXPECT_EQ(tokens[2].column, 2u);
}

TEST(LambdaRdParserPoc, ParsesPrattExpressionsAndTopLevelDeclarations) {
    const char* source =
        "let result = start(worker, [1, 2 + 3]) |> format\n"
        "fn render(value: int) => value.?int * 2\n"
        "let displayed = if (result) result else null\n";
    LambdaParseMetrics metrics = {};
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        &metrics, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
    EXPECT_GT(metrics.token_count, 0u);
    EXPECT_GT(metrics.reduction_count, 0u);
    EXPECT_NE(metrics.structural_hash, 0u);
}

TEST(LambdaRdParserPoc, ReportsIncompleteDelimitedExpression) {
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source("let value = [1", strlen("let value = [1"),
        NULL, NULL, NULL, &error), LAMBDA_PARSE_INCOMPLETE);
    EXPECT_EQ(error.actual_kind, LAMBDA_TOK_EOF);
}

TEST(LambdaRdParserPoc, KeepsTypeAndStaticPathAsCommittedSourceSlots) {
    const char* source =
        "fn select(record: {id: int, tags: [string]}) => .records.~~.id\n"
        "let selected = select(data).?int\n";
    LambdaParseMetrics metrics = {};
    LambdaParseError error = {};
    ParserSeamRecorder recorder = {};
    LambdaParseSink sink = {record_parser_seam};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), &sink, &recorder,
        &metrics, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
    // The Phase 1 parser records seams, while Phase 2 will hand these exact
    // spans to the existing Lambda type/path AST parsers.
    EXPECT_EQ(recorder.type_count, 2);
    EXPECT_EQ(recorder.path_count, 1);
    EXPECT_EQ(strncmp(source + recorder.type_span.start_byte, "int", 3), 0);
    EXPECT_EQ(strncmp(source + recorder.path_span.start_byte, ".records.~~.id", 14), 0);
}

TEST(LambdaRdParserPoc, PublishesCommittedTokenFormsAndCompletePrattSpans) {
    const char* source = "start(1 + 2)";
    LambdaParseError error = {};
    ReductionMetadataRecorder recorder = {};
    LambdaParseSink sink = {record_reduction_metadata};
    ASSERT_EQ(lambda_rd_parse_source(source, strlen(source), &sink, &recorder,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");

    ASSERT_GT(recorder.atom_count, 0);
    ASSERT_EQ(recorder.binary_count, 1);
    ASSERT_EQ(recorder.postfix_count, 1);
    EXPECT_EQ(recorder.binary.detail_token.kind, LAMBDA_TOK_PLUS);
    EXPECT_EQ(recorder.binary.detail_token.span.start_byte, 8u);
    EXPECT_EQ(recorder.binary.span.start_byte, 6u);
    EXPECT_EQ(recorder.binary.span.end_byte, 11u);
    EXPECT_EQ(recorder.postfix.detail_token.kind, LAMBDA_TOK_LPAREN);
    EXPECT_EQ(recorder.postfix.span.start_byte, 0u);
    EXPECT_EQ(recorder.postfix.span.end_byte, strlen(source));
}

TEST(LambdaRdParserPoc, ParsesTypeAliasAndObjectDeclarationShells) {
    const char* source =
        "type Id = int | string\n"
        "type Record { id: Id, tags: [string]; fn label() => .id }\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesTypedAnonymousArrowHead) {
    const char* source = "let add = (left: int, right: int) => left + right\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, DistinguishesTypeDeclarationFromTypeFunctionCall) {
    const char* source = "type(value)\ntype Alias = type\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesElementAttributesAndContentBoundary) {
    const char* source =
        "<svg.root width: float(width), 'aria-label': label;\n"
        "  <text fill: color; text_value>\n"
        ">\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesQualifiedAttributesAndDirectElementContent) {
    const char* source =
        "<svg.rect\n"
        "  svg.width: 100,\n"
        "  svg.height: 50>\n"
        "let doc = <doc <paragraph; \"Hello\">>\n"
        "let rich = <div; <strong; \"Alpha\"> <span; \"Beta\">>\n"
        "let typed_tag = <list; <string; \"entry\">>\n"
        "let path_child = <svg; .rect>\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesForClausesAndProceduralWhile) {
    const char* source =
        "let values = for (x in [3, 1, 2], let doubled = x * 2 where doubled > 2 order by doubled desc limit 2) doubled\n"
        "pn main() {\n"
        "  var i = 0\n"
        "  if (i == 0) { i = i + 1 }\n"
        "  while (i < 3) { i = i + 1 }\n"
        "}\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesExpressionAndBlockMatchArms) {
    const char* source =
        "match value {\n"
        "  case int:\n"
        "    value + 1\n"
        "  case string { value ++ \"!\" }\n"
        "  default: null\n"
        "}\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesViewStateEventsAndApplyForms) {
    const char* source =
        "view Card: <card title: string; string> (value: int) string state selected: false {\n"
        "  apply;\n"
        "  value |> apply()\n"
        "}\n"
        "on click(event) { var selected = true }\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesMultilineDelimitedCollections) {
    const char* source =
        "let palette = [\n"
        "  \"red\",\n"
        "  \"blue\"\n"
        "]\n"
        "let settings = {\n"
        "  color: palette,\n"
        "  width: 3\n"
        "}\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesBlockArmsForParenthesisFreeIfExpression) {
    const char* source =
        "let config = if enabled { {*:defaults, *:override} }\n"
        "  else { defaults }\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesValueFormElseIfInsideBlockStatement) {
    const char* source =
        "fn marker(value) {\n"
        "  if (value == null) { \"missing\" }\n"
        "  else if (value == true) \"enabled\"\n"
        "  else if (value == false) \"disabled\"\n"
        "  else string(value)\n"
        "}\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, DisambiguatesFinalIfExpressionFromBlockStatement) {
    const char* source =
        "fn choose(flag) {\n"
        "  if (flag) \"yes\" else \"no\"\n"
        "}\n"
        "pn update(flag) { if (flag) { var changed = true } }\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesSoftNamesAndDelimitedForContinuations) {
    const char* source =
        "fn move(from: int[], to, at) int[] =>\n"
        "  call(to: to, at: at, values: [from, to])\n"
        "fn apply(state, view) => state\n"
        "let record = {group: move(from: 1, to: 2, at: 3), type: \"point\"}\n"
        "let cell = matrix[\n"
        "  row,\n"
        "  column\n"
        "]\n"
        "let ticks = for (value in values\n"
        "  group by value.region as region into bucket)\n"
        "  format(bucket.region)\n"
        "let records = for (entry in entries) {name: entry.name}\n"
        "let pad = if (enabled) padding else {top: 20, right: 20}\n"
        "let compared = cell gt 0 and cell ne null\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesBindingListsAndPublicAssignments) {
    const char* source =
        "let a = 123, b = a * 2, c = a + 2\n"
        "let left, right = pair\n"
        "let key, value at record\n"
        "pub exported = a\n"
        "pn update() { var first, second = pair; var count = 0 }\n"
        "pn read_state(state) { return state.count }\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesNewlineContinuationAfterValueIntroducers) {
    const char* source =
        "pub DEFAULT =\n"
        "  <style; \"body\">\n"
        "let settings = {\n"
        "  palette:\n"
        "    [\"blue\", \"red\"]\n"
        "}\n"
        "let final = settings.palette[\n"
        "  last\n"
        "]\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ContinuesExpressionAtALeadingBinaryOperator) {
    const char* source =
        "fn page() =>\n"
        "  \"<html>\"\n"
        "  ++ \"<body>\"\n"
        "  ++ \"</body></html>\"\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, ParsesContextualNamesPathsAndMatchPatterns) {
    const char* source =
        "import mermaid_state: .mermaid.state\n"
        "pn step() { var fn = 1 }\n"
        "let schema = {default: /.src.**}\n"
        "let fields = [for (key:int, value in items) key]\n"
        "let node = <node; if (len(fields) > 0) { <item; \"present\"> }>\n"
        "match score {\n"
        "  case 90 to 100: \"A\"\n"
        "  case int that (~ > 0): \"positive\"\n"
        "  default: \"other\"\n"
        "}\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}

TEST(LambdaRdParserPoc, RejectsKnownStatementScopeAmbiguities) {
    LambdaParseError error = {};
    EXPECT_NE(lambda_rd_parse_source("(// comment\n)", strlen("(// comment\n)"),
        NULL, NULL, NULL, &error), LAMBDA_PARSE_OK);
    EXPECT_NE(lambda_rd_parse_source("\"a\" < \"b\"", strlen("\"a\" < \"b\""),
        NULL, NULL, NULL, &error), LAMBDA_PARSE_OK);
    EXPECT_EQ(lambda_rd_parse_source("(\"a\" < \"b\")", strlen("(\"a\" < \"b\")"),
        NULL, NULL, NULL, &error), LAMBDA_PARSE_OK);
}

TEST(LambdaRdParserPoc, ParsesMultilineTypesJoinsAndAdjacentContentForms) {
    const char* source =
        "type Byte = 0 to 255\n"
        "type Callback = fn (value: int) int\n"
        "let percent: int that 0 <= ~ <= 100 = 50\n"
        "let union: int |\n"
        "  string = 1\n"
        "let path_parts = [.a.1, record.1]\n"
        "let joined = [for (left in lefts, right in rights on left.id == right.id) right]\n"
        "let xml = input('test.xml'),\n"
        "  more_xml = input('more.xml')\n"
        "<root> <item> \"one\" <item> \"two\"\n"
        "<item> is element\n"
        "pn store(var data, i) { if (data[i] == null) { data[i] = 1 data[i] = 2 } }\n";
    LambdaParseError error = {};
    EXPECT_EQ(lambda_rd_parse_source(source, strlen(source), NULL, NULL,
        NULL, &error), LAMBDA_PARSE_OK) << (error.message ? error.message : "");
}
