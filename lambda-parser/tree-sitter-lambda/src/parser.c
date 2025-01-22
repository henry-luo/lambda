#include "tree_sitter/parser.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define LANGUAGE_VERSION 14
#define STATE_COUNT 79
#define LARGE_STATE_COUNT 2
#define SYMBOL_COUNT 56
#define ALIAS_COUNT 0
#define TOKEN_COUNT 37
#define EXTERNAL_TOKEN_COUNT 0
#define FIELD_COUNT 6
#define MAX_ALIAS_SEQUENCE_LENGTH 4
#define PRODUCTION_ID_COUNT 4

enum ts_symbol_identifiers {
  anon_sym_LBRACE = 1,
  anon_sym_COMMA = 2,
  anon_sym_RBRACE = 3,
  anon_sym_COLON = 4,
  anon_sym_LBRACK = 5,
  anon_sym_RBRACK = 6,
  anon_sym_to = 7,
  anon_sym_DQUOTE = 8,
  sym_string_content = 9,
  anon_sym_SQUOTE = 10,
  sym_symbol_content = 11,
  sym_escape_sequence = 12,
  sym_number = 13,
  sym_true = 14,
  sym_false = 15,
  sym_null = 16,
  sym_comment = 17,
  anon_sym_LPAREN = 18,
  anon_sym_RPAREN = 19,
  anon_sym_and = 20,
  anon_sym_or = 21,
  anon_sym_PLUS = 22,
  anon_sym_DASH = 23,
  anon_sym_STAR = 24,
  anon_sym_SLASH = 25,
  anon_sym_PERCENT = 26,
  anon_sym_STAR_STAR = 27,
  anon_sym_LT = 28,
  anon_sym_LT_EQ = 29,
  anon_sym_EQ_EQ = 30,
  anon_sym_BANG_EQ = 31,
  anon_sym_GT_EQ = 32,
  anon_sym_GT = 33,
  anon_sym_in = 34,
  anon_sym_not = 35,
  sym_identifier = 36,
  sym_document = 37,
  sym__value = 38,
  sym_object = 39,
  sym_pair = 40,
  sym_array = 41,
  sym_range = 42,
  sym_string = 43,
  aux_sym__string_content = 44,
  sym_symbol = 45,
  aux_sym__symbol_content = 46,
  sym_parenthesized_expression = 47,
  sym_expression = 48,
  sym_primary_expression = 49,
  sym_binary_expression = 50,
  sym_unary_expression = 51,
  sym__identifier = 52,
  aux_sym_document_repeat1 = 53,
  aux_sym_object_repeat1 = 54,
  aux_sym_array_repeat1 = 55,
};

static const char * const ts_symbol_names[] = {
  [ts_builtin_sym_end] = "end",
  [anon_sym_LBRACE] = "{",
  [anon_sym_COMMA] = ",",
  [anon_sym_RBRACE] = "}",
  [anon_sym_COLON] = ":",
  [anon_sym_LBRACK] = "[",
  [anon_sym_RBRACK] = "]",
  [anon_sym_to] = "to",
  [anon_sym_DQUOTE] = "\"",
  [sym_string_content] = "string_content",
  [anon_sym_SQUOTE] = "'",
  [sym_symbol_content] = "symbol_content",
  [sym_escape_sequence] = "escape_sequence",
  [sym_number] = "number",
  [sym_true] = "true",
  [sym_false] = "false",
  [sym_null] = "null",
  [sym_comment] = "comment",
  [anon_sym_LPAREN] = "(",
  [anon_sym_RPAREN] = ")",
  [anon_sym_and] = "and",
  [anon_sym_or] = "or",
  [anon_sym_PLUS] = "+",
  [anon_sym_DASH] = "-",
  [anon_sym_STAR] = "*",
  [anon_sym_SLASH] = "/",
  [anon_sym_PERCENT] = "%",
  [anon_sym_STAR_STAR] = "**",
  [anon_sym_LT] = "<",
  [anon_sym_LT_EQ] = "<=",
  [anon_sym_EQ_EQ] = "==",
  [anon_sym_BANG_EQ] = "!=",
  [anon_sym_GT_EQ] = ">=",
  [anon_sym_GT] = ">",
  [anon_sym_in] = "in",
  [anon_sym_not] = "not",
  [sym_identifier] = "identifier",
  [sym_document] = "document",
  [sym__value] = "_value",
  [sym_object] = "object",
  [sym_pair] = "pair",
  [sym_array] = "array",
  [sym_range] = "range",
  [sym_string] = "string",
  [aux_sym__string_content] = "_string_content",
  [sym_symbol] = "symbol",
  [aux_sym__symbol_content] = "_symbol_content",
  [sym_parenthesized_expression] = "parenthesized_expression",
  [sym_expression] = "expression",
  [sym_primary_expression] = "primary_expression",
  [sym_binary_expression] = "binary_expression",
  [sym_unary_expression] = "unary_expression",
  [sym__identifier] = "_identifier",
  [aux_sym_document_repeat1] = "document_repeat1",
  [aux_sym_object_repeat1] = "object_repeat1",
  [aux_sym_array_repeat1] = "array_repeat1",
};

static const TSSymbol ts_symbol_map[] = {
  [ts_builtin_sym_end] = ts_builtin_sym_end,
  [anon_sym_LBRACE] = anon_sym_LBRACE,
  [anon_sym_COMMA] = anon_sym_COMMA,
  [anon_sym_RBRACE] = anon_sym_RBRACE,
  [anon_sym_COLON] = anon_sym_COLON,
  [anon_sym_LBRACK] = anon_sym_LBRACK,
  [anon_sym_RBRACK] = anon_sym_RBRACK,
  [anon_sym_to] = anon_sym_to,
  [anon_sym_DQUOTE] = anon_sym_DQUOTE,
  [sym_string_content] = sym_string_content,
  [anon_sym_SQUOTE] = anon_sym_SQUOTE,
  [sym_symbol_content] = sym_symbol_content,
  [sym_escape_sequence] = sym_escape_sequence,
  [sym_number] = sym_number,
  [sym_true] = sym_true,
  [sym_false] = sym_false,
  [sym_null] = sym_null,
  [sym_comment] = sym_comment,
  [anon_sym_LPAREN] = anon_sym_LPAREN,
  [anon_sym_RPAREN] = anon_sym_RPAREN,
  [anon_sym_and] = anon_sym_and,
  [anon_sym_or] = anon_sym_or,
  [anon_sym_PLUS] = anon_sym_PLUS,
  [anon_sym_DASH] = anon_sym_DASH,
  [anon_sym_STAR] = anon_sym_STAR,
  [anon_sym_SLASH] = anon_sym_SLASH,
  [anon_sym_PERCENT] = anon_sym_PERCENT,
  [anon_sym_STAR_STAR] = anon_sym_STAR_STAR,
  [anon_sym_LT] = anon_sym_LT,
  [anon_sym_LT_EQ] = anon_sym_LT_EQ,
  [anon_sym_EQ_EQ] = anon_sym_EQ_EQ,
  [anon_sym_BANG_EQ] = anon_sym_BANG_EQ,
  [anon_sym_GT_EQ] = anon_sym_GT_EQ,
  [anon_sym_GT] = anon_sym_GT,
  [anon_sym_in] = anon_sym_in,
  [anon_sym_not] = anon_sym_not,
  [sym_identifier] = sym_identifier,
  [sym_document] = sym_document,
  [sym__value] = sym__value,
  [sym_object] = sym_object,
  [sym_pair] = sym_pair,
  [sym_array] = sym_array,
  [sym_range] = sym_range,
  [sym_string] = sym_string,
  [aux_sym__string_content] = aux_sym__string_content,
  [sym_symbol] = sym_symbol,
  [aux_sym__symbol_content] = aux_sym__symbol_content,
  [sym_parenthesized_expression] = sym_parenthesized_expression,
  [sym_expression] = sym_expression,
  [sym_primary_expression] = sym_primary_expression,
  [sym_binary_expression] = sym_binary_expression,
  [sym_unary_expression] = sym_unary_expression,
  [sym__identifier] = sym__identifier,
  [aux_sym_document_repeat1] = aux_sym_document_repeat1,
  [aux_sym_object_repeat1] = aux_sym_object_repeat1,
  [aux_sym_array_repeat1] = aux_sym_array_repeat1,
};

