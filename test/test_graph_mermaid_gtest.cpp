#include <gtest/gtest.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "../lambda/input/input.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/file.h"
#include "../lib/mempool.h"
#include "../lib/string.h"

static const char* MERMAID_CORPUS_DIR = "test/lambda/graph/mermaid";
static const char* MERMAID_MANIFEST = "test/lambda/graph/mermaid/manifest.mark";

static const char* item_text(const ItemReader& item) {
    if (item.isString()) return item.cstring();
    if (item.isSymbol()) {
        Symbol* symbol = item.asSymbol();
        return symbol ? symbol->chars : nullptr;
    }
    return nullptr;
}

static const char* attr_text(const ElementReader& element, const char* key) {
    return item_text(element.get_attr(key));
}

static int64_t count_tag_recursive(const ElementReader& parent, const char* tag) {
    int64_t count = 0;
    ElementReader child;
    auto children = parent.childElements();
    while (children.next(&child)) {
        if (child.hasTag(tag)) count++;
        count += count_tag_recursive(child, tag);
    }
    return count;
}

static ElementReader find_by_identity(const ElementReader& parent,
                                      const ElementReader& expected) {
    const char* tag = expected.tagName();
    const char* expected_id = attr_text(expected, "id");
    const char* expected_from = attr_text(expected, "from");
    const char* expected_to = attr_text(expected, "to");
    const char* expected_class = attr_text(expected, "class");
    const char* expected_target_kind = attr_text(expected, "target-kind");

    ElementReader child;
    auto children = parent.childElements();
    while (children.next(&child)) {
        bool tag_matches = tag && child.hasTag(tag);
        bool identity_matches = tag_matches;
        if (identity_matches && expected_id) {
            const char* actual_id = child.get_attr_string("id");
            identity_matches = actual_id && strcmp(actual_id, expected_id) == 0;
        }
        if (identity_matches && expected_from) {
            const char* actual_from = child.get_attr_string("from");
            identity_matches = actual_from && strcmp(actual_from, expected_from) == 0;
        }
        if (identity_matches && expected_to) {
            const char* actual_to = child.get_attr_string("to");
            identity_matches = actual_to && strcmp(actual_to, expected_to) == 0;
        }
        if (identity_matches && expected_class) {
            const char* actual_class = child.get_attr_string("class");
            identity_matches = actual_class && strcmp(actual_class, expected_class) == 0;
        }
        if (identity_matches && expected_target_kind) {
            const char* actual_target_kind = child.get_attr_string("target-kind");
            identity_matches = actual_target_kind &&
                strcmp(actual_target_kind, expected_target_kind) == 0;
        }
        if (identity_matches) return child;

        ElementReader nested = find_by_identity(child, expected);
        if (nested.isValid()) return nested;
    }
    return ElementReader();
}

static void expect_matching_attributes(const ElementReader& actual,
                                       const ElementReader& expected) {
    const char* key = nullptr;
    ItemReader expected_value;
    auto attrs = expected.attrs();
    while (attrs.next(&key, &expected_value)) {
        ItemReader actual_value = actual.get_attr(key);
        if (expected_value.isString() || expected_value.isSymbol()) {
            const char* actual_text = item_text(actual_value);
            ASSERT_NE(actual_text, nullptr) << "attribute " << key;
            EXPECT_STREQ(actual_text, item_text(expected_value)) << "attribute " << key;
        } else if (expected_value.isInt()) {
            ASSERT_TRUE(actual_value.isInt()) << "attribute " << key;
            EXPECT_EQ(actual_value.asInt(), expected_value.asInt()) << "attribute " << key;
        } else if (expected_value.isBool()) {
            ASSERT_TRUE(actual_value.isBool()) << "attribute " << key;
            EXPECT_EQ(actual_value.asBool(), expected_value.asBool()) << "attribute " << key;
        }
    }
}

static void expect_source_span(const ElementReader& element) {
    ItemReader start = element.get_attr("source-start");
    ItemReader end = element.get_attr("source-end");
    ItemReader line = element.get_attr("source-line");
    ItemReader column = element.get_attr("source-column");
    ASSERT_TRUE(start.isInt());
    ASSERT_TRUE(end.isInt());
    ASSERT_TRUE(line.isInt());
    ASSERT_TRUE(column.isInt());
    EXPECT_LT(start.asInt(), end.asInt());
    EXPECT_GT(line.asInt(), 0);
    EXPECT_GT(column.asInt(), 0);
}

