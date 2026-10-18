#ifndef TEST_MATH_FORMAT_RUNTIME_HPP
#define TEST_MATH_FORMAT_RUNTIME_HPP

#include "../lambda/lambda.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "../lib/stringbuf.h"

extern "C" Item fn_format2(Item item, Item type);

// D7.1.1: source round trips invoke the runtime, never evaluate scripts in IO.
struct MathFormatTestRuntime {
    Runtime runtime{};
    bool ready;
    MathFormatTestRuntime() {
        runtime_init(&runtime);
        // runtime_init creates metadata; an evaluation establishes the heap and TLS.
        ready = run_script_mir(&runtime, "null\n", (char*)"<math-format-test>", false) != nullptr;
    }
    ~MathFormatTestRuntime() { runtime_cleanup(&runtime); }

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
