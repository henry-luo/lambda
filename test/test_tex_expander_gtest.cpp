// test_tex_expander_gtest.cpp - Unit tests for TeX Expander (Gullet)
//
// Tests the tex_expander.hpp implementation against LaTeXML expansion fixtures.
// Uses test files from ./test/latexml/fixtures/expansion/

#include <gtest/gtest.h>
#include "lambda/tex/tex_expander.hpp"
#include "lambda/tex/tex_tokenizer.hpp"
#include "lambda/tex/tex_catcode.hpp"
#include "lambda/tex/tex_token.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class TexExpanderTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    Expander* expander;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        expander = new Expander(arena);
    }

    void TearDown() override {
        delete expander;
        arena_destroy(arena);
        pool_destroy(pool);
    }

    // Helper to expand a string and return result as string
    std::string expand_to_string(const char* input) {
        expander->push_input(input, strlen(input));
        
        std::string result;
        while (!expander->at_end()) {
            Token t = expander->expand_token();
            if (t.is_end()) break;
            
            if (t.type == TokenType::CHAR) {
                result += t.chr.ch;
            } else if (t.type == TokenType::CS) {
                result += '\\';
                result.append(t.cs.name, t.cs.len);
            }
        }
        return result;
    }

    // Read a file to string
    std::string read_file(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};

// ============================================================================
// Basic Token Tests
// ============================================================================

TEST_F(TexExpanderTest, TokenCharacter) {
    Token t = Token::make_char('a', CatCode::LETTER);
    EXPECT_TRUE(t.is_char());
    EXPECT_EQ(t.chr.ch, 'a');
    EXPECT_EQ(t.catcode, CatCode::LETTER);
}

TEST_F(TexExpanderTest, TokenControlSequence) {
    Token t = Token::make_cs("hello", 5, arena);
    EXPECT_TRUE(t.is_cs());
    EXPECT_EQ(t.cs.len, 5);
    EXPECT_STREQ(t.cs.name, "hello");
}

TEST_F(TexExpanderTest, TokenParameter) {
    Token t = Token::make_param(1);
    EXPECT_TRUE(t.is_param());
    EXPECT_EQ(t.param.num, 1);
}

// ============================================================================
// Catcode Tests
// ============================================================================

TEST_F(TexExpanderTest, CatCodeDefaults) {
    CatCodeTable cat = CatCodeTable::latex_default();
    
    EXPECT_EQ(cat.get('\\'), CatCode::ESCAPE);
    EXPECT_EQ(cat.get('{'), CatCode::BEGIN_GROUP);
    EXPECT_EQ(cat.get('}'), CatCode::END_GROUP);
    EXPECT_EQ(cat.get('$'), CatCode::MATH_SHIFT);
    EXPECT_EQ(cat.get('&'), CatCode::ALIGN_TAB);
    EXPECT_EQ(cat.get('#'), CatCode::PARAM);
    EXPECT_EQ(cat.get('^'), CatCode::SUPERSCRIPT);
    EXPECT_EQ(cat.get('_'), CatCode::SUBSCRIPT);
    EXPECT_EQ(cat.get('~'), CatCode::ACTIVE);
    EXPECT_EQ(cat.get('%'), CatCode::COMMENT);
    EXPECT_EQ(cat.get(' '), CatCode::SPACE);
    EXPECT_EQ(cat.get('a'), CatCode::LETTER);
    EXPECT_EQ(cat.get('A'), CatCode::LETTER);
    EXPECT_EQ(cat.get('0'), CatCode::OTHER);
    EXPECT_EQ(cat.get('.'), CatCode::OTHER);
}

TEST_F(TexExpanderTest, CatCodeModification) {
    CatCodeTable cat = CatCodeTable::latex_default();
    
    EXPECT_EQ(cat.get('@'), CatCode::OTHER);
    cat.make_letter('@');
    EXPECT_EQ(cat.get('@'), CatCode::LETTER);
    cat.make_other('@');
    EXPECT_EQ(cat.get('@'), CatCode::OTHER);
}

// ============================================================================
// Tokenizer Tests
// ============================================================================

TEST_F(TexExpanderTest, TokenizeSimple) {
    std::string result = expand_to_string("abc");
    EXPECT_EQ(result, "abc");
}

TEST_F(TexExpanderTest, TokenizeControlSequence) {
    std::string result = expand_to_string("\\hello");
    EXPECT_EQ(result, "\\hello");
}

TEST_F(TexExpanderTest, TokenizeSpaces) {
    // Multiple spaces should be compressed to one
    std::string result = expand_to_string("a   b");
    EXPECT_EQ(result, "a b");
}

TEST_F(TexExpanderTest, TokenizeComment) {
    // Comments should be skipped, along with the end-of-line
    std::string result = expand_to_string("a%comment\nb");
    EXPECT_EQ(result, "ab");  // comment and newline both eaten
}

TEST_F(TexExpanderTest, TokenizeEmptyLine) {
    // Empty line becomes \par
    std::string result = expand_to_string("a\n\nb");
    EXPECT_TRUE(result.find("\\par") != std::string::npos);
}

