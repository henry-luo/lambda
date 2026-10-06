#include <gtest/gtest.h>
#include "../../lambda/input/css/css_tokenizer.hpp"
#include "../../lambda/input/css/css_value_parser.hpp"
#include "../../lambda/input/css/css_parser.hpp"
#include "../../lambda/input/css/css_engine.hpp"
#include "../../lib/mempool.h"

class CssParserTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper to validate CSS tokenization works
    void validateTokenization(const char* css, size_t min_tokens = 1) {
        size_t token_count;
        CSSToken* tokens = css_tokenize(css, strlen(css), pool, &token_count);
        EXPECT_NE(tokens, nullptr) << "Should tokenize: " << css;
        EXPECT_GE(token_count, min_tokens) << "Should have at least " << min_tokens << " tokens";
    }
};

// Test basic CSS parsing components
TEST_F(CssParserTest, ParseEmptyStylesheet) {
    const char* css = "";

    // Empty CSS should still work with tokenizer
    size_t token_count;
    CSSToken* tokens = css_tokenize(css, 0, pool, &token_count);
    EXPECT_NE(tokens, nullptr) << "Empty CSS should still return tokens";
}

TEST_F(CssParserTest, ParseWhitespaceOnlyStylesheet) {
    const char* css = "   \n\t  \r\n  ";
    validateTokenization(css, 1); // Should produce whitespace tokens
}

// Test simple style rule parsing
TEST_F(CssParserTest, ParseSimpleStyleRule) {
    const char* css = "body { color: red; }";
    validateTokenization(css, 5); // body, {, color, :, red, ;, }

    // Test that parsers can be created
    CssPropertyValueParser* prop_parser = css_property_value_parser_create(pool);
    EXPECT_NE(prop_parser, nullptr) << "Property parser should be created";
    if (prop_parser) {
        css_property_value_parser_destroy(prop_parser);
    }

    // Legacy selector parser removed - modern array-based parser is integrated into css_parser.c
    // CSSSelectorParser* sel_parser = css_selector_parser_create(pool);
    // EXPECT_NE(sel_parser, nullptr) << "Selector parser should be created";
    // if (sel_parser) {
    //     css_selector_parser_destroy(sel_parser);
    // }
}

TEST_F(CssParserTest, ParseMultipleRules) {
    const char* css = "body { color: red; } div { margin: 10px; }";
    validateTokenization(css, 10); // Should have many tokens
}

TEST_F(CssParserTest, ParseInvalidCSS) {
    const char* css = "invalid { css } syntax";
    validateTokenization(css, 3); // Should still tokenize even if semantically invalid
}

TEST_F(CssParserTest, VarReferencePreservesEmptyAndNestedFallbacks) {
    CssPropertyValueParser* parser = css_property_value_parser_create(pool);
    ASSERT_NE(parser, nullptr);
    const char* cases[] = {" --missing , )", "--missing, rgb(255,0,0))"};
    for (int i = 0; i < 2; i++) {
        size_t count = 0;
        CssToken* tokens = css_tokenize(cases[i], strlen(cases[i]), pool, &count);
        ASSERT_NE(tokens, nullptr);
        CSSVarRef* reference = css_parse_var_function(parser, tokens, (int)count);
        ASSERT_NE(reference, nullptr);
        EXPECT_TRUE(reference->has_fallback);
        ASSERT_NE(reference->fallback, nullptr);
        if (i == 0) {
            EXPECT_EQ(reference->fallback->type, CSS_VALUE_TYPE_LIST);
            EXPECT_EQ(reference->fallback->data.list.count, 0);
        } else {
            ASSERT_EQ(reference->fallback->type, CSS_VALUE_TYPE_FUNCTION);
            ASSERT_NE(reference->fallback->data.function, nullptr);
            EXPECT_EQ(reference->fallback->data.function->arg_count, 3);
        }
    }
}

// ============================================================================
// CSS Engine Stylesheet Parsing Tests
// ============================================================================

class CssEngineParserTest : public ::testing::Test {
protected:
    Pool* pool;
    CssEngine* engine;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
        engine = css_engine_create(pool);
        ASSERT_NE(engine, nullptr) << "Failed to create CSS engine";
    }

    void TearDown() override {
        if (engine) {
            css_engine_destroy(engine);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }
};

