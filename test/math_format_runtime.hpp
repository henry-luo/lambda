#ifndef TEST_MATH_FORMAT_RUNTIME_HPP
#define TEST_MATH_FORMAT_RUNTIME_HPP

#include "../lambda/lambda.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "lib/stringbuf.h"

extern "C" Item fn_format2(Item item, Item type);

// D9.1.1: source round trips invoke the runtime, never evaluate scripts in IO.
struct MathFormatTestRuntime {
    Runtime runtime{};
    bool ready;
    MathFormatTestRuntime() {
        lambda_home_init();
        runtime_init(&runtime);
        // runtime_init creates metadata; an evaluation establishes the heap and TLS.
        ready = run_script_mir(&runtime, "null\n", (char*)"<math-format-test>", false) != nullptr;
    }
    ~MathFormatTestRuntime() { runtime_cleanup(&runtime); }

    String* roundtrip_tex_math(const char* source, String* type, String* flavor) {
        if (!ready) return nullptr;
        // D10.2.2v2: TeX math uses the runtime parser before Script formatting.
        RootFrame roots(3);
        Rooted<String*> text(roots, heap_strcpy(source, strlen(source)));
        Rooted<Symbol*> notation(roots, heap_create_symbol(type->chars, type->len));
        if (!text.get() || !notation.get()) return nullptr;
        Rooted<Item> parsed(roots, fn_parse2({.item = s2it(text.get())}, {.item = y2it(notation.get())}));
        if (get_type_id(parsed.get()) != LMD_TYPE_ELEMENT) return nullptr;
        return format(parsed.get(), type, flavor, runtime_heap(&runtime)->pool);
    }

    String* format(Item source, String* type, String* flavor, Pool* pool) {
        if (!ready) return nullptr;
        if (strcmp(type->chars, "math") != 0) return format_data(source, type, flavor, pool);
        StringBuf* target = stringbuf_new(pool);
        stringbuf_append_all(target, 2, "math-", flavor && flavor->len ? flavor->chars : "latex");
        RootFrame roots(2);
        Rooted<Item> source_root(roots, source);
        // s2it evaluates its argument twice; detach the buffer before tagging.
        String* target_string = stringbuf_to_string(target);
        Rooted<Item> result(roots, fn_format2(source_root.get(), {.item = s2it(target_string)}));
        if (get_type_id(result.get()) != LMD_TYPE_STRING) return nullptr;
        String* value = result.get().get_string();
        return string_from_strview(strview_init(value->chars, value->len), pool);
    }
};
#endif
