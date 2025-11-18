#include <gtest/gtest.h>
#include "../lambda/mark_reader.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lib/mempool.h"
#include <cmath>
#include <string>
#include <set>

// Test fixture for MarkReader tests
class MarkReaderTest : public ::testing::Test {
protected:
    Input* input;
    MarkBuilder* builder;

    void SetUp() override {
        input = InputManager::create_input(nullptr);
        builder = new MarkBuilder(input);
    }

    void TearDown() override {
        delete builder;
    }
};

// ==============================================================================
// ItemReader Basic Tests
// ==============================================================================

TEST_F(MarkReaderTest, ItemReaderNull) {
    Item null_item = builder->createNull();
    ItemReader reader(null_item.to_const());

    EXPECT_TRUE(reader.isNull());
    EXPECT_FALSE(reader.isString());
    EXPECT_FALSE(reader.isInt());
    EXPECT_FALSE(reader.isFloat());
    EXPECT_FALSE(reader.isBool());
    EXPECT_FALSE(reader.isElement());
    EXPECT_FALSE(reader.isMap());
    EXPECT_FALSE(reader.isArray());
}

TEST_F(MarkReaderTest, ItemReaderString) {
    Item str_item = builder->createStringItem("Hello, World!");
    ItemReader reader(str_item.to_const());

    EXPECT_FALSE(reader.isNull());
    EXPECT_TRUE(reader.isString());
    EXPECT_FALSE(reader.isInt());

    const char* str = reader.cstring();
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str, "Hello, World!");

    String* string_ptr = reader.asString();
    ASSERT_NE(string_ptr, nullptr);
    EXPECT_EQ(string_ptr->len, 13);
}

TEST_F(MarkReaderTest, ItemReaderInt) {
    Item int_item = builder->createInt(42);
    ItemReader reader(int_item.to_const());

    EXPECT_TRUE(reader.isInt());
    EXPECT_FALSE(reader.isFloat());

    int64_t val = reader.asInt();
    EXPECT_EQ(val, 42);

    int32_t val32 = reader.asInt32();
    EXPECT_EQ(val32, 42);
}

TEST_F(MarkReaderTest, ItemReaderFloat) {
    Item float_item = builder->createFloat(3.14159);
    ItemReader reader(float_item.to_const());

    EXPECT_TRUE(reader.isFloat());
    EXPECT_FALSE(reader.isInt());

    double val = reader.asFloat();
    EXPECT_NEAR(val, 3.14159, 0.00001);
}

TEST_F(MarkReaderTest, ItemReaderBool) {
    Item true_item = builder->createBool(true);
    Item false_item = builder->createBool(false);

    ItemReader true_reader(true_item.to_const());
    ItemReader false_reader(false_item.to_const());

    EXPECT_TRUE(true_reader.isBool());
    EXPECT_TRUE(false_reader.isBool());

    EXPECT_TRUE(true_reader.asBool());
    EXPECT_FALSE(false_reader.asBool());
}

TEST_F(MarkReaderTest, ItemReaderTypeMismatch) {
    Item str_item = builder->createStringItem("test");
    ItemReader reader(str_item.to_const());

    // Type mismatches should return defaults
    EXPECT_EQ(reader.asInt(), 0);
    EXPECT_EQ(reader.asFloat(), 0.0);
    EXPECT_FALSE(reader.asBool());
}

// ==============================================================================
// ArrayReader Tests
// ==============================================================================

TEST_F(MarkReaderTest, ArrayReaderBasic) {
    Item array_item = builder->array()
        .append((int64_t)1)
        .append((int64_t)2)
        .append((int64_t)3)
        .final();

    ItemReader item_reader(array_item.to_const());
    EXPECT_TRUE(item_reader.isArray());

    ArrayReader arr = item_reader.asArray();
    EXPECT_TRUE(arr.isValid());
    EXPECT_EQ(arr.length(), 3);
    EXPECT_FALSE(arr.isEmpty());

    ItemReader first = arr.get(0);
    EXPECT_TRUE(first.isInt());
    EXPECT_EQ(first.asInt(), 1);

    ItemReader second = arr.get(1);
    EXPECT_EQ(second.asInt(), 2);

    ItemReader third = arr.get(2);
    EXPECT_EQ(third.asInt(), 3);
}