TEST_F(CssEngineParserTest, InvalidSizeValuesDoNotEnterCascade) {
    const char* invalid[] = {
        "width: 1deg", "width: 1foo", "width: wobble(1px)",
        "width: calc(100px + foo)", "width: calc(100px + wobble(1))",
        "width: calc(100px * 2px)", "width: calc(100px + 1deg)",
        "width: round(10px)", "width: round(sideways, 10px, 2px)",
        "width: 12", "width: red", "font-size: 2s",
        "font-size: wobble(12px)", "font-size: auto", "all: red"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }

    const char* valid[] = {
        "width: calc(100% - 1px)", "width: calc(100px * sin(30deg))",
        "width: round(up, 13px, 10px)", "width: mod(-13px, 10px)",
        "width: fit-content(20em)",
        "width: auto", "max-width: none", "font-size: 1.5em",
        "font-size: large", "font-size: VAR(--size, 12px)", "all: initial"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, PhysicalSideLengthsValidateWholeValues) {
    const char* valid[] = {"margin-left:-2px", "top:-5%", "padding-top:calc(-2px + 4px)",
        "border-top-width:calc(1px + 2px)", "column-rule-width:thin", "right:auto"};
    const char* invalid[] = {"margin-left:3", "left:calc(1px + wat)",
        "padding-top:-1px", "padding-left:2px 3px", "border-top-width:3furlong",
        "border-left-width:2%", "column-rule-width:3", "margin-right:2deg"};
    for (const char* value : valid)
        EXPECT_NE(css_parse_declaration_text(value, strlen(value), pool), nullptr) << value;
    for (const char* value : invalid)
        EXPECT_EQ(css_parse_declaration_text(value, strlen(value), pool), nullptr) << value;
}

TEST_F(CssEngineParserTest, ImportantMarkerRequiresTrailingCaseInsensitiveTokens) {
    const char* important[] = {"width:20px !important", "width:20px !IMPORTANT",
        "width:20px ! /*priority*/ ImPoRtAnT /*end*/", "--size:20px !IMPORTANT"};
    for (const char* source : important) {
        CssDeclaration* declaration = css_parse_declaration_text(source, strlen(source), pool);
        ASSERT_NE(declaration, nullptr) << source;
        EXPECT_TRUE(declaration->important) << source;
    }
    const char* nested[] = {"--size:[!important]", "--size:(!important)",
        "--size:{!important}", "--size:var(--other, !important)"};
    for (const char* source : nested) {
        CssDeclaration* declaration = css_parse_declaration_text(source, strlen(source), pool);
        ASSERT_NE(declaration, nullptr) << source;
        EXPECT_FALSE(declaration->important) << source;
    }
    const char* invalid[] = {"width:20px !important junk", "width:20px !important 30px"};
    for (const char* source : invalid)
        EXPECT_EQ(css_parse_declaration_text(source, strlen(source), pool), nullptr) << source;
}

TEST_F(CssEngineParserTest, ImportantFontFamilyListValidatesWithoutPriorityTokens) {
    const char* source = "h1,h2,h3,h4,h5,h6{font-family:Cairo,\"Helvetica Neue\","
        "Helvetica,Arial,Geneva,sans-serif!important}";
    CssStylesheet* sheet = css_parse_stylesheet(engine, source, nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 1u);
    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule->data.style_rule.selector_group, nullptr);
    ASSERT_EQ(rule->data.style_rule.selector_group->selector_count, 6);
    ASSERT_EQ(rule->data.style_rule.declaration_count, 1u);
    CssDeclaration* declaration = rule->data.style_rule.declarations[0];
    ASSERT_NE(declaration, nullptr);
    EXPECT_TRUE(declaration->important);
    ASSERT_EQ(declaration->value->type, CSS_VALUE_TYPE_LIST);
    EXPECT_EQ(declaration->value->data.list.count, 6);
    EXPECT_EQ(declaration->value->data.list.values[5]->type, CSS_VALUE_TYPE_KEYWORD);
    EXPECT_EQ(declaration->value->data.list.values[5]->data.keyword, CSS_VALUE_SANS_SERIF);
}

TEST_F(CssEngineParserTest, KeyframesPreserveMathWhitespace) {
    const char* css = "@keyframes units { from { width:calc(2em + 10%); } "
        "to { width:calc(4em + 20%); } }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 1u);
    ASSERT_EQ(sheet->rules[0]->type, CSS_RULE_KEYFRAMES);
    ASSERT_NE(sheet->rules[0]->data.generic_rule.content, nullptr);
    EXPECT_NE(strstr(sheet->rules[0]->data.generic_rule.content, "calc(2em + 10%)"), nullptr);
    EXPECT_NE(strstr(sheet->rules[0]->data.generic_rule.content, "calc(4em + 20%)"), nullptr);
}

