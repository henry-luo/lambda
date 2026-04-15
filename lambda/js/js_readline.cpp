/**
 * js_readline.cpp — Node.js-style 'readline' module for LambdaJS
 *
 * Provides createInterface() for interactive line-by-line input.
 * Registered as built-in module 'readline' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstring>
#include <stdio.h>

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// =============================================================================
// readline.question(prompt, callback) — ask question, call back with answer
// =============================================================================

extern "C" Item js_readline_question(Item self, Item prompt_item, Item callback_item) {
    // print prompt
    if (get_type_id(prompt_item) == LMD_TYPE_STRING) {
        String* s = it2s(prompt_item);
        fwrite(s->chars, 1, s->len, stdout);
        fflush(stdout);
    }

    // read one line
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin)) {
        // trim trailing newline
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) len--;
        
        if (get_type_id(callback_item) == LMD_TYPE_FUNC) {
            Item answer = make_string_item(buf, (int)len);
            js_call_function(callback_item, ItemNull, &answer, 1);
        }
    }
    return self;
}

// =============================================================================
// readline.close() — close the interface
// =============================================================================

extern "C" Item js_readline_close(Item self) {
    // emit 'close' event
    Item on_close = js_property_get(self, make_string_item("__on_close__"));
    if (get_type_id(on_close) == LMD_TYPE_FUNC) {
        js_call_function(on_close, ItemNull, NULL, 0);
    }
    return self;
}

// on(event, callback)
extern "C" Item js_readline_on(Item self, Item event_item, Item callback_item) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback_item);
    return self;
}

// =============================================================================
// readline.createInterface(options) → interface object
// options: { input, output, prompt }
// =============================================================================

extern "C" Item js_readline_createInterface(Item options_item) {
    Item rl = js_new_object();

    // extract prompt from options if available
    Item prompt_val = make_string_item("> ");
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item p = js_property_get(options_item, make_string_item("prompt"));
        if (get_type_id(p) == LMD_TYPE_STRING) prompt_val = p;
    }
    js_property_set(rl, make_string_item("__prompt__"), prompt_val);

    // methods
    js_property_set(rl, make_string_item("question"),
                    js_new_function((void*)js_readline_question, 3));
    js_property_set(rl, make_string_item("close"),
                    js_new_function((void*)js_readline_close, 1));
    js_property_set(rl, make_string_item("on"),
                    js_new_function((void*)js_readline_on, 3));

    return rl;
}

// =============================================================================
// readline Module Namespace
// =============================================================================

static Item readline_namespace = {0};

extern "C" Item js_get_readline_namespace(void) {
    if (readline_namespace.item != 0) return readline_namespace;

    readline_namespace = js_new_object();

    Item key = make_string_item("createInterface");
    Item fn = js_new_function((void*)js_readline_createInterface, 1);
    js_property_set(readline_namespace, key, fn);

    Item default_key = make_string_item("default");
    js_property_set(readline_namespace, default_key, readline_namespace);

    return readline_namespace;
}

extern "C" void js_readline_reset(void) {
    readline_namespace = (Item){0};
}