TEST_F(MarkReaderTest, ArrayReaderEmpty) {
    Item array_item = builder->array().final();

    ArrayReader arr = ArrayReader::fromItem(array_item);
    EXPECT_TRUE(arr.isValid());
    EXPECT_EQ(arr.length(), 0);
    EXPECT_TRUE(arr.isEmpty());
}

TEST_F(MarkReaderTest, ArrayReaderOutOfBounds) {
    Item array_item = builder->array()
        .append("a")
        .append("b")
        .final();

    ArrayReader arr = ArrayReader::fromItem(array_item);

    ItemReader invalid = arr.get(10);  // Out of bounds
    EXPECT_TRUE(invalid.isNull());

    ItemReader negative = arr.get(-1);  // Negative index
    EXPECT_TRUE(negative.isNull());
}

TEST_F(MarkReaderTest, ArrayReaderIteration) {
    Item array_item = builder->array()
        .append("apple")
        .append("banana")
        .append("cherry")
        .final();

    ArrayReader arr = ArrayReader::fromItem(array_item);

    ArrayReader::Iterator iter = arr.items();
    ItemReader item;
    int count = 0;

    while (iter.next(&item)) {
        EXPECT_TRUE(item.isString());
        count++;
    }

    EXPECT_EQ(count, 3);

    // Test reset
    iter.reset();
    EXPECT_TRUE(iter.next(&item));
    EXPECT_STREQ(item.cstring(), "apple");
}

TEST_F(MarkReaderTest, ArrayReaderMixedTypes) {
    Item array_item = builder->array()
        .append((int64_t)42)
        .append("string")
        .append(3.14)
        .append(true)
        .final();

    ArrayReader arr = ArrayReader::fromItem(array_item);

    ItemReader item0 = arr.get(0);
    EXPECT_TRUE(item0.isInt());
    EXPECT_EQ(item0.asInt(), 42);

    ItemReader item1 = arr.get(1);
    EXPECT_TRUE(item1.isString());
    EXPECT_STREQ(item1.cstring(), "string");

    ItemReader item2 = arr.get(2);
    EXPECT_TRUE(item2.isFloat());
    EXPECT_NEAR(item2.asFloat(), 3.14, 0.01);

    ItemReader item3 = arr.get(3);
    EXPECT_TRUE(item3.isBool());
    EXPECT_TRUE(item3.asBool());
}

// ==============================================================================
// MapReader Tests
// ==============================================================================

TEST_F(MarkReaderTest, MapReaderBasic) {
    Item map_item = builder->map()
        .put("name", "John")
        .put("age", (int64_t)30)
        .put("active", true)
        .final();

    ItemReader item_reader(map_item.to_const());
    EXPECT_TRUE(item_reader.isMap());

    MapReader map = item_reader.asMap();
    EXPECT_TRUE(map.isValid());
    EXPECT_EQ(map.size(), 3);
    EXPECT_FALSE(map.isEmpty());

    EXPECT_TRUE(map.has("name"));
    EXPECT_TRUE(map.has("age"));
    EXPECT_TRUE(map.has("active"));
    EXPECT_FALSE(map.has("nonexistent"));

    ItemReader name = map.get("name");
    EXPECT_TRUE(name.isString());
    EXPECT_STREQ(name.cstring(), "John");

    ItemReader age = map.get("age");
    EXPECT_TRUE(age.isInt());
    EXPECT_EQ(age.asInt(), 30);

    ItemReader active = map.get("active");
    EXPECT_TRUE(active.isBool());
    EXPECT_TRUE(active.asBool());
}

TEST_F(MarkReaderTest, MapReaderEmpty) {
    Item map_item = builder->map().final();

    MapReader map = MapReader::fromItem(map_item);
    EXPECT_TRUE(map.isValid());
    EXPECT_EQ(map.size(), 0);
    EXPECT_TRUE(map.isEmpty());
}

TEST_F(MarkReaderTest, MapReaderMissingKey) {
    Item map_item = builder->map()
        .put("existing", "value")
        .final();

    MapReader map = MapReader::fromItem(map_item);

    ItemReader missing = map.get("missing");
    EXPECT_TRUE(missing.isNull());
}

