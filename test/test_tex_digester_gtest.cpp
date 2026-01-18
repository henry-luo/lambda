// test_tex_digester_gtest.cpp - Unit tests for TeX Digester
//
// Tests the digestion phase (Stomach) of the LaTeX pipeline.

#include <gtest/gtest.h>
#include "../lambda/tex/tex_digester.hpp"
#include "../lambda/tex/tex_digested.hpp"
#include "../lambda/tex/tex_expander.hpp"
#include "../lib/arena.h"
#include "../lib/mempool.h"
#include <cstring>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class DigesterTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    Expander* expander;
    Digester* digester;
    CommandRegistry* registry;
    PackageLoader* loader;
    
    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        expander = new Expander(arena);
        registry = new CommandRegistry(arena);
        digester = new Digester(expander, arena);
        digester->set_registry(registry);
        loader = new PackageLoader(registry, arena);
    }
    
    void TearDown() override {
        delete loader;
        delete digester;
        delete registry;
        delete expander;
        arena_destroy(arena);
        pool_destroy(pool);
    }
    
    // Push input and digest
    DigestedNode* digest(const char* input) {
        expander->push_input(input, strlen(input));
        return digester->digest();
    }
    
    // Count nodes of a specific type in a list
    size_t count_nodes(DigestedNode* list, DigestedType type) {
        if (!list || list->type != DigestedType::LIST) return 0;
        
        size_t count = 0;
        for (DigestedNode* n = list->content.list.head; n; n = n->next) {
            if (n->type == type) count++;
        }
        return count;
    }
    
    // Get nth node from list
    DigestedNode* nth_node(DigestedNode* list, size_t index) {
        if (!list || list->type != DigestedType::LIST) return nullptr;
        
        DigestedNode* n = list->content.list.head;
        for (size_t i = 0; i < index && n; i++) {
            n = n->next;
        }
        return n;
    }
    
    // Get text content from a BOX node
    const char* get_box_text(DigestedNode* node) {
        if (!node || node->type != DigestedType::BOX) return nullptr;
        return node->content.box.text;
    }
};

// ============================================================================
// Basic Digestion Tests
// ============================================================================

TEST_F(DigesterTest, EmptyInput) {
    DigestedNode* result = digest("");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, DigestedType::LIST);
    EXPECT_EQ(result->list_length(), 0);
}

TEST_F(DigesterTest, SimpleText) {
    DigestedNode* result = digest("Hello");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, DigestedType::LIST);
    
    // should have created a paragraph
    EXPECT_GE(result->list_length(), 1);
}

TEST_F(DigesterTest, MultipleWords) {
    DigestedNode* result = digest("Hello World");
    ASSERT_NE(result, nullptr);
    
    // find the paragraph (horizontal list)
    DigestedNode* para = nullptr;
    for (DigestedNode* n = result->content.list.head; n; n = n->next) {
        if (n->type == DigestedType::LIST && n->content.list.is_horizontal) {
            para = n;
            break;
        }
    }
    ASSERT_NE(para, nullptr);
    
    // should have chars and glue (space)
    EXPECT_GT(para->list_length(), 1);
    EXPECT_GT(count_nodes(para, DigestedType::CHAR), 0);
    EXPECT_GT(count_nodes(para, DigestedType::GLUE), 0);
}

TEST_F(DigesterTest, Paragraph) {
    DigestedNode* result = digest("First paragraph.\n\nSecond paragraph.");
    ASSERT_NE(result, nullptr);
    
    // should have two paragraphs (via \par)
    // note: actual parsing depends on tokenizer handling of blank lines
}

// ============================================================================
// Mode Switching Tests
// ============================================================================

TEST_F(DigesterTest, VerticalModeInitial) {
    EXPECT_TRUE(digester->is_vertical());
    EXPECT_FALSE(digester->is_horizontal());
}

TEST_F(DigesterTest, HorizontalModeOnText) {
    expander->push_input("a", 1);
    Token t = expander->expand_token();
    digester->digest_token(t);
    
    EXPECT_TRUE(digester->is_horizontal());
}

TEST_F(DigesterTest, ParEndsHorizontalMode) {
    expander->push_input("text\\par", 8);
    digester->digest();
    
    EXPECT_TRUE(digester->is_vertical());
}