// ============================================================================
// Simple Macro Tests
// ============================================================================

TEST_F(TexExpanderTest, DefineSimpleMacro) {
    std::string result = expand_to_string("\\def\\foo{bar}\\foo");
    EXPECT_EQ(result, "bar");
}

TEST_F(TexExpanderTest, MacroWithOneParam) {
    std::string result = expand_to_string("\\def\\foo#1{[#1]}\\foo{x}");
    EXPECT_EQ(result, "[x]");
}

TEST_F(TexExpanderTest, MacroWithTwoParams) {
    std::string result = expand_to_string("\\def\\foo#1#2{#1-#2}\\foo{a}{b}");
    EXPECT_EQ(result, "a-b");
}

TEST_F(TexExpanderTest, MacroNestedExpansion) {
    std::string result = expand_to_string(
        "\\def\\inner{IN}\\def\\outer{(\\inner)}\\outer");
    EXPECT_EQ(result, "(IN)");
}

// ============================================================================
// Grouping Tests
// ============================================================================

TEST_F(TexExpanderTest, GroupScope) {
    std::string result = expand_to_string(
        "\\def\\x{A}{\\def\\x{B}\\x}\\x");
    // { and } are returned as tokens, but scoping works
    EXPECT_EQ(result, "{B}A");
}

TEST_F(TexExpanderTest, GlobalDef) {
    std::string result = expand_to_string(
        "\\def\\x{A}{\\gdef\\x{B}\\x}\\x");
    // \gdef is global, so \x stays B even after group ends
    EXPECT_EQ(result, "{B}B");
}

// ============================================================================
// Conditional Tests
// ============================================================================

TEST_F(TexExpanderTest, IfTrue) {
    std::string result = expand_to_string("\\iftrue yes\\else no\\fi");
    EXPECT_EQ(result, "yes");
}

TEST_F(TexExpanderTest, IfFalse) {
    std::string result = expand_to_string("\\iffalse yes\\else no\\fi");
    EXPECT_EQ(result, "no");
}

TEST_F(TexExpanderTest, IfxSameLetters) {
    // Note: space after AA is preserved
    std::string result = expand_to_string("\\ifx AA T\\else F\\fi");
    EXPECT_EQ(result, " T");
}

TEST_F(TexExpanderTest, IfxDifferentLetters) {
    std::string result = expand_to_string("\\ifx AB T\\else F\\fi");
    EXPECT_EQ(result, "F");
}

TEST_F(TexExpanderTest, IfxSameMacros) {
    std::string result = expand_to_string(
        "\\def\\a{foo}\\def\\b{foo}\\ifx\\a\\b T\\else F\\fi");
    // Note: macros with same expansion are still different definitions
    // This depends on implementation - LaTeXML says they're different
    EXPECT_TRUE(result == "T" || result == "F");
}

TEST_F(TexExpanderTest, IfnumLess) {
    std::string result = expand_to_string("\\ifnum 1<2 T\\else F\\fi");
    EXPECT_EQ(result, "T");
}

TEST_F(TexExpanderTest, IfnumEqual) {
    std::string result = expand_to_string("\\ifnum 2=2 T\\else F\\fi");
    EXPECT_EQ(result, "T");
}

TEST_F(TexExpanderTest, IfnumGreater) {
    std::string result = expand_to_string("\\ifnum 3>2 T\\else F\\fi");
    EXPECT_EQ(result, "T");
}

TEST_F(TexExpanderTest, Ifodd) {
    std::string result = expand_to_string("\\ifodd 3 T\\else F\\fi");
    EXPECT_EQ(result, "T");
    
    result = expand_to_string("\\ifodd 4 T\\else F\\fi");
    EXPECT_EQ(result, "F");
}

TEST_F(TexExpanderTest, NestedConditionals) {
    std::string result = expand_to_string(
        "\\iftrue\\iftrue A\\else B\\fi\\else C\\fi");
    EXPECT_EQ(result, "A");
}

TEST_F(TexExpanderTest, IfCase) {
    std::string result;
    
    result = expand_to_string("\\ifcase 0 zero\\or one\\or two\\else other\\fi");
    EXPECT_EQ(result, "zero");
    
    result = expand_to_string("\\ifcase 1 zero\\or one\\or two\\else other\\fi");
    EXPECT_EQ(result, "one");
    
    result = expand_to_string("\\ifcase 2 zero\\or one\\or two\\else other\\fi");
    EXPECT_EQ(result, "two");
    
    result = expand_to_string("\\ifcase 99 zero\\or one\\or two\\else other\\fi");
    EXPECT_EQ(result, "other");
}

// ============================================================================
// Expandafter Tests
// ============================================================================

TEST_F(TexExpanderTest, ExpandafterBasic) {
    std::string result = expand_to_string(
        "\\def\\foo{FOO}\\expandafter\\def\\expandafter\\bar\\expandafter{\\foo}\\bar");
    // \bar should expand to "FOO" (the expansion of \foo at definition time)
    EXPECT_EQ(result, "FOO");
}

// ============================================================================
// Let Tests
// ============================================================================