TEST_F(CssEngineParserTest, SpacingShorthandsValidateTokensBeforeCascade) {
    const char* valid[] = {
        "margin: -2px 5% auto", "padding: calc(2px + 3px) 4px",
        "margin: var(--gap) 30px", "padding: var(--pad) 3px",
        "margin: inherit", "padding: unset"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "margin: 1deg", "margin: wobble(2px)",
        "margin: 1px 2px 3px 4px 5px", "margin: 1px inherit",
        "padding: -1px", "padding: -2%", "padding: 4px auto",
        "padding: 4px wobble(2px)", "padding: 2px, 3px"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, AnimationValuesValidateWholeDeclaration) {
    const char* valid[] = {
        "animation:grow 1s linear -.5s 2 alternate both paused",
        "animation:2s step-start none", "animation:spin 1s, grow 2s",
        "animation:\"reverse\" 2s cubic-bezier(.1,2,.8,-1)",
        "animation:var(--motion)", "animation-name:grow, \"Grow\"",
        "animation:grow -1s", "animation:grow -1s 2s",
        "animation-duration:1s, 250ms", "animation-delay:-1s, 0s",
        "animation-iteration-count:0, .5, infinite",
        "animation-timing-function:steps(2,jump-none), cubic-bezier(0,0,1,1)"
    };
    const char* invalid[] = {
        "animation:grow -1s -2s", "animation:grow 1px", "animation:grow 1s 2s 3s",
        "animation:grow spin 1s", "animation:grow 1s running paused",
        "animation:grow 1s cubic-bezier(-.1,0,1,1)",
        "animation:grow 1s steps(1,jump-none)", "animation:grow 1s steps(1.5)",
        "animation:grow 1s steps(2.0)", "animation:grow 1s steps(2e0)",
        "animation-name:default", "animation-name:grow spin",
        "animation:grow 1foo", "animation-name:1foo",
        "animation-duration:-1s", "animation-duration:1s 2s",
        "animation-duration:0", "animation-iteration-count:-.5",
        "animation-direction:normal, inherit", "animation-fill-mode:red"
    };
    for (const char* source : valid) {
        EXPECT_NE(css_parse_declaration_text(source, strlen(source), pool), nullptr) << source;
    }
    for (const char* source : invalid) {
        EXPECT_EQ(css_parse_declaration_text(source, strlen(source), pool), nullptr) << source;
    }
}

TEST_F(CssEngineParserTest, ColorVariableGrammarDefersUntilSubstitution) {
    const char* valid[] = {
        "color:var(--x)", "color:{var(--x)}", "color:{ var(--x) }",
        "background-color:rgb(var(--channels) / .5)", "border-left-color:var(--border)"
    };
    const char* invalid[] = {
        "color:1px", "color:{red}", "color:var(--x) {}", "color:{} var(--x)",
        "color:{var(--x)} red", "color:red {var(--x)}"
    };
    for (const char* text : valid)
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    for (const char* text : invalid)
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
}

TEST_F(CssEngineParserTest, OpacityValidatesNumericDomainAndWholeValue) {
    const char* valid[] = {
        "opacity:50%", "opacity:-1", "opacity:2", "opacity:inherit",
        "opacity:var(--fade)", "opacity:calc(20% + 30%)",
        "opacity:clamp(0%, calc(10% + 20%), 100%)", "opacity:calc(50% * 2)",
        "opacity:min(80%, 90%)", "opacity:sin(30deg)"
    };
    const char* invalid[] = {
        "opacity:auto", "opacity:red", "opacity:1px", "opacity:1deg", "opacity:pi",
        "opacity:.5 .2", "opacity:50%, 80%", "opacity:calc(.2 + 1px)",
        "opacity:calc(10% + 2deg)", "opacity:calc(.2 + 30%)", "opacity:calc(50% * 50%)", "opacity:wobble(.5)", "opacity:calc(.2 +)"
    };
    for (const char* text : valid)
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    for (const char* text : invalid)
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
}

TEST_F(CssEngineParserTest, TransformValidatesFunctionArityAndNumericDomains) {
    const char* valid[] = {
        "transform:none", "transform:inherit", "transform:var(--motion)",
        "transform:translateX(var(--end))", "transform:translate(1em, 50%) rotate(.5turn)",
        "transform:scale(50%, 150%)", "transform:translate3d(10%, 1px, 2em)",
        "transform:rotate3d(0, 1, 0, 90deg)", "transform:matrix(1,0,0,1,10,20)",
        "transform:skewY(0)", "transform:perspective(100px)", "transform:perspective(none)",
        "transform:perspective(0)", "transform:perspective(.5px)",
        "transform:translateX(calc(10px + 50%))", "transform:scale(calc(50% * 2))"
    };
    const char* invalid[] = {
        "transform:wobble(1)", "transform:translateX()", "transform:translateX(1px,2px)",
        "transform:translate(10)", "transform:translateX(10deg)", "transform:rotate(10)",
        "transform:rotate(2px)", "transform:scale(2px)", "transform:translate3d(1px,2px,3%)",
        "transform:matrix(1,0,0,1,10)", "transform:perspective(-1px)",
        "transform:perspective(50%)", "transform:translateX(1px), rotate(0)",
        "transform:translateX(1px) garbage", "transform:none translateX(1px)",
        "transform:translateX(calc(10px + 1deg))"
    };
    for (const char* text : valid)
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    for (const char* text : invalid)
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
}

TEST_F(CssEngineParserTest, TransitionValuesValidateWholeDeclaration) {
    const char* valid[] = {
        "transition:opacity 1s linear -.5s, width 250ms steps(2,jump-start)",
        "transition:1s", "transition:none 2s", "transition:var(--motion)",
        "transition:opacity -1s", "transition:opacity -1s 2s",
        "transition-property:width, unknown-target, opacity",
        "transition-duration:1s, 250ms", "transition-delay:-1s, 0s",
        "transition-timing-function:steps(2,jump-none), ease", "transition:inherit"
    };
    const char* invalid[] = {
        "transition:opacity -1s -2s", "transition:opacity 1px",
        "transition:opacity 1s 2s 3s", "transition:opacity width 1s",
        "transition:opacity 1s linear ease", "transition:none 1s, opacity 2s",
        "transition-property:none, opacity", "transition-property:width opacity",
        "transition-property:default", "transition-property:\"opacity\"",
        "transition-property:1foo", "transition-duration:-1s",
        "transition-duration:0", "transition-duration:1s 2s",
        "transition-delay:1s, inherit", "transition-timing-function:steps(2.0)",
        "transition-timing-function:cubic-bezier(-1,0,1,1)"
    };
    for (const char* source : valid) {
        EXPECT_NE(css_parse_declaration_text(source, strlen(source), pool), nullptr) << source;
    }
    for (const char* source : invalid) {
        EXPECT_EQ(css_parse_declaration_text(source, strlen(source), pool), nullptr) << source;
    }
}

TEST_F(CssEngineParserTest, CustomPropertiesPreserveEmptyValuesAndCommaBoundaries) {
    const char* sources[] = {"--empty:;", "--space: ;", "--comma:,;", "--tail:red,;", "--leading:,blue;"};
    for (int i = 0; i < 5; i++) {
        CssDeclaration* declaration = css_parse_declaration_text(sources[i], strlen(sources[i]), pool);
        ASSERT_NE(declaration, nullptr) << sources[i];
        ASSERT_NE(declaration->value, nullptr) << sources[i];
        ASSERT_EQ(declaration->value->type, CSS_VALUE_TYPE_LIST) << sources[i];
        EXPECT_EQ(declaration->value->data.list.count, i < 2 ? 0 : 2) << sources[i];
        EXPECT_EQ(declaration->value->data.list.comma_separated, i >= 2) << sources[i];
    }
    EXPECT_EQ(css_parse_declaration_text("margin:;", 8, pool), nullptr);
}

TEST_F(CssEngineParserTest, LogicalOverflowValidatesAxisValues) {
    const char* valid[] = {
        "overflow-block: clip", "overflow-inline: auto",
        "overflow: hidden auto", "overflow-x: initial",
        "overflow-inline: overlay",
        "overflow-inline: var(--axis)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "overflow-block: bogus", "overflow-inline: 5px",
        "overflow: hidden bogus", "overflow: hidden auto scroll"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, OverscrollBehaviorValidatesAxesAndKeywords) {
    const char* valid[] = {
        "overscroll-behavior: auto", "overscroll-behavior: contain none",
        "overscroll-behavior: chain contain",
        "overscroll-behavior-x: none", "overscroll-behavior-y: inherit",
        "overscroll-behavior: var(--boundary)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "overscroll-behavior: visible", "overscroll-behavior: auto 5px",
        "overscroll-behavior: auto contain none",
        "overscroll-behavior-x: contain none",
        "overscroll-behavior-y: bogus"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, ScrollSpacingValidatesShorthandAndLonghandValues) {
    const char* valid[] = {
        "scroll-margin: -4px 2em 0 1px",
        "scroll-margin-inline: 2px -3px",
        "scroll-margin-block-start: var(--offset)",
        "scroll-padding: auto 10% 3px",
        "scroll-padding-inline: 2px 5%",
        "scroll-padding-top: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "scroll-margin: 10%", "scroll-margin: auto",
        "scroll-margin: 1px 2px 3px 4px 5px",
        "scroll-margin-block: 1px 2px 3px",
        "scroll-margin-top: 1px 2px",
        "scroll-padding: -2px", "scroll-padding: -5%",
        "scroll-padding: 1px inherit", "scroll-padding: 1px, 2px",
        "scroll-padding-inline-end: 2px 3px"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, OverflowClipMarginAcceptsBoxAndSignedLength) {
    const char* valid[] = {
        "overflow-clip-margin: 10px",
        "overflow-clip-margin: -2px border-box",
        "overflow-clip-margin: content-box 1em",
        "overflow-clip-margin: padding-box",
        "overflow-clip-margin: var(--edge)",
        "overflow-clip-margin: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "overflow-clip-margin: 10%", "overflow-clip-margin: auto",
        "overflow-clip-margin: content-box border-box",
        "overflow-clip-margin: 1px 2px",
        "overflow-clip-margin: margin-box",
        "overflow-clip-margin: 1px, border-box"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, ScrollSnapValidatesAxisStrictnessAndAlignment) {
    const char* valid[] = {
        "scroll-snap-type: none", "scroll-snap-type: x mandatory",
        "scroll-snap-type: block proximity", "scroll-snap-type: pair",
        "scroll-snap-align: start", "scroll-snap-align: center end",
        "scroll-snap-type: var(--snap)", "scroll-snap-align: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "scroll-snap-type: mandatory", "scroll-snap-type: x y",
        "scroll-snap-type: none mandatory",
        "scroll-snap-type: x mandatory proximity",
        "scroll-snap-align: auto", "scroll-snap-align: start bogus",
        "scroll-snap-align: start end center"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, ScrollBehaviorAcceptsOnlyAutoAndSmooth) {
    const char* valid[] = {
        "scroll-behavior: auto", "scroll-behavior: smooth",
        "scroll-behavior: inherit", "scroll-behavior: var(--motion)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "scroll-behavior: none", "scroll-behavior: smooth auto",
        "scroll-behavior: 300ms"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, LogicalBorderPartsValidateOneOrTwoSides) {
    const char* valid[] = {
        "border-inline-width: 2px 4px",
        "border-inline-style: solid dashed",
        "border-inline-color: red rgb(0, 0, 255)",
        "border-block-style: dotted double",
        "border-block-width: thin thick",
        "border-block-color: green purple",
        "border-inline-start-width: 0",
        "border-inline-end-color: currentColor",
        "border-block-end-style: inherit",
        "border-inline-width: var(--edge)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "border-inline-width: 2px 4px 6px",
        "border-inline-width: -2px",
        "border-inline-width: 20%",
        "border-inline-style: solid 2px",
        "border-block-style: solid dashed dotted",
        "border-inline-color: red bogus",
        "border-block-color: inherit red",
        "border-inline-start-color: red blue",
        "border-block-end-style: 3px"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, LogicalCornerRadiiValidatePhysicalValueGrammar) {
    const char* valid[] = {
        "border-start-start-radius: 10px",
        "border-start-end-radius: 20% 5px",
        "border-end-start-radius: 0",
        "border-end-end-radius: inherit",
        "border-end-end-radius: var(--corner)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "border-start-start-radius: -1px",
        "border-start-end-radius: 10% -5px",
        "border-end-start-radius: 1px 2px 3px",
        "border-end-end-radius: 1px / 2px",
        "border-end-end-radius: 2foo",
        "border-start-start-radius: red"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, BorderImageSliceValidatesFourSidesAndFill) {
    const char* valid[] = {
        "border-image-slice: 1", "border-image-slice: 10% 2 30% 4 fill",
        "border-image-slice: fill 0", "border-image-slice: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "border-image-slice: fill", "border-image-slice: 1 fill fill",
        "border-image-slice: 1 2 3 4 5", "border-image-slice: -1",
        "border-image-slice: 2px", "border-image-slice: 10% inherit"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, BorderImageWidthOutsetAndRepeatValidate) {
    const char* valid[] = {
        "border-image-width: 1", "border-image-width: 3px 20% auto 2",
        "border-image-outset: 0", "border-image-outset: 2 3px 4 5px",
        "border-image-repeat: round", "border-image-repeat: space stretch"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "border-image-width: -1", "border-image-width: 2px 3px 4px 5px 6px",
        "border-image-width: repeat", "border-image-outset: -2px",
        "border-image-outset: 10%", "border-image-repeat: repeat bogus",
        "border-image-repeat: round space repeat"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, BorderImageShorthandValidatesItsComponents) {
    const char* valid[] = {
        "border-image: none", "border-image: round",
        "border-image: url(border.png) 1 / 2 / 3 round",
        "border-image: linear-gradient(red, blue) 20% fill / 2px repeat",
        "border-image: 1 // 2", "border-image: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "border-image: 1 / -2", "border-image: 1 / 2 / 3 / 4",
        "border-image: 1 fill fill", "border-image: 1 / bogus",
        "border-image: url(border.png) 1 2 3 4 5"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, NamespaceRulesBindQualifiedTypeSelectors) {
    const char* css =
        "@namespace html \"http://www.w3.org/1999/xhtml\";"
        "@namespace \"http://www.w3.org/2000/svg\";"
        "html|div, *|span, |em, div {color:red}"
        "missing|div {color:blue}";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->namespace_count, 2u);
    ASSERT_GE(sheet->rule_count, 3u);
    CssRule* style = sheet->rules[2];
    ASSERT_EQ(style->type, CSS_RULE_STYLE);
    ASSERT_NE(style->data.style_rule.selector_group, nullptr);
    CssSelectorGroup* group = style->data.style_rule.selector_group;
    ASSERT_EQ(group->selector_count, 4u);
    const char* expected[] = {
        "http://www.w3.org/1999/xhtml", nullptr, "",
        "http://www.w3.org/2000/svg"
    };
    for (size_t i = 0; i < 4; i++) {
        CssSimpleSelector* type = group->selectors[i]->compound_selectors[0]
            ->simple_selectors[0];
        ASSERT_NE(type, nullptr);
        if (expected[i]) {
            ASSERT_NE(type->namespace_url, nullptr);
            EXPECT_STREQ(type->namespace_url, expected[i]);
        } else {
            EXPECT_EQ(type->namespace_url, nullptr);
        }
    }
    // An undeclared prefix invalidates the whole later style rule.
    EXPECT_EQ(sheet->rule_count, 3u);
    EXPECT_EQ(css_parse_selector_group_text("html|div", 8, pool), nullptr);
    EXPECT_NE(css_parse_selector_group_text("*|div", 6, pool), nullptr);
    EXPECT_NE(css_parse_selector_group_text("|div", 4, pool), nullptr);
}

TEST_F(CssEngineParserTest, NamespaceSyntaxAndRulePlacement) {
    const char* css =
        "@namespace h url(\"http://www.w3.org/1999/xhtml\");"
        "@namespace s url(http://www.w3.org/2000/svg);"
        "h|*, s|rect, |span, *|div { color: red }"
        "div { color: blue }"
        "@namespace late \"urn:late\";"
        "late|div { color: green }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->namespace_count, 2u);
    ASSERT_EQ(sheet->rule_count, 4u);
    CssSelectorGroup* group = sheet->rules[2]->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 4u);
    EXPECT_STREQ(group->selectors[0]->compound_selectors[0]->simple_selectors[0]
        ->namespace_url, "http://www.w3.org/1999/xhtml");
    EXPECT_STREQ(group->selectors[1]->compound_selectors[0]->simple_selectors[0]
        ->namespace_url, "http://www.w3.org/2000/svg");
    EXPECT_STREQ(group->selectors[2]->compound_selectors[0]->simple_selectors[0]
        ->namespace_url, "");
    EXPECT_EQ(group->selectors[3]->compound_selectors[0]->simple_selectors[0]
        ->namespace_url, nullptr);

    const char* invalid[] = {"h|", "|", "*|", "h| div", "h|.x", "h|div, |"};
    for (const char* selector : invalid) {
        EXPECT_EQ(css_parse_selector_group_text(selector, strlen(selector), pool),
            nullptr) << selector;
    }
}

TEST_F(CssEngineParserTest, AttributeNamespacesResolveIndependentlyOfDefault) {
    CssStylesheet* sheet = css_parse_stylesheet(engine,
        "@namespace x \"http://www.w3.org/1999/xlink\";"
        "@namespace \"http://www.w3.org/2000/svg\";"
        "[x|href], [|href], [*|href], [href] { color: red }"
        "[missing|href] { color: blue }", nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 3u);
    CssSelectorGroup* group = sheet->rules[2]->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 4u);
    const char* urls[] = {"http://www.w3.org/1999/xlink", "", nullptr, ""};
    for (size_t i = 0; i < 4; i++) {
        CssSimpleSelector* attr = group->selectors[i]->compound_selectors[0]
            ->simple_selectors[0];
        ASSERT_NE(attr, nullptr);
        EXPECT_EQ(attr->type, CSS_SELECTOR_ATTR_EXISTS);
        EXPECT_STREQ(attr->attribute.name, "href");
        if (urls[i]) EXPECT_STREQ(attr->namespace_url, urls[i]);
        else EXPECT_EQ(attr->namespace_url, nullptr);
    }
    EXPECT_EQ(css_parse_selector_group_text("[x|href]", 8, pool), nullptr);
    EXPECT_NE(css_parse_selector_group_text("[*|href]", 9, pool), nullptr);
    EXPECT_NE(css_parse_selector_group_text("[|href]", 7, pool), nullptr);
}

TEST_F(CssEngineParserTest, NamespaceErrorsRespectFunctionalListRecovery) {
    const char* css =
        "@namespace h \"http://www.w3.org/1999/xhtml\";"
        ":is(missing|div, h|div) { color: red }"
        ":where(missing|span, h|span) { color: green }"
        ":not(missing|div, h|div) { color: blue }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 3u);
    for (size_t i = 1; i < 3; i++) {
        CssSimpleSelector* pseudo = sheet->rules[i]->data.style_rule
            .selector_group->selectors[0]->compound_selectors[0]->simple_selectors[0];
        ASSERT_NE(pseudo, nullptr);
        ASSERT_EQ(pseudo->function_selector_count, 1u);
        EXPECT_STREQ(pseudo->function_selectors[0]->compound_selectors[0]
            ->simple_selectors[0]->namespace_url,
            "http://www.w3.org/1999/xhtml");
    }
    CssSelectorGroup* query = css_parse_selector_group_text(
        ":is(missing|div, |div)", strlen(":is(missing|div, |div)"), pool);
    ASSERT_NE(query, nullptr);
    ASSERT_EQ(query->selector_count, 1u);
    EXPECT_EQ(query->selectors[0]->compound_selectors[0]
        ->simple_selectors[0]->function_selector_count, 1u);
    EXPECT_EQ(css_parse_selector_group_text(
        ":not(missing|div, |div)", strlen(":not(missing|div, |div)"), pool),
        nullptr);
}

TEST_F(CssEngineParserTest, LayerOrderStatementPreservesFollowingRule) {
    CssStylesheet* sheet = css_parse_stylesheet(engine,
        "@layer reset, theme; div { color: green }", nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 2u);
    EXPECT_EQ(sheet->rules[0]->type, CSS_RULE_LAYER);
    EXPECT_EQ(sheet->rules[0]->data.conditional_rule.rule_count, 0u);
    ASSERT_EQ(sheet->rules[0]->data.conditional_rule.layer_name_count, 2u);
    EXPECT_STREQ(sheet->rules[0]->data.conditional_rule.layer_names[0].parts[0],
        "reset");
    EXPECT_STREQ(sheet->rules[0]->data.conditional_rule.layer_names[1].parts[0],
        "theme");
    EXPECT_EQ(sheet->rules[1]->type, CSS_RULE_STYLE);
    EXPECT_STREQ(sheet->rules[1]->data.style_rule.selector_group
        ->selectors[0]->compound_selectors[0]->simple_selectors[0]->value,
        "div");
}

TEST_F(CssEngineParserTest, ImportConditionsAndLayerNameArePreserved) {
    CssStylesheet* sheet = css_parse_stylesheet(engine,
        "@import url('theme.css') layer(theme.ui) "
        "supports(display: grid) screen and (min-width: 1px);",
        nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 1u);
    CssRule* rule = sheet->rules[0];
    ASSERT_EQ(rule->type, CSS_RULE_IMPORT);
    EXPECT_FALSE(rule->data.import_rule.invalid);
    EXPECT_STREQ(rule->data.import_rule.url, "theme.css");
    EXPECT_TRUE(rule->data.import_rule.has_layer);
    EXPECT_FALSE(rule->data.import_rule.anonymous_layer);
    ASSERT_EQ(rule->data.import_rule.layer_name.part_count, 2u);
    EXPECT_STREQ(rule->data.import_rule.layer_name.parts[0], "theme");
    EXPECT_STREQ(rule->data.import_rule.layer_name.parts[1], "ui");
    EXPECT_STREQ(rule->data.import_rule.supports, "(display: grid)");
    EXPECT_STREQ(rule->data.import_rule.media,
        "screen and (min-width: 1px)");
}

TEST_F(CssEngineParserTest, ImportPlacementRespectsLayerStatements) {
    CssStylesheet* sheet = css_parse_stylesheet(engine,
        "@layer base; @import 'first.css'; @layer later;"
        "@import 'late.css'; div { color: green }", nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 4u);
    EXPECT_EQ(sheet->rules[0]->type, CSS_RULE_LAYER);
    EXPECT_EQ(sheet->rules[1]->type, CSS_RULE_IMPORT);
    EXPECT_STREQ(sheet->rules[1]->data.import_rule.url, "first.css");
    EXPECT_EQ(sheet->rules[2]->type, CSS_RULE_LAYER);
    EXPECT_EQ(sheet->rules[3]->type, CSS_RULE_STYLE);

    CssStylesheet* after_style = css_parse_stylesheet(engine,
        "div { color: red } @import 'late.css';", nullptr);
    ASSERT_NE(after_style, nullptr);
    ASSERT_EQ(after_style->rule_count, 1u);
    EXPECT_EQ(after_style->rules[0]->type, CSS_RULE_STYLE);
}

TEST_F(CssEngineParserTest, CaretColorAcceptsAutoAndColors) {
    const char* valid[] = {
        "caret-color: auto", "caret-color: red",
        "caret-color: currentColor", "caret-color: var(--caret, blue)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {"caret-color: 4px", "caret-color: bogus"};
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, ImageRenderingAcceptsOnlyScalingKeywords) {
    const char* valid[] = {
        "image-rendering: auto", "image-rendering: smooth",
        "image-rendering: high-quality", "image-rendering: pixelated",
        "image-rendering: crisp-edges", "image-rendering: optimizeSpeed",
        "image-rendering: optimizeQuality", "image-rendering: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "image-rendering: bogus", "image-rendering: 2px",
        "image-rendering: pixelated smooth"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, GridAutoFlowDenseValidatesPair) {
    const char* valid[] = {
        "grid-auto-flow: row", "grid-auto-flow: column",
        "grid-auto-flow: dense", "grid-auto-flow: row dense",
        "grid-auto-flow: dense column", "grid-auto-flow: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "grid-auto-flow: row column", "grid-auto-flow: dense dense",
        "grid-auto-flow: row dense column", "grid-auto-flow: bogus"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, TextAlignmentLonghandsValidateTheirOwnKeywords) {
    const char* valid[] = {
        "text-align: justify-all", "text-align: -webkit-center",
        "text-align-all: match-parent", "text-align-all: justify",
        "text-align-last: auto", "text-align-last: end",
        "text-align-all: var(--align, right)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "text-align-all: justify-all", "text-align-all: auto",
        "text-align-last: justify-all", "text-align-last: bogus",
        "text-align: auto", "text-align-all: 2px"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, TextUnderlineOffsetValidatesLengthPercentageAndAuto) {
    const char* valid[] = {
        "text-underline-offset: auto", "text-underline-offset: 3px",
        "text-underline-offset: -2px", "text-underline-offset: 25%",
        "text-underline-offset: -10%", "text-underline-offset: 0",
        "text-underline-offset: calc(1em + 2px)",
        "text-underline-offset: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
    const char* invalid[] = {
        "text-underline-offset: 2", "text-underline-offset: 1deg",
        "text-underline-offset: 4foo", "text-underline-offset: center"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr)
            << text;
    }
}

TEST_F(CssEngineParserTest, TextDecorationSkipInkAcceptsDefinedModesOnly) {
    const char* valid[] = {
        "text-decoration-skip-ink: auto", "text-decoration-skip-ink: none",
        "text-decoration-skip-ink: all", "text-decoration-skip-ink: inherit",
        "text-decoration-skip-ink: var(--ink, none)"
    };
    for (const char* declaration : valid) {
        EXPECT_NE(css_parse_declaration_text(declaration, strlen(declaration), pool), nullptr)
            << declaration;
    }
    const char* invalid[] = {
        "text-decoration-skip-ink: 2px", "text-decoration-skip-ink: under",
        "text-decoration-skip-ink: none all"
    };
    for (const char* declaration : invalid) {
        EXPECT_EQ(css_parse_declaration_text(declaration, strlen(declaration), pool), nullptr)
            << declaration;
    }
}

TEST_F(CssEngineParserTest, TextUnderlinePositionValidatesBaseAndSide) {
    const char* valid[] = {
        "text-underline-position: auto", "text-underline-position: from-font",
        "text-underline-position: under", "text-underline-position: left",
        "text-underline-position: right under",
        "text-underline-position: from-font left",
        "text-underline-position: inherit",
        "text-underline-position: var(--underline, under)"
    };
    for (const char* declaration : valid) {
        EXPECT_NE(css_parse_declaration_text(declaration, strlen(declaration), pool), nullptr)
            << declaration;
    }
    const char* invalid[] = {
        "text-underline-position: auto under",
        "text-underline-position: under from-font",
        "text-underline-position: left right",
        "text-underline-position: under under",
        "text-underline-position: below", "text-underline-position: 2px"
    };
    for (const char* declaration : invalid) {
        EXPECT_EQ(css_parse_declaration_text(declaration, strlen(declaration), pool), nullptr)
            << declaration;
    }
}

TEST_F(CssEngineParserTest, BackgroundAttachmentAcceptsDefinedModesOnly) {
    const char* valid[] = {
        "background-attachment: scroll", "background-attachment: fixed",
        "background-attachment: local", "background-attachment: inherit",
        "background-attachment: var(--attachment, fixed)"
    };
    for (const char* declaration : valid) {
        EXPECT_NE(css_parse_declaration_text(declaration, strlen(declaration), pool), nullptr)
            << declaration;
    }
    const char* invalid[] = {
        "background-attachment: sticky", "background-attachment: 10px",
        "background-attachment: fixed local"
    };
    for (const char* declaration : invalid) {
        EXPECT_EQ(css_parse_declaration_text(declaration, strlen(declaration), pool), nullptr)
            << declaration;
    }
}

TEST_F(CssEngineParserTest, TabSizeAcceptsNumberOrLengthOnly) {
    const char* valid[] = {
        "tab-size: 0", "tab-size: 2.5", "tab-size: 24px",
        "tab-size: 1.5em", "tab-size: calc(12px + 4px)",
        "tab-size: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
    const char* invalid[] = {
        "tab-size: -1", "tab-size: -1px", "tab-size: 50%",
        "tab-size: auto", "tab-size: 3foo"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, ColumnRuleColorRejectsNonColorsBeforeCascade) {
    const char* valid[] = {
        "column-rule-color: red", "column-rule-color: rgb(0, 128, 0)",
        "column-rule-color: currentColor", "column-rule-color: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
    const char* invalid[] = {
        "column-rule-color: 10px", "column-rule-color: bogus",
        "column-rule-color: wobble(10)"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, TextIndentModifiersValidateWholeValue) {
    const char* valid[] = {
        "text-indent: 4em", "text-indent: 0", "text-indent: 20% hanging",
        "text-indent: each-line -2em", "text-indent: 4em each-line hanging",
        "text-indent: hanging each-line calc(2em + 1px)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
    const char* invalid[] = {
        "text-indent: hanging", "text-indent: 4em hanging hanging",
        "text-indent: 4em 5em", "text-indent: auto", "text-indent: 4foo",
        "text-indent: 4em bogus"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, TextDecorationLineAndThicknessValidateValues) {
    const char* valid[] = {
        "text-decoration-line: underline overline",
        "text-decoration-line: line-through underline overline",
        "text-decoration-line: none", "text-decoration-line: inherit",
        "text-decoration-thickness: 2px",
        "text-decoration-thickness: 25%",
        "text-decoration-thickness: from-font",
        "text-decoration: underline overline solid red 3px",
        "text-decoration: red", "text-decoration: from-font",
        "text-decoration: inherit"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
    const char* invalid[] = {
        "text-decoration-line: underline underline",
        "text-decoration-line: none overline",
        "text-decoration-line: red",
        "text-decoration-thickness: -2px",
        "text-decoration-thickness: 5foo",
        "text-decoration: underline underline red",
        "text-decoration: underline overline none",
        "text-decoration: red blue underline",
        "text-decoration: underline 5foo"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, TextEmphasisValidatesStyleColorAndPosition) {
    const char* valid[] = {
        "text-emphasis-style: filled dot", "text-emphasis-style: open sesame",
        "text-emphasis-style: '*'", "text-emphasis-style: none",
        "text-emphasis: open circle red", "text-emphasis: rgb(0, 128, 0) dot",
        "text-emphasis: red", "text-emphasis: inherit",
        "text-emphasis-color: transparent", "text-emphasis-position: under left",
        "text-emphasis-position: over"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
    const char* invalid[] = {
        "text-emphasis-style: filled open", "text-emphasis-style: dot circle",
        "text-emphasis-style: none dot", "text-emphasis-style: red",
        "text-emphasis: open open red", "text-emphasis: dot red blue",
        "text-emphasis-position: left", "text-emphasis-position: over under",
        "text-emphasis-color: 3px"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, TextJustifyAcceptsOnlyDefinedModes) {
    const char* valid[] = {
        "text-justify: auto", "text-justify: none",
        "text-justify: inter-word", "text-justify: inter-character",
        "text-justify: distribute", "text-justify: inherit",
        "text-justify: var(--method)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
    const char* invalid[] = {
        "text-justify: bogus", "text-justify: auto none",
        "text-justify: 2px", "text-justify: inter-letter"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, LogicalFloatAndClearRejectOtherKeywords) {
    const char* valid[] = {
        "float: inline-start", "float: inline-end", "float: left",
        "clear: inline-start", "clear: inline-end", "clear: both",
        "float: initial", "clear: var(--side)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
    const char* invalid[] = {
        "float: both", "float: top", "float: red",
        "clear: 4px", "clear: auto", "clear: left right"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, AppearanceAcceptsStandardAndCompatibilityValues) {
    const char* valid[] = {
        "appearance: none", "appearance: auto", "appearance: base",
        "appearance: base-select", "appearance: checkbox",
        "appearance: menulist-button", "appearance: inherit",
        "appearance: var(--look)"
    };
    for (const char* text : valid) {
        EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
    const char* invalid[] = {
        "appearance: red", "appearance: 2px",
        "appearance: auto none", "appearance: slider-vertical"
    };
    for (const char* text : invalid) {
        EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    }
}

TEST_F(CssEngineParserTest, ParseEmptyStylesheet) {
    const char* css = "";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 0) << "Empty stylesheet should have 0 rules";
}

TEST_F(CssEngineParserTest, ParseWhitespaceOnlyStylesheet) {
    const char* css = "   \n\t  \r\n  ";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 0) << "Whitespace-only stylesheet should have 0 rules";
}

TEST_F(CssEngineParserTest, ParseSimpleStyleRule) {
    const char* css = "body { color: red; }";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GT(stylesheet->rule_count, 0) << "Should have at least 1 rule";

    if (stylesheet->rule_count > 0) {
        CssRule* rule = stylesheet->rules[0];
        ASSERT_NE(rule, nullptr) << "Rule should not be NULL";
    }
}

TEST_F(CssEngineParserTest, ParseMultipleRules) {
    const char* css = "body { color: red; } div { margin: 10px; }";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GE(stylesheet->rule_count, 1) << "Should have at least 1 rule";
}

TEST_F(CssEngineParserTest, ParseInvalidCSS) {
    const char* css = "body { color: ; }"; // Missing value
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    // Should still create a stylesheet even with invalid CSS
    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL even with invalid CSS";
}

TEST_F(CssEngineParserTest, ParseImportUrlWithQueryAndPlus) {
    const char* css =
        "@import url(http://fonts.googleapis.com/css?family=Open+Sans:400,700,600);"
        "@import url(http://fonts.googleapis.com/css?family=Fjalla+One);";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    ASSERT_EQ(stylesheet->rule_count, 2) << "Both @import rules should parse";

    ASSERT_NE(stylesheet->rules[0], nullptr);
    ASSERT_NE(stylesheet->rules[1], nullptr);
    EXPECT_EQ(stylesheet->rules[0]->type, CSS_RULE_IMPORT);
    EXPECT_EQ(stylesheet->rules[1]->type, CSS_RULE_IMPORT);
    ASSERT_NE(stylesheet->rules[0]->data.import_rule.url, nullptr);
    ASSERT_NE(stylesheet->rules[1]->data.import_rule.url, nullptr);
    EXPECT_STREQ(stylesheet->rules[0]->data.import_rule.url,
                 "http://fonts.googleapis.com/css?family=Open+Sans:400,700,600");
    EXPECT_STREQ(stylesheet->rules[1]->data.import_rule.url,
                 "http://fonts.googleapis.com/css?family=Fjalla+One");
}

TEST_F(CssEngineParserTest, ParseLayerRules) {
    const char* css = "@layer components { .button { color: red; } }";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr);
    ASSERT_EQ(stylesheet->rule_count, 1);
    CssRule* layer = stylesheet->rules[0];
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(layer->type, CSS_RULE_LAYER);
    ASSERT_EQ(layer->data.conditional_rule.rule_count, 1);
    ASSERT_NE(layer->data.conditional_rule.rules[0], nullptr);
    EXPECT_EQ(layer->data.conditional_rule.rules[0]->type, CSS_RULE_STYLE);
}

// Regression: a nested qualified rule that itself contains another nested
// qualified rule used to cause `css_parse_declaration_from_tokens` to stall
// on the inner '}' (token type CSS_TOKEN_RIGHT_BRACE), spinning forever in
// the inner-decl loop of `css_parse_qualified_rule_from_tokens`. The parser
// must terminate (and return a stylesheet) for such input. Encountered on
// real-world stylesheets shipped by web.archive.org.
TEST_F(CssEngineParserTest, ParseNestedOfNestedRuleTerminates) {
    const char* css =
        ".outer {"
        "  color: red;"
        "  .middle {"
        "    background: blue;"
        "    .inner { width: 10px; }"
        "  }"
        "}";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    // The only thing this test asserts is that parsing terminates and yields
    // a non-null stylesheet. Reaching this line proves the infinite-loop
    // regression is fixed.
    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should parse without hanging";
}

TEST_F(CssEngineParserTest, NestedRulesRetainDepthAndAuthoredSelector) {
    CssStylesheet* stylesheet = css_parse_stylesheet(engine,
        ".outer { color:red; &.active { .child { color:green } } color:blue }",
        nullptr);
    ASSERT_NE(stylesheet, nullptr);
    ASSERT_EQ(stylesheet->rule_count, 1u);
    CssRule* outer = stylesheet->rules[0];
    ASSERT_EQ(outer->type, CSS_RULE_STYLE);
    ASSERT_EQ(outer->data.style_rule.nested_rule_count, 2u);
    CssRule* active = outer->data.style_rule.nested_rules[0];
    ASSERT_EQ(active->type, CSS_RULE_STYLE);
    ASSERT_NE(active->data.style_rule.authored_selector_text, nullptr);
    EXPECT_NE(strstr(active->data.style_rule.authored_selector_text, "&.active"), nullptr);
    ASSERT_EQ(active->data.style_rule.nested_rule_count, 1u);
    EXPECT_EQ(active->data.style_rule.nested_rules[0]->type, CSS_RULE_STYLE);
    CssRule* trailing = outer->data.style_rule.nested_rules[1];
    EXPECT_EQ(trailing->type, CSS_RULE_NESTED_DECLARATIONS);
    EXPECT_EQ(trailing->data.style_rule.selector_group,
              outer->data.style_rule.selector_group);
    EXPECT_EQ(trailing->data.style_rule.declaration_count, 1u);
}

TEST_F(CssEngineParserTest, NestedDeclarationsKeepInterleavedRuleOrder) {
    CssStylesheet* stylesheet = css_parse_stylesheet(engine,
        ".x { .one {} color:blue; .two {} color:green }", nullptr);
    ASSERT_NE(stylesheet, nullptr);
    ASSERT_EQ(stylesheet->rule_count, 1u);
    CssRule* outer = stylesheet->rules[0];
    ASSERT_EQ(outer->data.style_rule.nested_rule_count, 4u);
    EXPECT_EQ(outer->data.style_rule.nested_rules[0]->type, CSS_RULE_STYLE);
    EXPECT_EQ(outer->data.style_rule.nested_rules[1]->type,
              CSS_RULE_NESTED_DECLARATIONS);
    EXPECT_EQ(outer->data.style_rule.nested_rules[2]->type, CSS_RULE_STYLE);
    EXPECT_EQ(outer->data.style_rule.nested_rules[3]->type,
              CSS_RULE_NESTED_DECLARATIONS);
}

TEST_F(CssEngineParserTest, TopLevelNestingSelectorUsesScope) {
    CssStylesheet* stylesheet = css_parse_stylesheet(engine,
        "&.top, .plain { color:green }", nullptr);
    ASSERT_NE(stylesheet, nullptr);
    ASSERT_EQ(stylesheet->rule_count, 1u);
    CssRule* rule = stylesheet->rules[0];
    ASSERT_EQ(rule->type, CSS_RULE_STYLE);
    ASSERT_NE(rule->data.style_rule.selector_group, nullptr);
    EXPECT_EQ(rule->data.style_rule.selector_group->selector_count, 2u);
    ASSERT_NE(rule->data.style_rule.authored_selector_text, nullptr);
    EXPECT_NE(strstr(rule->data.style_rule.authored_selector_text, "&.top"), nullptr);
}

// CSS Syntax §5.5.6 consumes simple blocks inside a declaration before it
// drops an invalid value. Their closing braces must not terminate the rule.
TEST_F(CssEngineParserTest, InvalidDeclarationBlockDoesNotEscapeContainingRule) {
    const char* css =
        ".overlay {"
        "  background: article { display: block; }"
        "  .escaped { padding: 100px; }"
        "}"
        ".valid { color: blue; }";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 2u)
        << "the declaration payload must not become a top-level rule";
}