static const TSSymbolMetadata ts_symbol_metadata[] = {
  [ts_builtin_sym_end] = {
    .visible = false,
    .named = true,
  },
  [anon_sym_LBRACE] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_COMMA] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_RBRACE] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_COLON] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_LBRACK] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_RBRACK] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_to] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_DQUOTE] = {
    .visible = true,
    .named = false,
  },
  [sym_string_content] = {
    .visible = true,
    .named = true,
  },
  [anon_sym_SQUOTE] = {
    .visible = true,
    .named = false,
  },
  [sym_symbol_content] = {
    .visible = true,
    .named = true,
  },
  [sym_escape_sequence] = {
    .visible = true,
    .named = true,
  },
  [sym_number] = {
    .visible = true,
    .named = true,
  },
  [sym_true] = {
    .visible = true,
    .named = true,
  },
  [sym_false] = {
    .visible = true,
    .named = true,
  },
  [sym_null] = {
    .visible = true,
    .named = true,
  },
  [sym_comment] = {
    .visible = true,
    .named = true,
  },
  [anon_sym_LPAREN] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_RPAREN] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_and] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_or] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_PLUS] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_DASH] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_STAR] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_SLASH] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_PERCENT] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_STAR_STAR] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_LT] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_LT_EQ] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_EQ_EQ] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_BANG_EQ] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_GT_EQ] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_GT] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_in] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_not] = {
    .visible = true,
    .named = false,
  },
  [sym_identifier] = {
    .visible = true,
    .named = true,
  },
  [sym_document] = {
    .visible = true,
    .named = true,
  },
  [sym__value] = {
    .visible = false,
    .named = true,
    .supertype = true,
  },
  [sym_object] = {
    .visible = true,
    .named = true,
  },
  [sym_pair] = {
    .visible = true,
    .named = true,
  },
  [sym_array] = {
    .visible = true,
    .named = true,
  },
  [sym_range] = {
    .visible = true,
    .named = true,
  },
  [sym_string] = {
    .visible = true,
    .named = true,
  },
  [aux_sym__string_content] = {
    .visible = false,
    .named = false,
  },
  [sym_symbol] = {
    .visible = true,
    .named = true,
  },
  [aux_sym__symbol_content] = {
    .visible = false,
    .named = false,
  },
  [sym_parenthesized_expression] = {
    .visible = true,
    .named = true,
  },
  [sym_expression] = {
    .visible = true,
    .named = true,
  },
  [sym_primary_expression] = {
    .visible = true,
    .named = true,
  },
  [sym_binary_expression] = {
    .visible = true,
    .named = true,
  },
  [sym_unary_expression] = {
    .visible = true,
    .named = true,
  },
  [sym__identifier] = {
    .visible = false,
    .named = true,
  },
  [aux_sym_document_repeat1] = {
    .visible = false,
    .named = false,
  },
  [aux_sym_object_repeat1] = {
    .visible = false,
    .named = false,
  },
  [aux_sym_array_repeat1] = {
    .visible = false,
    .named = false,
  },
};

enum ts_field_identifiers {
  field_argument = 1,
  field_key = 2,
  field_left = 3,
  field_operator = 4,
  field_right = 5,
  field_value = 6,
};

static const char * const ts_field_names[] = {
  [0] = NULL,
  [field_argument] = "argument",
  [field_key] = "key",
  [field_left] = "left",
  [field_operator] = "operator",
  [field_right] = "right",
  [field_value] = "value",
};

static const TSFieldMapSlice ts_field_map_slices[PRODUCTION_ID_COUNT] = {
  [1] = {.index = 0, .length = 2},
  [2] = {.index = 2, .length = 2},
  [3] = {.index = 4, .length = 3},
};

static const TSFieldMapEntry ts_field_map_entries[] = {
  [0] =
    {field_argument, 1},
    {field_operator, 0},
  [2] =
    {field_key, 0},
    {field_value, 2},
  [4] =
    {field_left, 0},
    {field_operator, 1},
    {field_right, 2},
};

static const TSSymbol ts_alias_sequences[PRODUCTION_ID_COUNT][MAX_ALIAS_SEQUENCE_LENGTH] = {
  [0] = {0},
};

static const uint16_t ts_non_terminal_alias_map[] = {
  0,
};

static const TSStateId ts_primary_state_ids[STATE_COUNT] = {
  [0] = 0,
  [1] = 1,
  [2] = 2,
  [3] = 2,
  [4] = 4,
  [5] = 5,
  [6] = 6,
  [7] = 7,
  [8] = 8,
  [9] = 9,
  [10] = 10,
  [11] = 11,
  [12] = 12,
  [13] = 13,
  [14] = 14,
  [15] = 15,
  [16] = 16,
  [17] = 17,
  [18] = 18,
  [19] = 19,
  [20] = 20,
  [21] = 21,
  [22] = 22,
  [23] = 23,
  [24] = 24,
  [25] = 25,
  [26] = 26,
  [27] = 27,
  [28] = 28,
  [29] = 29,
  [30] = 30,
  [31] = 31,
  [32] = 32,
  [33] = 33,
  [34] = 16,
  [35] = 35,
  [36] = 36,
  [37] = 37,
  [38] = 38,
  [39] = 39,
  [40] = 31,
  [41] = 30,
  [42] = 42,
  [43] = 43,
  [44] = 26,
  [45] = 45,
  [46] = 32,
  [47] = 28,
  [48] = 14,
  [49] = 29,
  [50] = 33,
  [51] = 51,
  [52] = 51,
  [53] = 53,
  [54] = 54,
  [55] = 55,
  [56] = 56,
  [57] = 56,
  [58] = 58,
  [59] = 55,
  [60] = 60,
  [61] = 61,
  [62] = 62,
  [63] = 63,
  [64] = 64,
  [65] = 63,
  [66] = 66,
  [67] = 67,
  [68] = 66,
  [69] = 62,
  [70] = 70,
  [71] = 61,
  [72] = 67,
  [73] = 64,
  [74] = 74,
  [75] = 75,
  [76] = 76,
  [77] = 77,
  [78] = 78,
};

static TSCharacterRange sym_identifier_character_set_1[] = {
  {'$', '$'}, {'A', 'Z'}, {'\\', '\\'}, {'_', '_'}, {'a', 'z'}, {0x7f, 0x9f}, {0xa1, 0x167f}, {0x1681, 0x1fff},
  {0x200c, 0x2027}, {0x202a, 0x202e}, {0x2030, 0x205e}, {0x2061, 0x2fff}, {0x3001, 0xfefe}, {0xff00, 0x10ffff},
};

static TSCharacterRange sym_identifier_character_set_2[] = {
  {'$', '$'}, {'0', '9'}, {'A', 'Z'}, {'\\', '\\'}, {'_', '_'}, {'a', 'z'}, {0x7f, 0x9f}, {0xa1, 0x167f},
  {0x1681, 0x1fff}, {0x200c, 0x2027}, {0x202a, 0x202e}, {0x2030, 0x205e}, {0x2061, 0x2fff}, {0x3001, 0xfefe}, {0xff00, 0x10ffff},
};