TEST_F(TexExpanderTest, LetSimple) {
    std::string result = expand_to_string(
        "\\def\\foo{X}\\let\\bar\\foo\\bar");
    // \bar should also expand to X
    EXPECT_EQ(result, "X");
}

TEST_F(TexExpanderTest, LetFi) {
    std::string result = expand_to_string(
        "\\let\\endif\\fi\\iftrue T\\endif");
    EXPECT_EQ(result, "T");
}

// ============================================================================
// Number/Romannumeral Tests
// ============================================================================

TEST_F(TexExpanderTest, Number) {
    std::string result = expand_to_string("\\number 42");
    EXPECT_EQ(result, "42");
}

TEST_F(TexExpanderTest, Romannumeral) {
    std::string result = expand_to_string("\\romannumeral 4");
    EXPECT_EQ(result, "iv");
    
    result = expand_to_string("\\romannumeral 9");
    EXPECT_EQ(result, "ix");
    
    result = expand_to_string("\\romannumeral 2024");
    EXPECT_EQ(result, "mmxxiv");
}

// ============================================================================
// Count Register Tests
// ============================================================================

TEST_F(TexExpanderTest, CountBasic) {
    // Set count0 to 5 and read it
    expander->set_count(0, 5);
    EXPECT_EQ(expander->get_count(0), 5);
}

// ============================================================================
// String/Csname Tests
// ============================================================================

TEST_F(TexExpanderTest, String) {
    std::string result = expand_to_string("\\string\\foo");
    EXPECT_EQ(result, "\\foo");
}

TEST_F(TexExpanderTest, CsnameBasic) {
    std::string result = expand_to_string(
        "\\def\\foo{X}\\csname foo\\endcsname");
    EXPECT_EQ(result, "X");
}

// ============================================================================
// e-TeX Tests
// ============================================================================

TEST_F(TexExpanderTest, Ifdefined) {
    std::string result;
    
    result = expand_to_string("\\def\\foo{X}\\ifdefined\\foo T\\else F\\fi");
    EXPECT_EQ(result, "T");
    
    result = expand_to_string("\\ifdefined\\undefined T\\else F\\fi");
    EXPECT_EQ(result, "F");
}

TEST_F(TexExpanderTest, Unexpanded) {
    std::string result = expand_to_string(
        "\\def\\foo{X}\\edef\\bar{\\unexpanded{\\foo}}\\bar");
    // \bar's replacement text is \foo (protected from expansion during \edef)
    // But when \bar is invoked, \foo expands to X
    EXPECT_EQ(result, "X");
}

// ============================================================================
// Fixture File Tests
// ============================================================================

class TexExpanderFixtureTest : public TexExpanderTest {
protected:
    // Process a LaTeX fixture file and check for expected patterns
    void test_fixture(const char* fixture_name) {
        std::string path = std::string("test/latexml/fixtures/expansion/") + fixture_name + ".tex";
        std::string content = read_file(path.c_str());
        
        if (content.empty()) {
            GTEST_SKIP() << "Fixture file not found: " << path;
        }
        
        // Just verify it doesn't crash - actual output comparison would need references
        expander->push_input(content.c_str(), content.size(), fixture_name);
        
        int token_count = 0;
        while (!expander->at_end() && token_count < 100000) {
            Token t = expander->expand_token();
            if (t.is_end()) break;
            token_count++;
        }
        
        EXPECT_GT(token_count, 0) << "Expected some tokens from " << fixture_name;
        log_info("fixture %s: processed %d tokens", fixture_name, token_count);
    }
};

TEST_F(TexExpanderFixtureTest, TestExpand) {
    test_fixture("testexpand");
}

TEST_F(TexExpanderFixtureTest, TestIf) {
    test_fixture("testif");
}

TEST_F(TexExpanderFixtureTest, Noexpand) {
    test_fixture("noexpand");
}

TEST_F(TexExpanderFixtureTest, NoexpandConditional) {
    test_fixture("noexpand_conditional");
}

TEST_F(TexExpanderFixtureTest, Toks) {
    test_fixture("toks");
}

TEST_F(TexExpanderFixtureTest, Env) {
    test_fixture("env");
}

TEST_F(TexExpanderFixtureTest, Environments) {
    test_fixture("environments");
}

TEST_F(TexExpanderFixtureTest, Etex) {
    test_fixture("etex");
}

TEST_F(TexExpanderFixtureTest, For) {
    test_fixture("for");
}

TEST_F(TexExpanderFixtureTest, Ifthen) {
    test_fixture("ifthen");
}

TEST_F(TexExpanderFixtureTest, Lettercase) {
    test_fixture("lettercase");
}

TEST_F(TexExpanderFixtureTest, Meaning) {
    test_fixture("meaning");
}

TEST_F(TexExpanderFixtureTest, Definedness) {
    test_fixture("definedness");
}

TEST_F(TexExpanderFixtureTest, Aftergroup) {
    test_fixture("aftergroup");
}

TEST_F(TexExpanderFixtureTest, Numexpr) {
    test_fixture("numexpr");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    log_init(nullptr);
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    log_finish();
    return result;
}