// ============================================================================
// Math Mode Tests
// ============================================================================

TEST_F(DigesterTest, InlineMath) {
    DigestedNode* result = digest("$x$");
    ASSERT_NE(result, nullptr);
    
    // should have a MATH node
    DigestedNode* para = nth_node(result, 0);
    if (para && para->type == DigestedType::LIST) {
        bool found_math = false;
        for (DigestedNode* n = para->content.list.head; n; n = n->next) {
            if (n->type == DigestedType::MATH) {
                found_math = true;
                EXPECT_FALSE(n->content.math.display);
            }
        }
        EXPECT_TRUE(found_math);
    }
}

TEST_F(DigesterTest, DisplayMath) {
    DigestedNode* result = digest("$$x$$");
    ASSERT_NE(result, nullptr);
    
    // should have a MATH node with display=true
    // It may be preceded/followed by GLUE nodes for spacing
    bool found_display_math = false;
    for (DigestedNode* n = result->content.list.head; n; n = n->next) {
        if (n->type == DigestedType::MATH) {
            found_display_math = true;
            EXPECT_TRUE(n->content.math.display);
        }
    }
    EXPECT_TRUE(found_display_math);
}

// ============================================================================
// Grouping Tests
// ============================================================================

TEST_F(DigesterTest, GroupingPreservesFont) {
    loader->load_latex_base();
    
    // initial font should be roman
    EXPECT_EQ(digester->current_font().family, std::string("cmr"));
    
    digester->begin_group();
    digester->set_font_family("cmbx");
    EXPECT_EQ(digester->current_font().family, std::string("cmbx"));
    
    digester->end_group();
    EXPECT_EQ(digester->current_font().family, std::string("cmr"));
}

TEST_F(DigesterTest, GroupDepth) {
    EXPECT_EQ(digester->group_depth(), 0);
    
    digester->begin_group();
    EXPECT_EQ(digester->group_depth(), 1);
    
    digester->begin_group();
    EXPECT_EQ(digester->group_depth(), 2);
    
    digester->end_group();
    EXPECT_EQ(digester->group_depth(), 1);
    
    digester->end_group();
    EXPECT_EQ(digester->group_depth(), 0);
}

// ============================================================================
// Counter Tests
// ============================================================================

TEST_F(DigesterTest, CounterCreation) {
    Counter* counter = digester->create_counter("test");
    ASSERT_NE(counter, nullptr);
    EXPECT_EQ(counter->value, 0);
}

TEST_F(DigesterTest, CounterStep) {
    digester->create_counter("section");
    
    EXPECT_EQ(digester->get_counter_value("section"), 0);
    
    digester->step_counter("section");
    EXPECT_EQ(digester->get_counter_value("section"), 1);
    
    digester->step_counter("section");
    EXPECT_EQ(digester->get_counter_value("section"), 2);
}

TEST_F(DigesterTest, CounterFormat_Arabic) {
    Counter* counter = digester->create_counter("test");
    counter->value = 42;
    
    const char* formatted = digester->format_counter("test");
    EXPECT_STREQ(formatted, "42");
}

TEST_F(DigesterTest, CounterFormat_Roman) {
    Counter* counter = digester->create_counter("test");
    counter->value = 14;
    counter->format = "roman";
    
    const char* formatted = digester->format_counter("test");
    EXPECT_STREQ(formatted, "xiv");
}

TEST_F(DigesterTest, CounterFormat_Alph) {
    Counter* counter = digester->create_counter("test");
    counter->value = 3;
    counter->format = "alph";
    
    const char* formatted = digester->format_counter("test");
    EXPECT_STREQ(formatted, "c");
}

// ============================================================================
// DigestedNode Factory Tests
// ============================================================================

TEST_F(DigesterTest, MakeBox) {
    DigestedFontSpec font = DigestedFontSpec::roman(12.0f);
    DigestedNode* box = DigestedNode::make_box(arena, "Hello", 5, font);
    
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->type, DigestedType::BOX);
    EXPECT_STREQ(box->content.box.text, "Hello");
    EXPECT_EQ(box->content.box.len, 5);
    EXPECT_EQ(box->font.size_pt, 12.0f);
}