TEST_F(MarkReaderTest, MapReaderKeyIteration) {
    Item map_item = builder->map()
        .put("key1", "val1")
        .put("key2", "val2")
        .put("key3", "val3")
        .final();

    MapReader map = MapReader::fromItem(map_item);

    MapReader::KeyIterator iter = map.keys();
    const char* key;
    int count = 0;
    std::set<std::string> keys;

    while (iter.next(&key)) {
        keys.insert(key);
        count++;
    }

    EXPECT_EQ(count, 3);
    EXPECT_TRUE(keys.count("key1") > 0);
    EXPECT_TRUE(keys.count("key2") > 0);
    EXPECT_TRUE(keys.count("key3") > 0);
}

TEST_F(MarkReaderTest, MapReaderValueIteration) {
    Item map_item = builder->map()
        .put("a", (int64_t)1)
        .put("b", (int64_t)2)
        .put("c", (int64_t)3)
        .final();

    MapReader map = MapReader::fromItem(map_item);

    MapReader::ValueIterator iter = map.values();
    ItemReader value;
    int sum = 0;

    while (iter.next(&value)) {
        EXPECT_TRUE(value.isInt());
        sum += (int)value.asInt();
    }

    EXPECT_EQ(sum, 6);  // 1 + 2 + 3
}

TEST_F(MarkReaderTest, MapReaderEntryIteration) {
    Item map_item = builder->map()
        .put("x", (int64_t)10)
        .put("y", (int64_t)20)
        .final();

    MapReader map = MapReader::fromItem(map_item);

    MapReader::EntryIterator iter = map.entries();
    const char* key;
    ItemReader value;
    int count = 0;

    while (iter.next(&key, &value)) {
        EXPECT_TRUE(value.isInt());
        count++;
    }

    EXPECT_EQ(count, 2);
}

TEST_F(MarkReaderTest, MapReaderNestedStructures) {
    Item nested_array = builder->array()
        .append((int64_t)1)
        .append((int64_t)2)
        .final();

    Item nested_map = builder->map()
        .put("inner", "value")
        .final();

    Item map_item = builder->map()
        .put("array", nested_array)
        .put("map", nested_map)
        .final();

    MapReader map = MapReader::fromItem(map_item);

    ItemReader array_item = map.get("array");
    EXPECT_TRUE(array_item.isArray());
    ArrayReader arr = array_item.asArray();
    EXPECT_EQ(arr.length(), 2);

    ItemReader map_item_reader = map.get("map");
    EXPECT_TRUE(map_item_reader.isMap());
    MapReader inner = map_item_reader.asMap();
    EXPECT_TRUE(inner.has("inner"));
}

// ==============================================================================
// ElementReaderWrapper Tests
// ==============================================================================

TEST_F(MarkReaderTest, ElementReaderBasic) {
    Item elem_item = builder->element("div")
        .attr("class", "container")
        .text("Hello")
        .final();

    ItemReader item_reader(elem_item.to_const());
    EXPECT_TRUE(item_reader.isElement());

    ElementReader elem = item_reader.asElement();
    EXPECT_TRUE(elem.isValid());
    EXPECT_STREQ(elem.tagName(), "div");
    EXPECT_TRUE(elem.hasTag("div"));
    EXPECT_FALSE(elem.hasTag("span"));

    EXPECT_GT(elem.childCount(), 0);
    EXPECT_FALSE(elem.isEmpty());
}

TEST_F(MarkReaderTest, ElementReaderChildren) {
    Item child1 = builder->element("p").text("Para 1").final();
    Item child2 = builder->element("p").text("Para 2").final();

    Item elem_item = builder->element("div")
        .child(child1)
        .child(child2)
        .final();

    ElementReader elem(elem_item);
    EXPECT_EQ(elem.childCount(), 2);
    EXPECT_TRUE(elem.hasChildElements());

    ItemReader first_child = elem.childAt(0);
    EXPECT_TRUE(first_child.isElement());

    ElementReader first_elem = first_child.asElement();
    EXPECT_STREQ(first_elem.tagName(), "p");
}

