// test/test_pdf_operators.c
// Unit tests for PDF operator parsing

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "../radiant/pdf/operators.h"
#include "../lib/mempool.h"
#include "../lib/log.h"

// Test helper: create input context
static Input* create_test_input() {
    Input* input = (Input*)calloc(1, sizeof(Input));
    input->pool = pool_create(1024 * 1024); // 1MB pool
    input->sb = stringbuf_new(input->pool);
    return input;
}

// Test helper: destroy input context
static void destroy_test_input(Input* input) {
    if (input) {
        if (input->pool) {
            pool_destroy(input->pool);
        }
        free(input);
    }
}

// Test: Graphics state initialization
Test(pdf_operators, graphics_state_init) {
    Pool* pool = pool_create(1024);
    PDFGraphicsState state;

    pdf_graphics_state_init(&state, pool);

    // Check default values
    cr_assert_eq(state.char_spacing, 0.0, "char_spacing should be 0");
    cr_assert_eq(state.word_spacing, 0.0, "word_spacing should be 0");
    cr_assert_eq(state.horizontal_scaling, 100.0, "horizontal_scaling should be 100");
    cr_assert_eq(state.font_size, 0.0, "font_size should be 0");

    // Check identity matrix
    cr_assert_eq(state.tm[0], 1.0, "tm[0] should be 1.0");
    cr_assert_eq(state.tm[1], 0.0, "tm[1] should be 0.0");
    cr_assert_eq(state.tm[2], 0.0, "tm[2] should be 0.0");
    cr_assert_eq(state.tm[3], 1.0, "tm[3] should be 1.0");
    cr_assert_eq(state.tm[4], 0.0, "tm[4] should be 0.0");
    cr_assert_eq(state.tm[5], 0.0, "tm[5] should be 0.0");

    pool_destroy(pool);
}

// Test: Parse BT operator
Test(pdf_operators, parse_bt_operator) {
    Input* input = create_test_input();
    const char* stream = "BT";

    PDFStreamParser* parser = pdf_stream_parser_create(stream, strlen(stream), input->pool, input);
    cr_assert_not_null(parser, "Parser should be created");

    PDFOperator* op = pdf_parse_next_operator(parser);
    cr_assert_not_null(op, "Should parse BT operator");
    cr_assert_eq(op->type, PDF_OP_BT, "Should be BT operator");
    cr_assert_str_eq(op->name, "BT", "Operator name should be BT");

    pdf_stream_parser_destroy(parser);
    destroy_test_input(input);
}

// Test: Parse Tf operator (set font)
Test(pdf_operators, parse_tf_operator) {
    Input* input = create_test_input();
    const char* stream = "/F1 12 Tf";

    PDFStreamParser* parser = pdf_stream_parser_create(stream, strlen(stream), input->pool, input);
    cr_assert_not_null(parser, "Parser should be created");

    PDFOperator* op = pdf_parse_next_operator(parser);
    cr_assert_not_null(op, "Should parse Tf operator");
    cr_assert_eq(op->type, PDF_OP_Tf, "Should be Tf operator");
    cr_assert_not_null(op->operands.set_font.font_name, "Font name should not be null");
    cr_assert_str_eq(op->operands.set_font.font_name->chars, "F1", "Font name should be F1");
    cr_assert_eq(op->operands.set_font.size, 12.0, "Font size should be 12");

    pdf_stream_parser_destroy(parser);
    destroy_test_input(input);
}

// Test: Parse Tm operator (set text matrix)
Test(pdf_operators, parse_tm_operator) {
    Input* input = create_test_input();
    const char* stream = "1 0 0 1 100 700 Tm";

    PDFStreamParser* parser = pdf_stream_parser_create(stream, strlen(stream), input->pool, input);
    cr_assert_not_null(parser, "Parser should be created");

    PDFOperator* op = pdf_parse_next_operator(parser);
    cr_assert_not_null(op, "Should parse Tm operator");
    cr_assert_eq(op->type, PDF_OP_Tm, "Should be Tm operator");
    cr_assert_eq(op->operands.text_matrix.a, 1.0, "a should be 1.0");
    cr_assert_eq(op->operands.text_matrix.b, 0.0, "b should be 0.0");
    cr_assert_eq(op->operands.text_matrix.c, 0.0, "c should be 0.0");
    cr_assert_eq(op->operands.text_matrix.d, 1.0, "d should be 1.0");
    cr_assert_eq(op->operands.text_matrix.e, 100.0, "e should be 100.0");
    cr_assert_eq(op->operands.text_matrix.f, 700.0, "f should be 700.0");

    pdf_stream_parser_destroy(parser);
    destroy_test_input(input);
}

// Test: Parse Tj operator (show text)
Test(pdf_operators, parse_tj_operator) {
    Input* input = create_test_input();
    const char* stream = "(Hello World) Tj";

    PDFStreamParser* parser = pdf_stream_parser_create(stream, strlen(stream), input->pool, input);
    cr_assert_not_null(parser, "Parser should be created");

    PDFOperator* op = pdf_parse_next_operator(parser);
    cr_assert_not_null(op, "Should parse Tj operator");
    cr_assert_eq(op->type, PDF_OP_Tj, "Should be Tj operator");
    cr_assert_not_null(op->operands.show_text.text, "Text should not be null");
    cr_assert_str_eq(op->operands.show_text.text->chars, "Hello World", "Text should be 'Hello World'");

    pdf_stream_parser_destroy(parser);
    destroy_test_input(input);
}