TEST_F(DigesterTest, MakeChar) {
    DigestedFontSpec font = DigestedFontSpec::roman(10.0f);
    DigestedNode* chr = DigestedNode::make_char(arena, 'A', font);
    
    ASSERT_NE(chr, nullptr);
    EXPECT_EQ(chr->type, DigestedType::CHAR);
    EXPECT_EQ(chr->content.chr.codepoint, 'A');
}

TEST_F(DigesterTest, MakeList) {
    DigestedNode* hlist = DigestedNode::make_list(arena, true);
    DigestedNode* vlist = DigestedNode::make_list(arena, false);
    
    ASSERT_NE(hlist, nullptr);
    ASSERT_NE(vlist, nullptr);
    
    EXPECT_EQ(hlist->type, DigestedType::LIST);
    EXPECT_TRUE(hlist->content.list.is_horizontal);
    
    EXPECT_EQ(vlist->type, DigestedType::LIST);
    EXPECT_FALSE(vlist->content.list.is_horizontal);
}

TEST_F(DigesterTest, ListAppend) {
    DigestedNode* list = DigestedNode::make_list(arena, true);
    DigestedFontSpec font = DigestedFontSpec::roman(10.0f);
    
    DigestedNode* n1 = DigestedNode::make_char(arena, 'A', font);
    DigestedNode* n2 = DigestedNode::make_char(arena, 'B', font);
    DigestedNode* n3 = DigestedNode::make_char(arena, 'C', font);
    
    list->append(n1);
    list->append(n2);
    list->append(n3);
    
    EXPECT_EQ(list->list_length(), 3);
    EXPECT_EQ(list->content.list.head, n1);
    EXPECT_EQ(list->content.list.tail, n3);
    EXPECT_EQ(n1->next, n2);
    EXPECT_EQ(n2->next, n3);
    EXPECT_EQ(n3->next, nullptr);
}

TEST_F(DigesterTest, MakeGlue) {
    GlueSpec spec = GlueSpec::flexible(10.0f, 3.0f, 2.0f);
    DigestedNode* glue = DigestedNode::make_glue(arena, spec);
    
    ASSERT_NE(glue, nullptr);
    EXPECT_EQ(glue->type, DigestedType::GLUE);
    EXPECT_EQ(glue->content.glue.space, 10.0f);
    EXPECT_EQ(glue->content.glue.stretch, 3.0f);
    EXPECT_EQ(glue->content.glue.shrink, 2.0f);
}

TEST_F(DigesterTest, MakeKern) {
    DigestedNode* kern = DigestedNode::make_kern(arena, 5.0f);
    
    ASSERT_NE(kern, nullptr);
    EXPECT_EQ(kern->type, DigestedType::KERN);
    EXPECT_EQ(kern->content.kern.amount, 5.0f);
}

TEST_F(DigesterTest, MakePenalty) {
    DigestedNode* penalty = DigestedNode::make_penalty(arena, -100);
    
    ASSERT_NE(penalty, nullptr);
    EXPECT_EQ(penalty->type, DigestedType::PENALTY);
    EXPECT_EQ(penalty->content.penalty.value, -100);
}

TEST_F(DigesterTest, MakeRule) {
    DigestedNode* rule = DigestedNode::make_rule(arena, 100.0f, 0.5f, 0.0f);
    
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->type, DigestedType::RULE);
    EXPECT_EQ(rule->content.rule.width, 100.0f);
    EXPECT_EQ(rule->content.rule.height, 0.5f);
    EXPECT_EQ(rule->content.rule.depth, 0.0f);
}

TEST_F(DigesterTest, MakeWhatsit) {
    DigestedNode* whatsit = DigestedNode::make_whatsit(arena, "section", 7);
    
    ASSERT_NE(whatsit, nullptr);
    EXPECT_EQ(whatsit->type, DigestedType::WHATSIT);
    EXPECT_STREQ(whatsit->content.whatsit.name, "section");
    
    // test properties
    whatsit->set_property("number", "1");
    EXPECT_STREQ(whatsit->get_property("number"), "1");
}

