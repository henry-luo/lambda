/**
 * js_dom_adapter.cpp — JS-side adapter for the DOM core (F25 / ES33).
 *
 * The DOM core in lambda/dom/ is realm-neutral: it drives Radiant's tree and is
 * reachable from both page JavaScript and Lambda script. Where it needs a
 * service that only the JS realm can provide, it declares a neutral entry point
 * and this file supplies the JS-specific implementation.
 *
 * Today that is the scheduling seam. The core asks for "run this once the
 * current turn settles"; the JS event loop is what actually runs it, and this
 * translation lives here so lambda/dom/ keeps no direct dependency on
 * js_event_loop.h. See vibe/Lambda_Design_DOM_API.md ES33.
 */

#include "../lambda-data.hpp"
#include "../dom/dom.h"
#include "js_event_loop.h"

extern "C" void dom_schedule_microtask(Item callback) {
    js_microtask_enqueue(callback);
}

extern "C" void dom_schedule_task(Item callback) {
    // Zero delay: this is "after the current turn", not timed work. setTimeout(0)
    // is the loop's existing spelling for that, so the seam needs no delay
    // parameter until a caller genuinely wants one.
    js_setTimeout(callback, (Item){.item = i2it(0)});
}