// Test: Parse multiple operators
Test(pdf_operators, parse_multiple_operators) {
    Input* input = create_test_input();
    const char* stream = "BT\n/F1 12 Tf\n1 0 0 1 100 700 Tm\n(Hello) Tj\nET";

    PDFStreamParser* parser = pdf_stream_parser_create(stream, strlen(stream), input->pool, input);
    cr_assert_not_null(parser, "Parser should be created");

    // Parse BT
    PDFOperator* op1 = pdf_parse_next_operator(parser);
    cr_assert_not_null(op1, "Should parse BT");
    cr_assert_eq(op1->type, PDF_OP_BT, "First operator should be BT");

    // Parse Tf
    PDFOperator* op2 = pdf_parse_next_operator(parser);
    cr_assert_not_null(op2, "Should parse Tf");
    cr_assert_eq(op2->type, PDF_OP_Tf, "Second operator should be Tf");

    // Parse Tm
    PDFOperator* op3 = pdf_parse_next_operator(parser);
    cr_assert_not_null(op3, "Should parse Tm");
    cr_assert_eq(op3->type, PDF_OP_Tm, "Third operator should be Tm");

    // Parse Tj
    PDFOperator* op4 = pdf_parse_next_operator(parser);
    cr_assert_not_null(op4, "Should parse Tj");
    cr_assert_eq(op4->type, PDF_OP_Tj, "Fourth operator should be Tj");

    // Parse ET
    PDFOperator* op5 = pdf_parse_next_operator(parser);
    cr_assert_not_null(op5, "Should parse ET");
    cr_assert_eq(op5->type, PDF_OP_ET, "Fifth operator should be ET");

    // No more operators
    PDFOperator* op6 = pdf_parse_next_operator(parser);
    cr_assert_null(op6, "Should have no more operators");

    pdf_stream_parser_destroy(parser);
    destroy_test_input(input);
}

// Test: Graphics state save and restore
Test(pdf_operators, graphics_state_save_restore) {
    Pool* pool = pool_create(1024);
    PDFGraphicsState state;

    pdf_graphics_state_init(&state, pool);

    // Modify state
    state.font_size = 14.0;
    state.tm[4] = 100.0;
    state.tm[5] = 200.0;

    // Save state
    pdf_graphics_state_save(&state);

    // Modify state again
    state.font_size = 18.0;
    state.tm[4] = 300.0;
    state.tm[5] = 400.0;

    // Restore state
    pdf_graphics_state_restore(&state);

    // Check restored values
    cr_assert_eq(state.font_size, 14.0, "font_size should be restored to 14.0");
    cr_assert_eq(state.tm[4], 100.0, "tm[4] should be restored to 100.0");
    cr_assert_eq(state.tm[5], 200.0, "tm[5] should be restored to 200.0");

    pool_destroy(pool);
}

// Test: Parse RGB color operators
Test(pdf_operators, parse_rgb_operators) {
    Input* input = create_test_input();
    const char* stream = "1 0 0 rg 0 1 0 RG";

    PDFStreamParser* parser = pdf_stream_parser_create(stream, strlen(stream), input->pool, input);
    cr_assert_not_null(parser, "Parser should be created");

    // Parse rg (fill color)
    PDFOperator* op1 = pdf_parse_next_operator(parser);
    cr_assert_not_null(op1, "Should parse rg");
    cr_assert_eq(op1->type, PDF_OP_rg, "Should be rg operator");
    cr_assert_eq(op1->operands.rgb_color.r, 1.0, "Red should be 1.0");
    cr_assert_eq(op1->operands.rgb_color.g, 0.0, "Green should be 0.0");
    cr_assert_eq(op1->operands.rgb_color.b, 0.0, "Blue should be 0.0");

    // Parse RG (stroke color)
    PDFOperator* op2 = pdf_parse_next_operator(parser);
    cr_assert_not_null(op2, "Should parse RG");
    cr_assert_eq(op2->type, PDF_OP_RG, "Should be RG operator");
    cr_assert_eq(op2->operands.rgb_color.r, 0.0, "Red should be 0.0");
    cr_assert_eq(op2->operands.rgb_color.g, 1.0, "Green should be 1.0");
    cr_assert_eq(op2->operands.rgb_color.b, 0.0, "Blue should be 0.0");

    pdf_stream_parser_destroy(parser);
    destroy_test_input(input);
}

// Test: Parse string with escape sequences
Test(pdf_operators, parse_escaped_string) {
    Input* input = create_test_input();
    const char* stream = "(Hello\\nWorld\\t!) Tj";

    PDFStreamParser* parser = pdf_stream_parser_create(stream, strlen(stream), input->pool, input);
    cr_assert_not_null(parser, "Parser should be created");

    PDFOperator* op = pdf_parse_next_operator(parser);
    cr_assert_not_null(op, "Should parse Tj operator");
    cr_assert_eq(op->type, PDF_OP_Tj, "Should be Tj operator");
    cr_assert_not_null(op->operands.show_text.text, "Text should not be null");
    cr_assert_str_eq(op->operands.show_text.text->chars, "Hello\nWorld\t!",
                     "Text should have newline and tab");

    pdf_stream_parser_destroy(parser);
    destroy_test_input(input);
}