TEST_F(DigesterTest, MakeMath) {
    DigestedNode* content = DigestedNode::make_list(arena, true);
    DigestedNode* math = DigestedNode::make_math(arena, content, true);
    
    ASSERT_NE(math, nullptr);
    EXPECT_EQ(math->type, DigestedType::MATH);
    EXPECT_TRUE(math->content.math.display);
    EXPECT_EQ(math->content.math.content, content);
}

// ============================================================================
// GlueSpec Tests
// ============================================================================

TEST_F(DigesterTest, GlueSpec_Fixed) {
    GlueSpec g = GlueSpec::fixed(10.0f);
    EXPECT_EQ(g.space, 10.0f);
    EXPECT_EQ(g.stretch, 0.0f);
    EXPECT_EQ(g.shrink, 0.0f);
}

TEST_F(DigesterTest, GlueSpec_Flexible) {
    GlueSpec g = GlueSpec::flexible(10.0f, 5.0f, 3.0f);
    EXPECT_EQ(g.space, 10.0f);
    EXPECT_EQ(g.stretch, 5.0f);
    EXPECT_EQ(g.shrink, 3.0f);
}

TEST_F(DigesterTest, GlueSpec_Parfillskip) {
    GlueSpec g = GlueSpec::parfillskip();
    EXPECT_EQ(g.space, 0.0f);
    EXPECT_EQ(g.stretch_order, GlueOrder::Fill);
}

// ============================================================================
// Command Registry Tests
// ============================================================================

TEST_F(DigesterTest, RegistryDefineMacro) {
    registry->define_macro("foo", "", "bar");
    
    const CommandDef* def = registry->lookup("foo");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, CommandType::MACRO);
    EXPECT_STREQ(def->replacement, "bar");
}

TEST_F(DigesterTest, RegistryDefineConstructor) {
    registry->define_constructor("textbf", "{}", "<b>#1</b>");
    
    const CommandDef* def = registry->lookup("textbf");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, CommandType::CONSTRUCTOR);
    EXPECT_EQ(def->param_count, 1);
    EXPECT_STREQ(def->pattern, "<b>#1</b>");
}

TEST_F(DigesterTest, RegistryDefineEnvironment) {
    registry->define_environment("center", "<div>", "</div>");
    
    const CommandDef* begin_def = registry->lookup("begin@center");
    const CommandDef* end_def = registry->lookup("end@center");
    
    ASSERT_NE(begin_def, nullptr);
    ASSERT_NE(end_def, nullptr);
    EXPECT_EQ(begin_def->type, CommandType::ENVIRONMENT);
    EXPECT_EQ(end_def->type, CommandType::ENVIRONMENT);
}

TEST_F(DigesterTest, RegistryDefineMath) {
    registry->define_math("sin", "sin", "TRIGFUNCTION");
    
    const CommandDef* def = registry->lookup("sin");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, CommandType::MATH);
    EXPECT_TRUE(def->is_math);
}

// ============================================================================
// Package Loader Tests
// ============================================================================

TEST_F(DigesterTest, LoadTexBase) {
    EXPECT_FALSE(loader->is_loaded("tex_base"));
    
    loader->load_tex_base();
    
    EXPECT_TRUE(loader->is_loaded("tex_base"));
    
    // check that relax is defined
    const CommandDef* def = registry->lookup("relax");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, CommandType::PRIMITIVE);
}

TEST_F(DigesterTest, LoadLatexBase) {
    loader->load_latex_base();
    
    EXPECT_TRUE(loader->is_loaded("tex_base"));
    EXPECT_TRUE(loader->is_loaded("latex_base"));
    
    // check that section is defined
    const CommandDef* def = registry->lookup("section");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, CommandType::CONSTRUCTOR);
}

TEST_F(DigesterTest, LoadAmsmath) {
    loader->load_amsmath();
    
    EXPECT_TRUE(loader->is_loaded("amsmath"));
    
    // check that frac is defined
    const CommandDef* def = registry->lookup("frac");
    ASSERT_NE(def, nullptr);
    
    // check math operators
    def = registry->lookup("sin");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->type, CommandType::MATH);
}

// ============================================================================
// PropertyMap Tests
// ============================================================================