TEST_F(MarkReaderTest, ElementReaderChildIteration) {
    Item elem_item = builder->element("ul")
        .child(builder->element("li").text("Item 1").final())
        .child(builder->element("li").text("Item 2").final())
        .child(builder->element("li").text("Item 3").final())
        .final();

    ElementReader elem(elem_item);

    ElementReader::ChildIterator iter = elem.children();
    ItemReader child;
    int count = 0;

    while (iter.next(&child)) {
        EXPECT_TRUE(child.isElement());
        count++;
    }

    EXPECT_EQ(count, 3);
}

TEST_F(MarkReaderTest, ElementReaderElementChildIteration) {
    Item elem_item = builder->element("div")
        .text("Text node")
        .child(builder->element("span").text("Span").final())
        .text("More text")
        .child(builder->element("p").text("Para").final())
        .final();

    ElementReader elem(elem_item);

    ElementReader::ElementChildIterator iter = elem.childElements();
    ElementReader child_elem;
    int elem_count = 0;

    while (iter.next(&child_elem)) {
        EXPECT_TRUE(child_elem.isValid());
        elem_count++;
    }

    EXPECT_EQ(elem_count, 2);  // Only span and p, not text nodes
}

TEST_F(MarkReaderTest, ElementReaderFindChild) {
    Item h1_item = builder->element("h1").text("Title").final();
    Item p_item = builder->element("p").text("Content").final();

    Item elem_item = builder->element("article")
        .child(h1_item)
        .child(p_item)
        .final();

    ElementReader elem(elem_item);

    ItemReader found_h1 = elem.findChild("h1");
    EXPECT_TRUE(found_h1.isElement());

    ElementReader h1_elem = found_h1.asElement();
    EXPECT_STREQ(h1_elem.tagName(), "h1");

    ElementReader found_p = elem.findChildElement("p");
    EXPECT_TRUE(found_p.isValid());
    EXPECT_STREQ(found_p.tagName(), "p");

    ItemReader not_found = elem.findChild("div");
    EXPECT_TRUE(not_found.isNull());
}

TEST_F(MarkReaderTest, ElementReaderTextOnly) {
    Item text_only = builder->element("p")
        .text("Just text")
        .final();

    ElementReader elem(text_only);
    EXPECT_TRUE(elem.isTextOnly());

    Item with_child = builder->element("div")
        .text("Text")
        .child(builder->element("span").final())
        .final();

    ElementReader elem2(with_child);
    EXPECT_FALSE(elem2.isTextOnly());
}

TEST_F(MarkReaderTest, ElementReaderEmpty) {
    Item empty_elem = builder->element("div").final();

    ElementReader elem(empty_elem);
    EXPECT_TRUE(elem.isEmpty());
    EXPECT_EQ(elem.childCount(), 0);
    EXPECT_FALSE(elem.hasChildElements());
}

// ==============================================================================
// AttributeReader Tests
// ==============================================================================

TEST_F(MarkReaderTest, AttributeReaderBasic) {
    Item elem_item = builder->element("div")
        .attr("id", "main")
        .attr("class", "container")
        .attr("width", (int64_t)100)
        .final();

    ElementReader elem(elem_item);

    EXPECT_TRUE(elem.isValid());
    EXPECT_TRUE(elem.has_attr("id"));
    EXPECT_TRUE(elem.has_attr("class"));
    EXPECT_TRUE(elem.has_attr("width"));
    EXPECT_FALSE(elem.has_attr("height"));

    const char* id = elem.get_attr_string("id");
    ASSERT_NE(id, nullptr);
    EXPECT_STREQ(id, "main");

    const char* cls = elem.get_attr_string("class");
    ASSERT_NE(cls, nullptr);
    EXPECT_STREQ(cls, "container");
}

TEST_F(MarkReaderTest, AttributeReaderIteration) {
    Item elem_item = builder->element("a")
        .attr("href", "https://example.com")
        .attr("target", "_blank")
        .attr("rel", "noopener")
        .final();

    ElementReader elem(elem_item);

    // Iterate through attributes manually using shape
    const TypeMap* map_type = (const TypeMap*)elem.element()->type;
    const ShapeEntry* field = map_type->shape;
    int count = 0;

    while (field) {
        const char* key = field->name->str;
        ItemReader value = elem.get_attr(key);
        EXPECT_NE(key, nullptr);
        count++;
        field = field->next;
    }

    EXPECT_EQ(count, 3);
}

