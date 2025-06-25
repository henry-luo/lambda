
#include "transpiler.h"
#include <gmp.h>

void run_test_script(Runtime *runtime, const char *script, StrBuf *strbuf) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s", runtime->current_dir, script);
    Item ret = run_script_at(runtime, path);
    strbuf_append_format(strbuf, "\nScript '%s' result: ", script);
    print_item(strbuf, ret);
    strbuf_append_str(strbuf, "\n");
}

Input* json_parse(const char* json_string);
Input* csv_parse(const char* csv_string);

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

    Input* json = json_parse("{\"a\":[\"name\", \"John\", \"age\", 30, \"city\", true]}");
    printf("JSON parse result: %llu, type: %d\n", json->root, ((LambdaItem)json->root).type_id);
    print_item(json->sb, json->root);
    String *result = (String*)json->sb->str;
    printf("JSON parsed: %s\n", result->chars);

    // test csv parsing
    Input* csv_input = csv_parse("name, age, city\nJohn, 30, New York\nJane, 25, Los Angeles");
    printf("CSV parse result: %llu, type: %d\n", csv_input->root, ((LambdaItem)csv_input->root).type_id);
    print_item(csv_input->sb, csv_input->root);
    String *csv_result = (String*)csv_input->sb->str;
    printf("CSV parsed: %s\n", csv_result->chars);
    return 0;
}