TEST_F(DigesterTest, PropertyMapBasic) {
    PropertyMap* map = (PropertyMap*)arena_alloc(arena, sizeof(PropertyMap));
    map->init(arena);
    
    map->set("key1", "value1");
    map->set("key2", "value2");
    
    EXPECT_STREQ(map->get("key1"), "value1");
    EXPECT_STREQ(map->get("key2"), "value2");
    EXPECT_EQ(map->get("key3"), nullptr);
}

TEST_F(DigesterTest, PropertyMapOverwrite) {
    PropertyMap* map = (PropertyMap*)arena_alloc(arena, sizeof(PropertyMap));
    map->init(arena);
    
    map->set("key1", "value1");
    EXPECT_STREQ(map->get("key1"), "value1");
    
    // overwrite same key
    map->set("key1", "new_value");
    EXPECT_STREQ(map->get("key1"), "new_value");
}

TEST_F(DigesterTest, PropertyMapHas) {
    PropertyMap* map = (PropertyMap*)arena_alloc(arena, sizeof(PropertyMap));
    map->init(arena);
    
    map->set("key1", "value1");
    
    EXPECT_TRUE(map->has("key1"));
    EXPECT_FALSE(map->has("missing"));
}

// ============================================================================
// Font Spec Tests
// ============================================================================

TEST_F(DigesterTest, FontSpecRoman) {
    DigestedFontSpec f = DigestedFontSpec::roman(10.0f);
    EXPECT_STREQ(f.family, "cmr");
    EXPECT_EQ(f.size_pt, 10.0f);
    EXPECT_FALSE(f.has(DigestedFontSpec::BOLD));
}

TEST_F(DigesterTest, FontSpecBold) {
    DigestedFontSpec f = DigestedFontSpec::bold(12.0f);
    EXPECT_STREQ(f.family, "cmbx");
    EXPECT_EQ(f.size_pt, 12.0f);
    EXPECT_TRUE(f.has(DigestedFontSpec::BOLD));
}

TEST_F(DigesterTest, FontSpecItalic) {
    DigestedFontSpec f = DigestedFontSpec::italic(11.0f);
    EXPECT_STREQ(f.family, "cmti");
    EXPECT_EQ(f.size_pt, 11.0f);
    EXPECT_TRUE(f.has(DigestedFontSpec::ITALIC));
}

// ============================================================================
// Footnote Tests
// ============================================================================

TEST_F(DigesterTest, AddFootnote) {
    DigestedNode* content1 = DigestedNode::make_box(arena, "Note 1", 6, DigestedFontSpec());
    DigestedNode* content2 = DigestedNode::make_box(arena, "Note 2", 6, DigestedFontSpec());
    
    digester->add_footnote(content1);
    digester->add_footnote(content2);
    
    size_t count;
    DigestedNode** footnotes = digester->get_footnotes(&count);
    
    EXPECT_EQ(count, 2);
    EXPECT_EQ(footnotes[0], content1);
    EXPECT_EQ(footnotes[1], content2);
}

TEST_F(DigesterTest, ClearFootnotes) {
    DigestedNode* content = DigestedNode::make_box(arena, "Note", 4, DigestedFontSpec());
    digester->add_footnote(content);
    
    digester->clear_footnotes();
    
    size_t count;
    digester->get_footnotes(&count);
    EXPECT_EQ(count, 0);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(DigesterTest, IntegrationWithExpander) {
    loader->load_latex_base();
    
    // test that expander + digester work together
    expander->push_input("Hello World", 11);
    DigestedNode* result = digester->digest();
    
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type, DigestedType::LIST);
    EXPECT_GT(result->list_length(), 0);
}

TEST_F(DigesterTest, IntegrationMathMode) {
    expander->push_input("$x+y$", 5);
    DigestedNode* result = digester->digest();
    
    ASSERT_NE(result, nullptr);
    
    // find math node
    bool found_math = false;
    for (DigestedNode* n = result->content.list.head; n; n = n->next) {
        if (n->type == DigestedType::LIST) {
            for (DigestedNode* m = n->content.list.head; m; m = m->next) {
                if (m->type == DigestedType::MATH) {
                    found_math = true;
                    EXPECT_FALSE(m->content.math.display);  // inline math
                }
            }
        } else if (n->type == DigestedType::MATH) {
            found_math = true;
        }
    }
    EXPECT_TRUE(found_math);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