static Pool* g_type_pool = nullptr;
static String* g_mark_type = nullptr;
static String* g_graph_type = nullptr;
static String* g_mermaid_flavor = nullptr;
static Item g_manifest_root = {.item = 0};

static ElementReader manifest_case_at(int wanted_index) {
    ElementReader manifest(g_manifest_root);
    ElementReader test_case;
    auto cases = manifest.childElements();
    int case_index = 0;
    while (cases.next(&test_case)) {
        if (!test_case.hasTag("case")) continue;
        if (case_index == wanted_index) return test_case;
        case_index++;
    }
    return ElementReader();
}

static void expect_manifest_policy(const ElementReader& test_case) {
    const char* origin = attr_text(test_case, "origin");
    ASSERT_NE(origin, nullptr);
    ASSERT_NE(attr_text(test_case, "status"), nullptr);
    ASSERT_NE(attr_text(test_case, "policy"), nullptr);
    ASSERT_NE(attr_text(test_case, "features"), nullptr);
    if (strcmp(origin, "mermaid") == 0) {
        ASSERT_NE(attr_text(test_case, "upstream-file"), nullptr);
        ASSERT_NE(attr_text(test_case, "upstream-test"), nullptr);
        EXPECT_STREQ(attr_text(test_case, "reference-version"), "11.16.0");
    }
    const char* policy = attr_text(test_case, "policy");
    if (policy && strcmp(policy, "scene-semantic") == 0) {
        const char* expected_scene = attr_text(test_case, "expected-scene");
        ASSERT_NE(expected_scene, nullptr);
        char* expected_path = file_path_join(MERMAID_CORPUS_DIR, expected_scene);
        ASSERT_NE(expected_path, nullptr);
        char* expected_source = read_text_file(expected_path);
        free(expected_path);
        ASSERT_NE(expected_source, nullptr) << "missing expected scene " << expected_scene;
        free(expected_source);
    }
}

static void run_manifest_case(int case_index) {
    ElementReader test_case = manifest_case_at(case_index);
    ASSERT_TRUE(test_case.isValid());
    expect_manifest_policy(test_case);

    const char* source_name = attr_text(test_case, "source");
    ASSERT_NE(source_name, nullptr);
    char* source_path = file_path_join(MERMAID_CORPUS_DIR, source_name);
    ASSERT_NE(source_path, nullptr);
    char* source = read_text_file(source_path);
    free(source_path);
    ASSERT_NE(source, nullptr);

    Input* graph_input = input_from_source(source, nullptr, g_graph_type, g_mermaid_flavor);
    free(source);
    ASSERT_NE(graph_input, nullptr);
    ASSERT_EQ(graph_input->root.type_id(), LMD_TYPE_ELEMENT);
    ElementReader graph(graph_input->root);
    ASSERT_TRUE(graph.hasTag("graph"));
    EXPECT_STREQ(graph.get_attr_string("ir-stage"), "source");

    ElementReader expected = test_case.findChildElement("expect");
    ASSERT_TRUE(expected.isValid());
    const char* direction = attr_text(expected, "direction");
    const char* kind = attr_text(expected, "kind");
    const char* status = attr_text(expected, "status");
    if (direction) EXPECT_STREQ(graph.get_attr_string("direction"), direction);
    if (kind) EXPECT_STREQ(graph.get_attr_string("kind"), kind);
    if (status) EXPECT_STREQ(graph.get_attr_string("status"), status);

    // source-stage declarations may repeat ids; canonical node counts belong to normalization.
    int64_t expected_source_nodes = expected.get_int_attr(
        "source-nodes", expected.get_int_attr("nodes", 0));
    EXPECT_EQ(count_tag_recursive(graph, "node"), expected_source_nodes);
    EXPECT_EQ(count_tag_recursive(graph, "edge"), expected.get_int_attr("edges", 0));
    EXPECT_EQ(count_tag_recursive(graph, "subgraph"), expected.get_int_attr("subgraphs", 0));
    EXPECT_EQ(count_tag_recursive(graph, "style-rule"), expected.get_int_attr("style-rules", 0));
    EXPECT_EQ(count_tag_recursive(graph, "class-assignment"),
              expected.get_int_attr("class-assignments", 0));
    EXPECT_EQ(count_tag_recursive(graph, "style-assignment"),
              expected.get_int_attr("style-assignments", 0));
    EXPECT_EQ(count_tag_recursive(graph, "interaction"),
              expected.get_int_attr("interactions", 0));
    EXPECT_EQ(count_tag_recursive(graph, "annotation"),
              expected.get_int_attr("annotations", 0));
    EXPECT_EQ(count_tag_recursive(graph, "edge-property"),
              expected.get_int_attr("edge-properties", 0));
    EXPECT_EQ(count_tag_recursive(graph, "front-matter"),
              expected.get_int_attr("front-matters", 0));
    EXPECT_EQ(count_tag_recursive(graph, "init"),
              expected.get_int_attr("init-directives", 0));

    ElementReader expected_item;
    auto expected_items = expected.childElements();
    while (expected_items.next(&expected_item)) {
        ElementReader actual_item = find_by_identity(graph, expected_item);
        ASSERT_TRUE(actual_item.isValid()) << "missing semantic " << expected_item.tagName();
        expect_matching_attributes(actual_item, expected_item);
        if (expected_item.hasTag("node") || expected_item.hasTag("edge") ||
            expected_item.hasTag("subgraph") || expected_item.hasTag("title") ||
            expected_item.hasTag("description") ||
            expected_item.hasTag("style-assignment") ||
            expected_item.hasTag("style-rule") ||
            expected_item.hasTag("class-assignment") ||
            expected_item.hasTag("interaction") ||
            expected_item.hasTag("annotation") ||
            expected_item.hasTag("edge-property")) {
            expect_source_span(actual_item);
        }
    }
}

