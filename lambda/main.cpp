
#include "transpiler.h"
extern "C" {
Input* json_parse(const char* json_string);
Input* csv_parse(const char* csv_string);
Input* ini_file_parse(const char* ini_string);
Input* xml_parse(const char* xml_string);
Input* yaml_parse(const char* yaml_string);
Input* markdown_parse(const char* markdown_string);
}

void run_test_script(Runtime *runtime, const char *script, StrBuf *strbuf) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s", runtime->current_dir, script);
    Item ret = run_script_at(runtime, path);
    strbuf_append_format(strbuf, "\nScript '%s' result: ", script);
    print_item(strbuf, ret);
    strbuf_append_str(strbuf, "\n");
}

void test_input() {
    Input* json = json_parse("{\"a\":[\"name\", \"John\", \"age\", 30, \"city\", true]}");
    LambdaItem json_item; json_item.item = json->root;
    printf("JSON parse result: %llu, type: %d\n", json->root, json_item.type_id);
    print_item(json->sb, json->root);
    String *result = (String*)json->sb->str;
    printf("JSON parsed: %s\n", result->chars);

    // test csv parsing
    Input* csv_input = csv_parse("name,\"age\",city\nJohn, 30,\"New York, City\"\nJane, 25,\"Los Angeles\"");
    LambdaItem csv_item; csv_item.item = csv_input->root;
    printf("CSV parse result: %llu, type: %d\n", csv_input->root, csv_item.type_id);
    print_item(csv_input->sb, csv_input->root);
    String *csv_result = (String*)csv_input->sb->str;
    printf("CSV parsed: %s\n", csv_result->chars);

    // test ini file parsing with type detection
    Input* ini_file_input = ini_file_parse("[server]\nhost=localhost\nport=8080\ndebug=true\ntimeout=30.5\n\n[database]\nname=mydb\nconnections=100\nssl=false\nversion=1.2.3");
    LambdaItem ini_item; ini_item.item = ini_file_input->root;
    printf("INI file parse result: %llu, type: %d\n", ini_file_input->root, ini_item.type_id);
    print_item(ini_file_input->sb, ini_file_input->root);
    String *ini_file_result = (String*)ini_file_input->sb->str;
    printf("INI file parsed: %s\n", ini_file_result->chars);

    // test xml parsing
    Input* xml_input = xml_parse("<?xml version=\"1.0\"?>\n<bookstore>\n  <book id=\"1\" category=\"fiction\">\n    <title>Great Gatsby</title>\n    <author>F. Scott Fitzgerald</author>\n    <price>12.99</price>\n  </book>\n  <book id=\"2\" category=\"science\">\n    <title>Brief History of Time</title>\n    <author>Stephen Hawking</author>\n    <price>15.99</price>\n  </book>\n</bookstore>");
    LambdaItem xml_item; xml_item.item = xml_input->root;
    printf("XML parse result: %llu, type: %d\n", xml_input->root, xml_item.type_id);
    print_item(xml_input->sb, xml_input->root);
    String *xml_result = (String*)xml_input->sb->str;
    printf("XML parsed: %s\n", xml_result->chars);

    // test yaml parsing
    Input* yaml_input = yaml_parse("---\nname: John Doe\nage: 30\nactive: true\ncity: New York\naddress:\n  street: 123 Main St\n  zip: 10001\nhobbies:\n  - reading\n  - swimming\n  - coding\nscores:\n  - 85.5\n  - 92.0\n  - 78.3\nmetadata:\n  created: 2023-01-15\n  updated: null\n  tags: [important, personal]");
    LambdaItem yaml_item; yaml_item.item = yaml_input->root;
    printf("YAML parse result: %llu, type: %d\n", yaml_input->root, yaml_item.type_id);
    print_item(yaml_input->sb, yaml_input->root);
    String *yaml_result = (String*)yaml_input->sb->str;
    printf("YAML parsed: %s\n", yaml_result->chars);

    // test markdown parsing
    Input* markdown_input = markdown_parse("# Welcome to Markdown\n\nThis is a **bold** paragraph with *italic* text and `inline code`.\n\n## Features\n\n- First item\n- Second item with [a link](https://example.com)\n- Third item\n\n### Code Example\n\n```python\ndef hello_world():\n    print(\"Hello, World!\")\n    return True\n```\n\n---\n\nAnother paragraph after horizontal rule.");
    LambdaItem markdown_item; markdown_item.item = markdown_input->root;
    printf("Markdown parse result: %llu, type: %d\n", markdown_input->root, markdown_item.type_id);
    print_item(markdown_input->sb, markdown_input->root);
    String *markdown_result = (String*)markdown_input->sb->str;
    printf("Markdown parsed: %s\n", markdown_result->chars);
}

int main(void) {
    _Static_assert(sizeof(bool) == 1, "bool size == 1 byte");
    _Static_assert(sizeof(uint8_t) == 1, "uint8_t size == 1 byte");
    _Static_assert(sizeof(uint16_t) == 2, "uint16_t size == 2 bytes");
    _Static_assert(sizeof(uint32_t) == 4, "uint32_t size == 4 bytes");
    _Static_assert(sizeof(uint64_t) == 8, "uint64_t size == 8 bytes");
    _Static_assert(sizeof(int32_t) == 4, "int32_t size == 4 bytes");
    _Static_assert(sizeof(int64_t) == 8, "int64_t size == 8 bytes");
    _Static_assert(sizeof(Item) == sizeof(double), "Item size == double size");
    _Static_assert(sizeof(LambdaItem) == sizeof(Item), "LambdaItem size == Item size");
    LambdaItem itm = {.item = ITEM_ERROR};
    assert(itm.type_id == LMD_TYPE_ERROR);

    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "test/lambda/";
    StrBuf *strbuf = strbuf_new_cap(256);  Item ret;
    strbuf_append_str(strbuf, "Test result ===============\n");
    run_test_script(&runtime, "value.ls", strbuf);
    run_test_script(&runtime, "expr.ls", strbuf);
    run_test_script(&runtime, "box_unbox.ls", strbuf);
    run_test_script(&runtime, "func.ls", strbuf);
    run_test_script(&runtime, "mem.ls", strbuf);
    run_test_script(&runtime, "type.ls", strbuf);

    printf("%s", strbuf->str);
    strbuf_free(strbuf);
    runtime_cleanup(&runtime);

    // mpf_t f;
    // mpf_init(f);
    // mpf_set_str(f, "5e-2", 10);  // This works!
    // gmp_printf("f = %.10Ff\n", f);  // Output: f = 0.0500000000

    // mpf_set_str(f, "3.14159", 10); 
    // gmp_printf("f = %.10Ff\n", f);

    // mpf_clear(f);
    // printf("size of mpf_t: %zu\n", sizeof(mpf_t));  

    // test_input();
    return 0;
}