static bool ts_lex(TSLexer *lexer, TSStateId state) {
  START_LEXER();
  eof = lexer->eof(lexer);
  switch (state) {
    case 0:
      if (eof) ADVANCE(39);
      ADVANCE_MAP(
        '!', 11,
        '"', 47,
        '%', 83,
        '\'', 54,
        '(', 74,
        ')', 75,
        '*', 81,
        '+', 78,
        ',', 41,
        '-', 80,
        '/', 82,
        '0', 62,
        ':', 43,
        '<', 85,
        '=', 12,
        '>', 90,
        '[', 44,
        '\\', 32,
        ']', 45,
        'a', 20,
        'f', 13,
        'i', 21,
        'n', 22,
        'o', 25,
        't', 24,
        '{', 40,
        '}', 42,
      );
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(38);
      if (('1' <= lookahead && lookahead <= '9')) ADVANCE(63);
      END_STATE();
    case 1:
      if (lookahead == '\n') SKIP(6);
      if (lookahead == '\'') ADVANCE(54);
      if (lookahead == '/') ADVANCE(55);
      if (lookahead == '\\') ADVANCE(32);
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') ADVANCE(58);
      if (lookahead != 0) ADVANCE(60);
      END_STATE();
    case 2:
      ADVANCE_MAP(
        '!', 11,
        '%', 83,
        ')', 75,
        '*', 81,
        '+', 78,
        ',', 41,
        '-', 79,
        '/', 82,
        '<', 85,
        '=', 12,
        '>', 90,
        ']', 45,
        'a', 20,
        'i', 21,
        'o', 25,
        't', 23,
      );
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(2);
      END_STATE();
    case 3:
      ADVANCE_MAP(
        '"', 47,
        '\'', 54,
        '(', 74,
        '+', 78,
        ',', 41,
        '-', 80,
        '/', 7,
        '0', 62,
        '[', 44,
        '\\', 29,
        ']', 45,
        'f', 94,
        'n', 100,
        't', 101,
        '{', 40,
      );
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(3);
      if (('1' <= lookahead && lookahead <= '9')) ADVANCE(63);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 4:
      if (lookahead == '"') ADVANCE(47);
      if (lookahead == '\'') ADVANCE(54);
      if (lookahead == '/') ADVANCE(7);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == '}') ADVANCE(42);
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(4);
      if (set_contains(sym_identifier_character_set_1, 14, lookahead)) ADVANCE(105);
      END_STATE();
    case 5:
      if (lookahead == '"') ADVANCE(47);
      if (lookahead == '/') ADVANCE(48);
      if (lookahead == '\\') ADVANCE(32);
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') ADVANCE(51);
      if (lookahead != 0) ADVANCE(53);
      END_STATE();
    case 6:
      if (lookahead == '\'') ADVANCE(54);
      if (lookahead == '/') ADVANCE(7);
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(6);
      END_STATE();
    case 7:
      if (lookahead == '*') ADVANCE(9);
      if (lookahead == '/') ADVANCE(73);
      END_STATE();
    case 8:
      if (lookahead == '*') ADVANCE(8);
      if (lookahead == '/') ADVANCE(72);
      if (lookahead != 0) ADVANCE(9);
      END_STATE();
    case 9:
      if (lookahead == '*') ADVANCE(8);
      if (lookahead != 0) ADVANCE(9);
      END_STATE();
    case 10:
      if (lookahead == '-') ADVANCE(33);
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(65);
      END_STATE();
    case 11:
      if (lookahead == '=') ADVANCE(88);
      END_STATE();
    case 12:
      if (lookahead == '=') ADVANCE(87);
      END_STATE();
    case 13:
      if (lookahead == 'a') ADVANCE(17);
      END_STATE();
    case 14:
      if (lookahead == 'd') ADVANCE(76);
      END_STATE();
    case 15:
      if (lookahead == 'e') ADVANCE(66);
      END_STATE();
    case 16:
      if (lookahead == 'e') ADVANCE(68);
      END_STATE();
    case 17:
      if (lookahead == 'l') ADVANCE(26);
      END_STATE();
    case 18:
      if (lookahead == 'l') ADVANCE(70);
      END_STATE();
    case 19:
      if (lookahead == 'l') ADVANCE(18);
      END_STATE();
    case 20:
      if (lookahead == 'n') ADVANCE(14);
      END_STATE();
    case 21:
      if (lookahead == 'n') ADVANCE(91);
      END_STATE();
    case 22:
      if (lookahead == 'o') ADVANCE(27);
      if (lookahead == 'u') ADVANCE(19);
      END_STATE();
    case 23:
      if (lookahead == 'o') ADVANCE(46);
      END_STATE();
    case 24:
      if (lookahead == 'o') ADVANCE(46);
      if (lookahead == 'r') ADVANCE(28);
      END_STATE();
    case 25:
      if (lookahead == 'r') ADVANCE(77);
      END_STATE();
    case 26:
      if (lookahead == 's') ADVANCE(16);
      END_STATE();
    case 27:
      if (lookahead == 't') ADVANCE(92);
      END_STATE();
    case 28:
      if (lookahead == 'u') ADVANCE(15);
      END_STATE();
    case 29:
      if (lookahead == 'u') ADVANCE(30);
      END_STATE();
    case 30:
      if (lookahead == '{') ADVANCE(35);
      if (('0' <= lookahead && lookahead <= '9') ||
          ('A' <= lookahead && lookahead <= 'F') ||
          ('a' <= lookahead && lookahead <= 'f')) ADVANCE(37);
      END_STATE();
    case 31:
      if (lookahead == '}') ADVANCE(105);
      if (('0' <= lookahead && lookahead <= '9') ||
          ('A' <= lookahead && lookahead <= 'F') ||
          ('a' <= lookahead && lookahead <= 'f')) ADVANCE(31);
      END_STATE();
    case 32:
      ADVANCE_MAP(
        '"', 61,
        '/', 61,
        '\\', 61,
        'b', 61,
        'f', 61,
        'n', 61,
        'r', 61,
        't', 61,
        'u', 61,
      );
      END_STATE();
    case 33:
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(65);
      END_STATE();
    case 34:
      if (('0' <= lookahead && lookahead <= '9') ||
          ('A' <= lookahead && lookahead <= 'F') ||
          ('a' <= lookahead && lookahead <= 'f')) ADVANCE(105);
      END_STATE();
    case 35:
      if (('0' <= lookahead && lookahead <= '9') ||
          ('A' <= lookahead && lookahead <= 'F') ||
          ('a' <= lookahead && lookahead <= 'f')) ADVANCE(31);
      END_STATE();
    case 36:
      if (('0' <= lookahead && lookahead <= '9') ||
          ('A' <= lookahead && lookahead <= 'F') ||
          ('a' <= lookahead && lookahead <= 'f')) ADVANCE(34);
      END_STATE();
    case 37:
      if (('0' <= lookahead && lookahead <= '9') ||
          ('A' <= lookahead && lookahead <= 'F') ||
          ('a' <= lookahead && lookahead <= 'f')) ADVANCE(36);
      END_STATE();
    case 38:
      if (eof) ADVANCE(39);
      ADVANCE_MAP(
        '!', 11,
        '"', 47,
        '%', 83,
        '\'', 54,
        '(', 74,
        ')', 75,
        '*', 81,
        '+', 78,
        ',', 41,
        '-', 80,
        '/', 82,
        '0', 62,
        ':', 43,
        '<', 85,
        '=', 12,
        '>', 90,
        '[', 44,
        ']', 45,
        'a', 20,
        'f', 13,
        'i', 21,
        'n', 22,
        'o', 25,
        't', 24,
        '{', 40,
        '}', 42,
      );
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(38);
      if (('1' <= lookahead && lookahead <= '9')) ADVANCE(63);
      END_STATE();
    case 39:
      ACCEPT_TOKEN(ts_builtin_sym_end);
      END_STATE();
    case 40:
      ACCEPT_TOKEN(anon_sym_LBRACE);
      END_STATE();
    case 41:
      ACCEPT_TOKEN(anon_sym_COMMA);
      END_STATE();
    case 42:
      ACCEPT_TOKEN(anon_sym_RBRACE);
      END_STATE();
    case 43:
      ACCEPT_TOKEN(anon_sym_COLON);
      END_STATE();
    case 44:
      ACCEPT_TOKEN(anon_sym_LBRACK);
      END_STATE();
    case 45:
      ACCEPT_TOKEN(anon_sym_RBRACK);
      END_STATE();
    case 46:
      ACCEPT_TOKEN(anon_sym_to);
      END_STATE();
    case 47:
      ACCEPT_TOKEN(anon_sym_DQUOTE);
      END_STATE();
    case 48:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '*') ADVANCE(50);
      if (lookahead == '/') ADVANCE(52);
      if (lookahead != 0 &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(53);
      END_STATE();
    case 49:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '*') ADVANCE(49);
      if (lookahead == '/') ADVANCE(53);
      if (lookahead != 0 &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(50);
      END_STATE();
    case 50:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '*') ADVANCE(49);
      if (lookahead != 0 &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(50);
      END_STATE();
    case 51:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '/') ADVANCE(48);
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') ADVANCE(51);
      if (lookahead != 0 &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(53);
      END_STATE();
    case 52:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '\n' ||
          lookahead == '\r' ||
          lookahead == 0x2028 ||
          lookahead == 0x2029) ADVANCE(53);
      if (lookahead != 0 &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(52);
      END_STATE();
    case 53:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead != 0 &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(53);
      END_STATE();
    case 54:
      ACCEPT_TOKEN(anon_sym_SQUOTE);
      END_STATE();
    case 55:
      ACCEPT_TOKEN(sym_symbol_content);
      if (lookahead == '*') ADVANCE(57);
      if (lookahead == '/') ADVANCE(59);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '\'' &&
          lookahead != '\\') ADVANCE(60);
      END_STATE();
    case 56:
      ACCEPT_TOKEN(sym_symbol_content);
      if (lookahead == '*') ADVANCE(56);
      if (lookahead == '/') ADVANCE(60);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '\'' &&
          lookahead != '\\') ADVANCE(57);
      END_STATE();
    case 57:
      ACCEPT_TOKEN(sym_symbol_content);
      if (lookahead == '*') ADVANCE(56);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '\'' &&
          lookahead != '\\') ADVANCE(57);
      END_STATE();
    case 58:
      ACCEPT_TOKEN(sym_symbol_content);
      if (lookahead == '/') ADVANCE(55);
      if (lookahead == '\t' ||
          (0x0b <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') ADVANCE(58);
      if (lookahead != 0 &&
          (lookahead < '\t' || '\r' < lookahead) &&
          lookahead != '\'' &&
          lookahead != '\\') ADVANCE(60);
      END_STATE();
    case 59:
      ACCEPT_TOKEN(sym_symbol_content);
      if (lookahead == '\r' ||
          lookahead == 0x2028 ||
          lookahead == 0x2029) ADVANCE(60);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '\'' &&
          lookahead != '\\') ADVANCE(59);
      END_STATE();
    case 60:
      ACCEPT_TOKEN(sym_symbol_content);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '\'' &&
          lookahead != '\\') ADVANCE(60);
      END_STATE();
    case 61:
      ACCEPT_TOKEN(sym_escape_sequence);
      END_STATE();
    case 62:
      ACCEPT_TOKEN(sym_number);
      if (lookahead == '.') ADVANCE(64);
      if (lookahead == 'E' ||
          lookahead == 'e') ADVANCE(10);
      END_STATE();
    case 63:
      ACCEPT_TOKEN(sym_number);
      if (lookahead == '.') ADVANCE(64);
      if (lookahead == 'E' ||
          lookahead == 'e') ADVANCE(10);
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(63);
      END_STATE();
    case 64:
      ACCEPT_TOKEN(sym_number);
      if (lookahead == 'E' ||
          lookahead == 'e') ADVANCE(10);
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(64);
      END_STATE();
    case 65:
      ACCEPT_TOKEN(sym_number);
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(65);
      END_STATE();
    case 66:
      ACCEPT_TOKEN(sym_true);
      END_STATE();
    case 67:
      ACCEPT_TOKEN(sym_true);
      if (lookahead == '\\') ADVANCE(29);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 68:
      ACCEPT_TOKEN(sym_false);
      END_STATE();
    case 69:
      ACCEPT_TOKEN(sym_false);
      if (lookahead == '\\') ADVANCE(29);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 70:
      ACCEPT_TOKEN(sym_null);
      END_STATE();
    case 71:
      ACCEPT_TOKEN(sym_null);
      if (lookahead == '\\') ADVANCE(29);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 72:
      ACCEPT_TOKEN(sym_comment);
      END_STATE();
    case 73:
      ACCEPT_TOKEN(sym_comment);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '\r' &&
          lookahead != 0x2028 &&
          lookahead != 0x2029) ADVANCE(73);
      END_STATE();
    case 74:
      ACCEPT_TOKEN(anon_sym_LPAREN);
      END_STATE();
    case 75:
      ACCEPT_TOKEN(anon_sym_RPAREN);
      END_STATE();
    case 76:
      ACCEPT_TOKEN(anon_sym_and);
      END_STATE();
    case 77:
      ACCEPT_TOKEN(anon_sym_or);
      END_STATE();
    case 78:
      ACCEPT_TOKEN(anon_sym_PLUS);
      END_STATE();
    case 79:
      ACCEPT_TOKEN(anon_sym_DASH);
      END_STATE();
    case 80:
      ACCEPT_TOKEN(anon_sym_DASH);
      if (lookahead == '0') ADVANCE(62);
      if (('1' <= lookahead && lookahead <= '9')) ADVANCE(63);
      END_STATE();
    case 81:
      ACCEPT_TOKEN(anon_sym_STAR);
      if (lookahead == '*') ADVANCE(84);
      END_STATE();
    case 82:
      ACCEPT_TOKEN(anon_sym_SLASH);
      if (lookahead == '*') ADVANCE(9);
      if (lookahead == '/') ADVANCE(73);
      END_STATE();
    case 83:
      ACCEPT_TOKEN(anon_sym_PERCENT);
      END_STATE();
    case 84:
      ACCEPT_TOKEN(anon_sym_STAR_STAR);
      END_STATE();
    case 85:
      ACCEPT_TOKEN(anon_sym_LT);
      if (lookahead == '=') ADVANCE(86);
      END_STATE();
    case 86:
      ACCEPT_TOKEN(anon_sym_LT_EQ);
      END_STATE();
    case 87:
      ACCEPT_TOKEN(anon_sym_EQ_EQ);
      END_STATE();
    case 88:
      ACCEPT_TOKEN(anon_sym_BANG_EQ);
      END_STATE();
    case 89:
      ACCEPT_TOKEN(anon_sym_GT_EQ);
      END_STATE();
    case 90:
      ACCEPT_TOKEN(anon_sym_GT);
      if (lookahead == '=') ADVANCE(89);
      END_STATE();
    case 91:
      ACCEPT_TOKEN(anon_sym_in);
      END_STATE();
    case 92:
      ACCEPT_TOKEN(anon_sym_not);
      END_STATE();
    case 93:
      ACCEPT_TOKEN(anon_sym_not);
      if (lookahead == '\\') ADVANCE(29);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 94:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'a') ADVANCE(97);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 95:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'e') ADVANCE(67);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 96:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'e') ADVANCE(69);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 97:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'l') ADVANCE(102);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 98:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'l') ADVANCE(71);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 99:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'l') ADVANCE(98);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 100:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'o') ADVANCE(103);
      if (lookahead == 'u') ADVANCE(99);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 101:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'r') ADVANCE(104);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 102:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 's') ADVANCE(96);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 103:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 't') ADVANCE(93);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 104:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (lookahead == 'u') ADVANCE(95);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    case 105:
      ACCEPT_TOKEN(sym_identifier);
      if (lookahead == '\\') ADVANCE(29);
      if (set_contains(sym_identifier_character_set_2, 15, lookahead)) ADVANCE(105);
      END_STATE();
    default:
      return false;
  }
}