class GraphMermaidCorpusTest : public ::testing::Test {};

class GraphMermaidCaseTest : public GraphMermaidCorpusTest {
public:
    explicit GraphMermaidCaseTest(int case_index) : case_index_(case_index) {}
    void TestBody() override { run_manifest_case(case_index_); }

private:
    int case_index_;
};

static void sanitize_test_name(const char* id, char* name, size_t capacity) {
    if (!name || capacity == 0) return;
    size_t index = 0;
    for (const char* cursor = id; cursor && *cursor && index + 1 < capacity; cursor++) {
        unsigned char ch = (unsigned char)*cursor;
        name[index++] = (char)(isalnum(ch) ? ch : '_');
    }
    name[index] = '\0';
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    g_type_pool = pool_create();
    if (!g_type_pool) return 1;
    g_mark_type = create_string(g_type_pool, "mark");
    g_graph_type = create_string(g_type_pool, "graph");
    g_mermaid_flavor = create_string(g_type_pool, "mermaid");

    char* manifest_source = read_text_file(MERMAID_MANIFEST);
    if (!manifest_source) return 1;
    Input* manifest_input = input_from_source(manifest_source, nullptr, g_mark_type, nullptr);
    free(manifest_source);
    if (!manifest_input || manifest_input->root.type_id() != LMD_TYPE_ELEMENT) return 1;
    g_manifest_root = manifest_input->root;

    ElementReader manifest(g_manifest_root);
    if (!manifest.hasTag("mermaid-suite")) return 1;
    ElementReader test_case;
    auto cases = manifest.childElements();
    int case_index = 0;
    while (cases.next(&test_case)) {
        if (!test_case.hasTag("case")) continue;
        const char* id = attr_text(test_case, "id");
        if (!id || !id[0]) return 1;
        char test_name[192];
        sanitize_test_name(id, test_name, sizeof(test_name));
        int registered_index = case_index;
        ::testing::RegisterTest(
            "GraphMermaidCorpus", test_name, nullptr, id, __FILE__, __LINE__,
            [registered_index]() -> GraphMermaidCorpusTest* {
                return new GraphMermaidCaseTest(registered_index);
            });
        case_index++;
    }

    int result = RUN_ALL_TESTS();
    InputManager::destroy_global();
    pool_destroy(g_type_pool);
    return result;
}