// ==============================================================================
// MarkReader Document Tests
// ==============================================================================

TEST_F(MarkReaderTest, MarkReaderBasic) {
    Item root = builder->element("html")
        .child(builder->element("body").text("Content").final())
        .final();

    MarkReader reader(root);

    ItemReader root_item = reader.getRoot();
    EXPECT_TRUE(root_item.isElement());

    ElementReader html = root_item.asElement();
    EXPECT_STREQ(html.tagName(), "html");
}

TEST_F(MarkReaderTest, MarkReaderFindAll) {
    Item p1 = builder->element("p").text("Para 1").final();
    Item p2 = builder->element("p").text("Para 2").final();
    Item div = builder->element("div").text("Not a p").final();

    Item root = builder->element("body")
        .child(p1)
        .child(div)
        .child(p2)
        .final();

    MarkReader reader(root);

    MarkReader::ElementIterator iter = reader.findAll("p");
    ItemReader found;
    int count = 0;

    while (iter.next(&found)) {
        EXPECT_TRUE(found.isElement());
        ElementReader elem = found.asElement();
        EXPECT_STREQ(elem.tagName(), "p");
        count++;
    }

    EXPECT_EQ(count, 2);
}

// ==============================================================================
// Edge Cases and Error Handling
// ==============================================================================

TEST_F(MarkReaderTest, InvalidElementReader) {
    ElementReader invalid;
    EXPECT_FALSE(invalid.isValid());
    EXPECT_EQ(invalid.tagName(), nullptr);
    EXPECT_EQ(invalid.childCount(), 0);
    EXPECT_TRUE(invalid.isEmpty());
}

TEST_F(MarkReaderTest, InvalidMapReader) {
    MapReader invalid;
    EXPECT_FALSE(invalid.isValid());
    EXPECT_EQ(invalid.size(), 0);
    EXPECT_TRUE(invalid.isEmpty());
}

TEST_F(MarkReaderTest, InvalidArrayReader) {
    ArrayReader invalid;
    EXPECT_FALSE(invalid.isValid());
    EXPECT_EQ(invalid.length(), 0);
    EXPECT_TRUE(invalid.isEmpty());
}

TEST_F(MarkReaderTest, CopySemantics) {
    Item str_item = builder->createStringItem("test");
    ItemReader reader1(str_item.to_const());

    // Copy constructor
    ItemReader reader2 = reader1;
    EXPECT_TRUE(reader2.isString());
    EXPECT_STREQ(reader2.cstring(), "test");

    // Copy assignment
    ItemReader reader3(builder->createNull().to_const());
    reader3 = reader1;
    EXPECT_TRUE(reader3.isString());
    EXPECT_STREQ(reader3.cstring(), "test");
}

TEST_F(MarkReaderTest, ComplexNestedDocument) {
    Item article = builder->element("article")
        .attr("id", "main-article")
        .child(
            builder->element("header")
                .child(builder->element("h1").text("Title").final())
                .final()
        )
        .child(
            builder->element("section")
                .child(builder->element("p").text("Paragraph 1").final())
                .child(builder->element("p").text("Paragraph 2").final())
                .final()
        )
        .child(
            builder->element("footer")
                .child(builder->element("small").text("Copyright 2025").final())
                .final()
        )
        .final();

    MarkReader reader(article);
    ItemReader root = reader.getRoot();

    EXPECT_TRUE(root.isElement());
    ElementReader article_elem = root.asElement();
    EXPECT_STREQ(article_elem.tagName(), "article");
    EXPECT_EQ(article_elem.childCount(), 3);

    // Find header
    ElementReader header = article_elem.findChildElement("header");
    EXPECT_TRUE(header.isValid());
    EXPECT_TRUE(header.hasChildElements());

    // Find section with paragraphs
    ElementReader section = article_elem.findChildElement("section");
    EXPECT_TRUE(section.isValid());

    ElementReader::ElementChildIterator iter = section.childElements();
    ElementReader p_elem;
    int p_count = 0;

    while (iter.next(&p_elem)) {
        EXPECT_STREQ(p_elem.tagName(), "p");
        p_count++;
    }

    EXPECT_EQ(p_count, 2);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