static const TSLexMode ts_lex_modes[STATE_COUNT] = {
  [0] = {.lex_state = 0},
  [1] = {.lex_state = 0},
  [2] = {.lex_state = 3},
  [3] = {.lex_state = 3},
  [4] = {.lex_state = 3},
  [5] = {.lex_state = 3},
  [6] = {.lex_state = 3},
  [7] = {.lex_state = 3},
  [8] = {.lex_state = 3},
  [9] = {.lex_state = 3},
  [10] = {.lex_state = 3},
  [11] = {.lex_state = 3},
  [12] = {.lex_state = 3},
  [13] = {.lex_state = 3},
  [14] = {.lex_state = 2},
  [15] = {.lex_state = 2},
  [16] = {.lex_state = 2},
  [17] = {.lex_state = 2},
  [18] = {.lex_state = 2},
  [19] = {.lex_state = 2},
  [20] = {.lex_state = 2},
  [21] = {.lex_state = 2},
  [22] = {.lex_state = 2},
  [23] = {.lex_state = 2},
  [24] = {.lex_state = 2},
  [25] = {.lex_state = 2},
  [26] = {.lex_state = 2},
  [27] = {.lex_state = 2},
  [28] = {.lex_state = 2},
  [29] = {.lex_state = 2},
  [30] = {.lex_state = 2},
  [31] = {.lex_state = 2},
  [32] = {.lex_state = 2},
  [33] = {.lex_state = 2},
  [34] = {.lex_state = 2},
  [35] = {.lex_state = 2},
  [36] = {.lex_state = 2},
  [37] = {.lex_state = 0},
  [38] = {.lex_state = 0},
  [39] = {.lex_state = 0},
  [40] = {.lex_state = 0},
  [41] = {.lex_state = 0},
  [42] = {.lex_state = 0},
  [43] = {.lex_state = 0},
  [44] = {.lex_state = 0},
  [45] = {.lex_state = 0},
  [46] = {.lex_state = 0},
  [47] = {.lex_state = 0},
  [48] = {.lex_state = 0},
  [49] = {.lex_state = 0},
  [50] = {.lex_state = 0},
  [51] = {.lex_state = 4},
  [52] = {.lex_state = 4},
  [53] = {.lex_state = 4},
  [54] = {.lex_state = 1},
  [55] = {.lex_state = 5},
  [56] = {.lex_state = 1},
  [57] = {.lex_state = 1},
  [58] = {.lex_state = 5},
  [59] = {.lex_state = 5},
  [60] = {.lex_state = 0},
  [61] = {.lex_state = 0},
  [62] = {.lex_state = 0},
  [63] = {.lex_state = 5},
  [64] = {.lex_state = 1},
  [65] = {.lex_state = 5},
  [66] = {.lex_state = 0},
  [67] = {.lex_state = 0},
  [68] = {.lex_state = 0},
  [69] = {.lex_state = 0},
  [70] = {.lex_state = 0},
  [71] = {.lex_state = 0},
  [72] = {.lex_state = 0},
  [73] = {.lex_state = 1},
  [74] = {.lex_state = 0},
  [75] = {.lex_state = 0},
  [76] = {.lex_state = 0},
  [77] = {.lex_state = 0},
  [78] = {.lex_state = 0},
};

static const uint16_t ts_parse_table[LARGE_STATE_COUNT][SYMBOL_COUNT] = {
  [0] = {
    [ts_builtin_sym_end] = ACTIONS(1),
    [anon_sym_LBRACE] = ACTIONS(1),
    [anon_sym_COMMA] = ACTIONS(1),
    [anon_sym_RBRACE] = ACTIONS(1),
    [anon_sym_COLON] = ACTIONS(1),
    [anon_sym_LBRACK] = ACTIONS(1),
    [anon_sym_RBRACK] = ACTIONS(1),
    [anon_sym_to] = ACTIONS(1),
    [anon_sym_DQUOTE] = ACTIONS(1),
    [anon_sym_SQUOTE] = ACTIONS(1),
    [sym_escape_sequence] = ACTIONS(1),
    [sym_number] = ACTIONS(1),
    [sym_true] = ACTIONS(1),
    [sym_false] = ACTIONS(1),
    [sym_null] = ACTIONS(1),
    [sym_comment] = ACTIONS(3),
    [anon_sym_LPAREN] = ACTIONS(1),
    [anon_sym_RPAREN] = ACTIONS(1),
    [anon_sym_and] = ACTIONS(1),
    [anon_sym_or] = ACTIONS(1),
    [anon_sym_PLUS] = ACTIONS(1),
    [anon_sym_DASH] = ACTIONS(1),
    [anon_sym_STAR] = ACTIONS(1),
    [anon_sym_SLASH] = ACTIONS(1),
    [anon_sym_PERCENT] = ACTIONS(1),
    [anon_sym_STAR_STAR] = ACTIONS(1),
    [anon_sym_LT] = ACTIONS(1),
    [anon_sym_LT_EQ] = ACTIONS(1),
    [anon_sym_EQ_EQ] = ACTIONS(1),
    [anon_sym_BANG_EQ] = ACTIONS(1),
    [anon_sym_GT_EQ] = ACTIONS(1),
    [anon_sym_GT] = ACTIONS(1),
    [anon_sym_in] = ACTIONS(1),
    [anon_sym_not] = ACTIONS(1),
  },
  [1] = {
    [sym_document] = STATE(77),
    [sym__value] = STATE(37),
    [sym_object] = STATE(43),
    [sym_array] = STATE(43),
    [sym_range] = STATE(43),
    [sym_string] = STATE(43),
    [sym_symbol] = STATE(43),
    [aux_sym_document_repeat1] = STATE(37),
    [ts_builtin_sym_end] = ACTIONS(5),
    [anon_sym_LBRACE] = ACTIONS(7),
    [anon_sym_LBRACK] = ACTIONS(9),
    [anon_sym_DQUOTE] = ACTIONS(11),
    [anon_sym_SQUOTE] = ACTIONS(13),
    [sym_number] = ACTIONS(15),
    [sym_true] = ACTIONS(17),
    [sym_false] = ACTIONS(17),
    [sym_null] = ACTIONS(17),
    [sym_comment] = ACTIONS(3),
  },
};

static const uint16_t ts_small_parse_table[] = {
  [0] = 16,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(21), 1,
      anon_sym_COMMA,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(25), 1,
      anon_sym_RBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(16), 1,
      sym_expression,
    STATE(62), 1,
      aux_sym_array_repeat1,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [60] = 16,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(21), 1,
      anon_sym_COMMA,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    ACTIONS(41), 1,
      anon_sym_RBRACK,
    STATE(34), 1,
      sym_expression,
    STATE(69), 1,
      aux_sym_array_repeat1,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [120] = 14,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(35), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    ACTIONS(43), 2,
      anon_sym_COMMA,
      anon_sym_RBRACK,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [175] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(23), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [226] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(36), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [277] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(15), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [328] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(20), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [379] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(18), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [430] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(21), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [481] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(22), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [532] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(24), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [583] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(19), 1,
      anon_sym_LBRACE,
    ACTIONS(23), 1,
      anon_sym_LBRACK,
    ACTIONS(27), 1,
      anon_sym_DQUOTE,
    ACTIONS(29), 1,
      anon_sym_SQUOTE,
    ACTIONS(31), 1,
      sym_number,
    ACTIONS(35), 1,
      anon_sym_LPAREN,
    ACTIONS(37), 1,
      anon_sym_PLUS,
    STATE(25), 1,
      sym_expression,
    ACTIONS(39), 2,
      anon_sym_DASH,
      anon_sym_not,
    STATE(17), 3,
      sym_primary_expression,
      sym_binary_expression,
      sym_unary_expression,
    ACTIONS(33), 4,
      sym_true,
      sym_false,
      sym_null,
      sym_identifier,
    STATE(27), 6,
      sym_object,
      sym_array,
      sym_string,
      sym_symbol,
      sym_parenthesized_expression,
      sym__identifier,
  [634] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(47), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(45), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [661] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(51), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(49), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [688] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(21), 1,
      anon_sym_COMMA,
    ACTIONS(53), 1,
      anon_sym_RBRACK,
    ACTIONS(57), 1,
      anon_sym_and,
    ACTIONS(59), 1,
      anon_sym_or,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    STATE(67), 1,
      aux_sym_array_repeat1,
    ACTIONS(61), 2,
      anon_sym_PLUS,
      anon_sym_DASH,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(69), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(71), 2,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
    ACTIONS(55), 4,
      anon_sym_to,
      anon_sym_LT_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [735] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(75), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(73), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [762] = 9,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(61), 2,
      anon_sym_PLUS,
      anon_sym_DASH,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(69), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(71), 2,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
    ACTIONS(55), 4,
      anon_sym_to,
      anon_sym_LT_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
    ACTIONS(77), 5,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
  [801] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(81), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(79), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [828] = 7,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(61), 2,
      anon_sym_PLUS,
      anon_sym_DASH,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(83), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(77), 11,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [863] = 10,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(57), 1,
      anon_sym_and,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(61), 2,
      anon_sym_PLUS,
      anon_sym_DASH,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(69), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(71), 2,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
    ACTIONS(55), 4,
      anon_sym_to,
      anon_sym_LT_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
    ACTIONS(77), 4,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_RPAREN,
      anon_sym_or,
  [904] = 6,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(83), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(77), 13,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [937] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(83), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(77), 14,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [966] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(83), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(77), 14,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [995] = 8,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(61), 2,
      anon_sym_PLUS,
      anon_sym_DASH,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(69), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(55), 4,
      anon_sym_to,
      anon_sym_LT_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
    ACTIONS(77), 7,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
  [1032] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(87), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(85), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1059] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(91), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(89), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1086] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(95), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(93), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1113] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(99), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(97), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1140] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(103), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(101), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1167] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(107), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(105), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1194] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(111), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(109), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1221] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(115), 4,
      anon_sym_STAR,
      anon_sym_SLASH,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(113), 15,
      anon_sym_COMMA,
      anon_sym_RBRACK,
      anon_sym_to,
      anon_sym_RPAREN,
      anon_sym_and,
      anon_sym_or,
      anon_sym_PLUS,
      anon_sym_DASH,
      anon_sym_PERCENT,
      anon_sym_STAR_STAR,
      anon_sym_LT_EQ,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1248] = 13,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(21), 1,
      anon_sym_COMMA,
    ACTIONS(57), 1,
      anon_sym_and,
    ACTIONS(59), 1,
      anon_sym_or,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(117), 1,
      anon_sym_RBRACK,
    STATE(72), 1,
      aux_sym_array_repeat1,
    ACTIONS(61), 2,
      anon_sym_PLUS,
      anon_sym_DASH,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(69), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(71), 2,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
    ACTIONS(55), 4,
      anon_sym_to,
      anon_sym_LT_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1295] = 11,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(57), 1,
      anon_sym_and,
    ACTIONS(59), 1,
      anon_sym_or,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(61), 2,
      anon_sym_PLUS,
      anon_sym_DASH,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(69), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(71), 2,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
    ACTIONS(119), 2,
      anon_sym_COMMA,
      anon_sym_RBRACK,
    ACTIONS(55), 4,
      anon_sym_to,
      anon_sym_LT_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1337] = 11,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(57), 1,
      anon_sym_and,
    ACTIONS(59), 1,
      anon_sym_or,
    ACTIONS(65), 1,
      anon_sym_PERCENT,
    ACTIONS(67), 1,
      anon_sym_STAR_STAR,
    ACTIONS(121), 1,
      anon_sym_RPAREN,
    ACTIONS(61), 2,
      anon_sym_PLUS,
      anon_sym_DASH,
    ACTIONS(63), 2,
      anon_sym_STAR,
      anon_sym_SLASH,
    ACTIONS(69), 2,
      anon_sym_LT,
      anon_sym_GT,
    ACTIONS(71), 2,
      anon_sym_EQ_EQ,
      anon_sym_BANG_EQ,
    ACTIONS(55), 4,
      anon_sym_to,
      anon_sym_LT_EQ,
      anon_sym_GT_EQ,
      anon_sym_in,
  [1378] = 10,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(7), 1,
      anon_sym_LBRACE,
    ACTIONS(9), 1,
      anon_sym_LBRACK,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    ACTIONS(13), 1,
      anon_sym_SQUOTE,
    ACTIONS(15), 1,
      sym_number,
    ACTIONS(123), 1,
      ts_builtin_sym_end,
    STATE(38), 2,
      sym__value,
      aux_sym_document_repeat1,
    ACTIONS(17), 3,
      sym_true,
      sym_false,
      sym_null,
    STATE(43), 5,
      sym_object,
      sym_array,
      sym_range,
      sym_string,
      sym_symbol,
  [1416] = 10,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(125), 1,
      ts_builtin_sym_end,
    ACTIONS(127), 1,
      anon_sym_LBRACE,
    ACTIONS(130), 1,
      anon_sym_LBRACK,
    ACTIONS(133), 1,
      anon_sym_DQUOTE,
    ACTIONS(136), 1,
      anon_sym_SQUOTE,
    ACTIONS(139), 1,
      sym_number,
    STATE(38), 2,
      sym__value,
      aux_sym_document_repeat1,
    ACTIONS(142), 3,
      sym_true,
      sym_false,
      sym_null,
    STATE(43), 5,
      sym_object,
      sym_array,
      sym_range,
      sym_string,
      sym_symbol,
  [1454] = 9,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(7), 1,
      anon_sym_LBRACE,
    ACTIONS(9), 1,
      anon_sym_LBRACK,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    ACTIONS(13), 1,
      anon_sym_SQUOTE,
    ACTIONS(15), 1,
      sym_number,
    STATE(75), 1,
      sym__value,
    ACTIONS(17), 3,
      sym_true,
      sym_false,
      sym_null,
    STATE(43), 5,
      sym_object,
      sym_array,
      sym_range,
      sym_string,
      sym_symbol,
  [1488] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(105), 12,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_COLON,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1506] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(101), 12,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_COLON,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1524] = 3,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(147), 1,
      anon_sym_to,
    ACTIONS(145), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1544] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(145), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1561] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(85), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1578] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(149), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1595] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(109), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1612] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(93), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1629] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(45), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1646] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(97), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1663] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(113), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_DQUOTE,
      anon_sym_SQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [1680] = 7,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    ACTIONS(13), 1,
      anon_sym_SQUOTE,
    ACTIONS(151), 1,
      anon_sym_RBRACE,
    ACTIONS(153), 1,
      sym_identifier,
    STATE(68), 1,
      sym_pair,
    STATE(76), 2,
      sym_string,
      sym_symbol,
  [1703] = 7,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    ACTIONS(13), 1,
      anon_sym_SQUOTE,
    ACTIONS(153), 1,
      sym_identifier,
    ACTIONS(155), 1,
      anon_sym_RBRACE,
    STATE(66), 1,
      sym_pair,
    STATE(76), 2,
      sym_string,
      sym_symbol,
  [1726] = 6,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    ACTIONS(13), 1,
      anon_sym_SQUOTE,
    ACTIONS(153), 1,
      sym_identifier,
    STATE(74), 1,
      sym_pair,
    STATE(76), 2,
      sym_string,
      sym_symbol,
  [1746] = 4,
    ACTIONS(157), 1,
      anon_sym_SQUOTE,
    ACTIONS(162), 1,
      sym_comment,
    STATE(54), 1,
      aux_sym__symbol_content,
    ACTIONS(159), 2,
      sym_symbol_content,
      sym_escape_sequence,
  [1760] = 4,
    ACTIONS(162), 1,
      sym_comment,
    ACTIONS(164), 1,
      anon_sym_DQUOTE,
    STATE(58), 1,
      aux_sym__string_content,
    ACTIONS(166), 2,
      sym_string_content,
      sym_escape_sequence,
  [1774] = 4,
    ACTIONS(162), 1,
      sym_comment,
    ACTIONS(168), 1,
      anon_sym_SQUOTE,
    STATE(54), 1,
      aux_sym__symbol_content,
    ACTIONS(170), 2,
      sym_symbol_content,
      sym_escape_sequence,
  [1788] = 4,
    ACTIONS(162), 1,
      sym_comment,
    ACTIONS(172), 1,
      anon_sym_SQUOTE,
    STATE(54), 1,
      aux_sym__symbol_content,
    ACTIONS(170), 2,
      sym_symbol_content,
      sym_escape_sequence,
  [1802] = 4,
    ACTIONS(162), 1,
      sym_comment,
    ACTIONS(174), 1,
      anon_sym_DQUOTE,
    STATE(58), 1,
      aux_sym__string_content,
    ACTIONS(176), 2,
      sym_string_content,
      sym_escape_sequence,
  [1816] = 4,
    ACTIONS(162), 1,
      sym_comment,
    ACTIONS(179), 1,
      anon_sym_DQUOTE,
    STATE(58), 1,
      aux_sym__string_content,
    ACTIONS(166), 2,
      sym_string_content,
      sym_escape_sequence,
  [1830] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(181), 1,
      anon_sym_COMMA,
    ACTIONS(184), 1,
      anon_sym_RBRACE,
    STATE(60), 1,
      aux_sym_object_repeat1,
  [1843] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(186), 1,
      anon_sym_COMMA,
    ACTIONS(188), 1,
      anon_sym_RBRACE,
    STATE(60), 1,
      aux_sym_object_repeat1,
  [1856] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(21), 1,
      anon_sym_COMMA,
    ACTIONS(53), 1,
      anon_sym_RBRACK,
    STATE(70), 1,
      aux_sym_array_repeat1,
  [1869] = 3,
    ACTIONS(162), 1,
      sym_comment,
    STATE(55), 1,
      aux_sym__string_content,
    ACTIONS(190), 2,
      sym_string_content,
      sym_escape_sequence,
  [1880] = 3,
    ACTIONS(162), 1,
      sym_comment,
    STATE(56), 1,
      aux_sym__symbol_content,
    ACTIONS(192), 2,
      sym_symbol_content,
      sym_escape_sequence,
  [1891] = 3,
    ACTIONS(162), 1,
      sym_comment,
    STATE(59), 1,
      aux_sym__string_content,
    ACTIONS(194), 2,
      sym_string_content,
      sym_escape_sequence,
  [1902] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(186), 1,
      anon_sym_COMMA,
    ACTIONS(196), 1,
      anon_sym_RBRACE,
    STATE(71), 1,
      aux_sym_object_repeat1,
  [1915] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(21), 1,
      anon_sym_COMMA,
    ACTIONS(198), 1,
      anon_sym_RBRACK,
    STATE(70), 1,
      aux_sym_array_repeat1,
  [1928] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(186), 1,
      anon_sym_COMMA,
    ACTIONS(200), 1,
      anon_sym_RBRACE,
    STATE(61), 1,
      aux_sym_object_repeat1,
  [1941] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(21), 1,
      anon_sym_COMMA,
    ACTIONS(117), 1,
      anon_sym_RBRACK,
    STATE(70), 1,
      aux_sym_array_repeat1,
  [1954] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(119), 1,
      anon_sym_RBRACK,
    ACTIONS(202), 1,
      anon_sym_COMMA,
    STATE(70), 1,
      aux_sym_array_repeat1,
  [1967] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(186), 1,
      anon_sym_COMMA,
    ACTIONS(205), 1,
      anon_sym_RBRACE,
    STATE(60), 1,
      aux_sym_object_repeat1,
  [1980] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(21), 1,
      anon_sym_COMMA,
    ACTIONS(207), 1,
      anon_sym_RBRACK,
    STATE(70), 1,
      aux_sym_array_repeat1,
  [1993] = 3,
    ACTIONS(162), 1,
      sym_comment,
    STATE(57), 1,
      aux_sym__symbol_content,
    ACTIONS(209), 2,
      sym_symbol_content,
      sym_escape_sequence,
  [2004] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(184), 2,
      anon_sym_COMMA,
      anon_sym_RBRACE,
  [2012] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(211), 2,
      anon_sym_COMMA,
      anon_sym_RBRACE,
  [2020] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(213), 1,
      anon_sym_COLON,
  [2027] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(215), 1,
      ts_builtin_sym_end,
  [2034] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(217), 1,
      sym_number,
};

static const uint32_t ts_small_parse_table_map[] = {
  [SMALL_STATE(2)] = 0,
  [SMALL_STATE(3)] = 60,
  [SMALL_STATE(4)] = 120,
  [SMALL_STATE(5)] = 175,
  [SMALL_STATE(6)] = 226,
  [SMALL_STATE(7)] = 277,
  [SMALL_STATE(8)] = 328,
  [SMALL_STATE(9)] = 379,
  [SMALL_STATE(10)] = 430,
  [SMALL_STATE(11)] = 481,
  [SMALL_STATE(12)] = 532,
  [SMALL_STATE(13)] = 583,
  [SMALL_STATE(14)] = 634,
  [SMALL_STATE(15)] = 661,
  [SMALL_STATE(16)] = 688,
  [SMALL_STATE(17)] = 735,
  [SMALL_STATE(18)] = 762,
  [SMALL_STATE(19)] = 801,
  [SMALL_STATE(20)] = 828,
  [SMALL_STATE(21)] = 863,
  [SMALL_STATE(22)] = 904,
  [SMALL_STATE(23)] = 937,
  [SMALL_STATE(24)] = 966,
  [SMALL_STATE(25)] = 995,
  [SMALL_STATE(26)] = 1032,
  [SMALL_STATE(27)] = 1059,
  [SMALL_STATE(28)] = 1086,
  [SMALL_STATE(29)] = 1113,
  [SMALL_STATE(30)] = 1140,
  [SMALL_STATE(31)] = 1167,
  [SMALL_STATE(32)] = 1194,
  [SMALL_STATE(33)] = 1221,
  [SMALL_STATE(34)] = 1248,
  [SMALL_STATE(35)] = 1295,
  [SMALL_STATE(36)] = 1337,
  [SMALL_STATE(37)] = 1378,
  [SMALL_STATE(38)] = 1416,
  [SMALL_STATE(39)] = 1454,
  [SMALL_STATE(40)] = 1488,
  [SMALL_STATE(41)] = 1506,
  [SMALL_STATE(42)] = 1524,
  [SMALL_STATE(43)] = 1544,
  [SMALL_STATE(44)] = 1561,
  [SMALL_STATE(45)] = 1578,
  [SMALL_STATE(46)] = 1595,
  [SMALL_STATE(47)] = 1612,
  [SMALL_STATE(48)] = 1629,
  [SMALL_STATE(49)] = 1646,
  [SMALL_STATE(50)] = 1663,
  [SMALL_STATE(51)] = 1680,
  [SMALL_STATE(52)] = 1703,
  [SMALL_STATE(53)] = 1726,
  [SMALL_STATE(54)] = 1746,
  [SMALL_STATE(55)] = 1760,
  [SMALL_STATE(56)] = 1774,
  [SMALL_STATE(57)] = 1788,
  [SMALL_STATE(58)] = 1802,
  [SMALL_STATE(59)] = 1816,
  [SMALL_STATE(60)] = 1830,
  [SMALL_STATE(61)] = 1843,
  [SMALL_STATE(62)] = 1856,
  [SMALL_STATE(63)] = 1869,
  [SMALL_STATE(64)] = 1880,
  [SMALL_STATE(65)] = 1891,
  [SMALL_STATE(66)] = 1902,
  [SMALL_STATE(67)] = 1915,
  [SMALL_STATE(68)] = 1928,
  [SMALL_STATE(69)] = 1941,
  [SMALL_STATE(70)] = 1954,
  [SMALL_STATE(71)] = 1967,
  [SMALL_STATE(72)] = 1980,
  [SMALL_STATE(73)] = 1993,
  [SMALL_STATE(74)] = 2004,
  [SMALL_STATE(75)] = 2012,
  [SMALL_STATE(76)] = 2020,
  [SMALL_STATE(77)] = 2027,
  [SMALL_STATE(78)] = 2034,
};

static const TSParseActionEntry ts_parse_actions[] = {
  [0] = {.entry = {.count = 0, .reusable = false}},
  [1] = {.entry = {.count = 1, .reusable = false}}, RECOVER(),
  [3] = {.entry = {.count = 1, .reusable = true}}, SHIFT_EXTRA(),
  [5] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_document, 0, 0, 0),
  [7] = {.entry = {.count = 1, .reusable = true}}, SHIFT(51),
  [9] = {.entry = {.count = 1, .reusable = true}}, SHIFT(2),
  [11] = {.entry = {.count = 1, .reusable = true}}, SHIFT(63),
  [13] = {.entry = {.count = 1, .reusable = true}}, SHIFT(64),
  [15] = {.entry = {.count = 1, .reusable = true}}, SHIFT(42),
  [17] = {.entry = {.count = 1, .reusable = true}}, SHIFT(43),
  [19] = {.entry = {.count = 1, .reusable = true}}, SHIFT(52),
  [21] = {.entry = {.count = 1, .reusable = true}}, SHIFT(4),
  [23] = {.entry = {.count = 1, .reusable = true}}, SHIFT(3),
  [25] = {.entry = {.count = 1, .reusable = true}}, SHIFT(48),
  [27] = {.entry = {.count = 1, .reusable = true}}, SHIFT(65),
  [29] = {.entry = {.count = 1, .reusable = true}}, SHIFT(73),
  [31] = {.entry = {.count = 1, .reusable = true}}, SHIFT(27),
  [33] = {.entry = {.count = 1, .reusable = false}}, SHIFT(27),
  [35] = {.entry = {.count = 1, .reusable = true}}, SHIFT(6),
  [37] = {.entry = {.count = 1, .reusable = true}}, SHIFT(7),
  [39] = {.entry = {.count = 1, .reusable = false}}, SHIFT(7),
  [41] = {.entry = {.count = 1, .reusable = true}}, SHIFT(14),
  [43] = {.entry = {.count = 1, .reusable = true}}, REDUCE(aux_sym_array_repeat1, 1, 0, 0),
  [45] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_array, 2, 0, 0),
  [47] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_array, 2, 0, 0),
  [49] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_unary_expression, 2, 0, 1),
  [51] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_unary_expression, 2, 0, 1),
  [53] = {.entry = {.count = 1, .reusable = true}}, SHIFT(49),
  [55] = {.entry = {.count = 1, .reusable = true}}, SHIFT(8),
  [57] = {.entry = {.count = 1, .reusable = true}}, SHIFT(9),
  [59] = {.entry = {.count = 1, .reusable = true}}, SHIFT(10),
  [61] = {.entry = {.count = 1, .reusable = true}}, SHIFT(11),
  [63] = {.entry = {.count = 1, .reusable = false}}, SHIFT(5),
  [65] = {.entry = {.count = 1, .reusable = true}}, SHIFT(5),
  [67] = {.entry = {.count = 1, .reusable = true}}, SHIFT(12),
  [69] = {.entry = {.count = 1, .reusable = false}}, SHIFT(8),
  [71] = {.entry = {.count = 1, .reusable = true}}, SHIFT(13),
  [73] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_expression, 1, 0, 0),
  [75] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_expression, 1, 0, 0),
  [77] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_binary_expression, 3, 0, 3),
  [79] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_parenthesized_expression, 3, 0, 0),
  [81] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_parenthesized_expression, 3, 0, 0),
  [83] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_binary_expression, 3, 0, 3),
  [85] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_object, 2, 0, 0),
  [87] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_object, 2, 0, 0),
  [89] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_primary_expression, 1, 0, 0),
  [91] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_primary_expression, 1, 0, 0),
  [93] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_object, 3, 0, 0),
  [95] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_object, 3, 0, 0),
  [97] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_array, 3, 0, 0),
  [99] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_array, 3, 0, 0),
  [101] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_string, 3, 0, 0),
  [103] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_string, 3, 0, 0),
  [105] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_symbol, 3, 0, 0),
  [107] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_symbol, 3, 0, 0),
  [109] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_object, 4, 0, 0),
  [111] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_object, 4, 0, 0),
  [113] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_array, 4, 0, 0),
  [115] = {.entry = {.count = 1, .reusable = false}}, REDUCE(sym_array, 4, 0, 0),
  [117] = {.entry = {.count = 1, .reusable = true}}, SHIFT(29),
  [119] = {.entry = {.count = 1, .reusable = true}}, REDUCE(aux_sym_array_repeat1, 2, 0, 0),
  [121] = {.entry = {.count = 1, .reusable = true}}, SHIFT(19),
  [123] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_document, 1, 0, 0),
  [125] = {.entry = {.count = 1, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0),
  [127] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(51),
  [130] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(2),
  [133] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(63),
  [136] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(64),
  [139] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(42),
  [142] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(43),
  [145] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym__value, 1, 0, 0),
  [147] = {.entry = {.count = 1, .reusable = true}}, SHIFT(78),
  [149] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_range, 3, 0, 0),
  [151] = {.entry = {.count = 1, .reusable = true}}, SHIFT(44),
  [153] = {.entry = {.count = 1, .reusable = true}}, SHIFT(76),
  [155] = {.entry = {.count = 1, .reusable = true}}, SHIFT(26),
  [157] = {.entry = {.count = 1, .reusable = false}}, REDUCE(aux_sym__symbol_content, 2, 0, 0),
  [159] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym__symbol_content, 2, 0, 0), SHIFT_REPEAT(54),
  [162] = {.entry = {.count = 1, .reusable = false}}, SHIFT_EXTRA(),
  [164] = {.entry = {.count = 1, .reusable = false}}, SHIFT(41),
  [166] = {.entry = {.count = 1, .reusable = true}}, SHIFT(58),
  [168] = {.entry = {.count = 1, .reusable = false}}, SHIFT(40),
  [170] = {.entry = {.count = 1, .reusable = true}}, SHIFT(54),
  [172] = {.entry = {.count = 1, .reusable = false}}, SHIFT(31),
  [174] = {.entry = {.count = 1, .reusable = false}}, REDUCE(aux_sym__string_content, 2, 0, 0),
  [176] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym__string_content, 2, 0, 0), SHIFT_REPEAT(58),
  [179] = {.entry = {.count = 1, .reusable = false}}, SHIFT(30),
  [181] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_object_repeat1, 2, 0, 0), SHIFT_REPEAT(53),
  [184] = {.entry = {.count = 1, .reusable = true}}, REDUCE(aux_sym_object_repeat1, 2, 0, 0),
  [186] = {.entry = {.count = 1, .reusable = true}}, SHIFT(53),
  [188] = {.entry = {.count = 1, .reusable = true}}, SHIFT(46),
  [190] = {.entry = {.count = 1, .reusable = true}}, SHIFT(55),
  [192] = {.entry = {.count = 1, .reusable = true}}, SHIFT(56),
  [194] = {.entry = {.count = 1, .reusable = true}}, SHIFT(59),
  [196] = {.entry = {.count = 1, .reusable = true}}, SHIFT(28),
  [198] = {.entry = {.count = 1, .reusable = true}}, SHIFT(50),
  [200] = {.entry = {.count = 1, .reusable = true}}, SHIFT(47),
  [202] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_array_repeat1, 2, 0, 0), SHIFT_REPEAT(4),
  [205] = {.entry = {.count = 1, .reusable = true}}, SHIFT(32),
  [207] = {.entry = {.count = 1, .reusable = true}}, SHIFT(33),
  [209] = {.entry = {.count = 1, .reusable = true}}, SHIFT(57),
  [211] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_pair, 3, 0, 2),
  [213] = {.entry = {.count = 1, .reusable = true}}, SHIFT(39),
  [215] = {.entry = {.count = 1, .reusable = true}},  ACCEPT_INPUT(),
  [217] = {.entry = {.count = 1, .reusable = true}}, SHIFT(45),
};

#ifdef __cplusplus
extern "C" {
#endif
#ifdef TREE_SITTER_HIDE_SYMBOLS
#define TS_PUBLIC
#elif defined(_WIN32)
#define TS_PUBLIC __declspec(dllexport)
#else
#define TS_PUBLIC __attribute__((visibility("default")))
#endif

TS_PUBLIC const TSLanguage *tree_sitter_lambda(void) {
  static const TSLanguage language = {
    .version = LANGUAGE_VERSION,
    .symbol_count = SYMBOL_COUNT,
    .alias_count = ALIAS_COUNT,
    .token_count = TOKEN_COUNT,
    .external_token_count = EXTERNAL_TOKEN_COUNT,
    .state_count = STATE_COUNT,
    .large_state_count = LARGE_STATE_COUNT,
    .production_id_count = PRODUCTION_ID_COUNT,
    .field_count = FIELD_COUNT,
    .max_alias_sequence_length = MAX_ALIAS_SEQUENCE_LENGTH,
    .parse_table = &ts_parse_table[0][0],
    .small_parse_table = ts_small_parse_table,
    .small_parse_table_map = ts_small_parse_table_map,
    .parse_actions = ts_parse_actions,
    .symbol_names = ts_symbol_names,
    .field_names = ts_field_names,
    .field_map_slices = ts_field_map_slices,
    .field_map_entries = ts_field_map_entries,
    .symbol_metadata = ts_symbol_metadata,
    .public_symbol_map = ts_symbol_map,
    .alias_map = ts_non_terminal_alias_map,
    .alias_sequences = &ts_alias_sequences[0][0],
    .lex_modes = ts_lex_modes,
    .lex_fn = ts_lex,
    .primary_state_ids = ts_primary_state_ids,
  };
  return &language;
}
#ifdef __cplusplus
}
#endif
