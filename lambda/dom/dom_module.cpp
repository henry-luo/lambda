/**
 * dom_module.cpp — the Lambda-facing face of the DOM core (`import dom`).
 *
 * ES36. Page JavaScript reaches the DOM core through the Jube declared
 * interfaces and the JubeHostDomAPI seam; this module is the same core's other
 * door, for Lambda scripts. Every function here is a thin delegation to
 * dom_ops.h -- the ordinal executor and the two property entries -- so a `dom.*`
 * call and the equivalent JS call land on one implementation (ES38). Nothing in
 * this file implements DOM behaviour of its own.
 *
 * Collections are returned as ordinary Lambda arrays, i.e. snapshots (S9.2.2):
 * a Lambda caller iterating the result of dom.query_all() walks the value it
 * was handed, and mutating the tree mid-iteration does not perturb it. Live
 * collections stay a JS-adapter concern (DOM_Pkg Q4).
 *
 * Document acquisition is deliberately absent -- see ESO80. Creating a document
 * needs Radiant's loader, which lives in the radiant link target, above this
 * one; a Lambda script obtains a root through `radiant.load(path)` today and
 * drives it with `dom.*` from there.
 */

#include "../lambda-data.hpp"
#include "dom.h"
#include "dom_ops.h"
#include "../js/js_runtime.h"
#include "../jube/jube_registry.h"
#include "../../lib/log.h"

#include "dom_core.h"

// ---------------------------------------------------------------------------
// The published table is an expansion of the catalog (ES39/ES44). A row is
// published to `dom.*` when it has a native body and is realm-neutral; rows
// whose body is engine-provided (DOM_F_ENGINE, filled at table time in F30)
// or realm-dependent (ESO81) stay in the catalog but out of this face until
// their dependency is resolved. Nothing here is written by hand per operation.
// ---------------------------------------------------------------------------

struct DomCatalogRow {
    const char* name;
    const char* signature;
    fn_ptr body;
    unsigned flags;
};

static const DomCatalogRow dom_catalog[] = {
#define DOM_OP(tier, name, cluster, argc, sig, body, flags, deriv) \
    { #name, sig, (fn_ptr)(body), (unsigned)(flags) },
#include "dom_api.def"
#undef DOM_OP
};
static const int dom_catalog_count = (int)(sizeof(dom_catalog) / sizeof(dom_catalog[0]));

static JubeFuncDef dom_functions[sizeof(dom_catalog) / sizeof(dom_catalog[0])];
static int dom_function_count = 0;

static void dom_build_published_table(void) {
    if (dom_function_count) return;
    for (int i = 0; i < dom_catalog_count; i++) {
        const DomCatalogRow& row = dom_catalog[i];
        if (!row.body || !(row.flags & DOM_F_NEUTRAL)) continue;
        JubeFuncDef def = { row.name, row.signature, row.body, JUBE_FN_NONE, nullptr, row.body };
        dom_functions[dom_function_count++] = def;
    }
}

// Zero is success here, matching every other Jube module's init.
static int dom_module_init(const JubeHostAPI* host) {
    // The module holds no state of its own: it forwards to the DOM core it is
    // linked with, so there is nothing to bind beyond the ABI check.
    if (!host || host->api_version != JUBE_HOST_API_VERSION) {
        log_error("JUBE_DOM: missing or mismatched host API during module init");
        return -1;
    }
    // Node wrapping is engine-side, so `import dom` needs the engine module's
    // host API bound even when `radiant` was never imported (ESO80).
    dom_engine_bind_host((const void*)host);
    return 0;
}

static JubeModuleDef dom_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "dom",
    "0.1.0",
    "DOM access for Lambda scripts over the shared DOM core",
    nullptr, 0,                                   // types: the wrappers are the
                                                  // radiant module's dom_node
    dom_functions, 0,                             // count set at registration
    nullptr, 0,                                   // namespaces
    dom_module_init,
    nullptr,                                      // shutdown
};

extern "C" void dom_jube_register_static(void) {
    dom_build_published_table();
    dom_module.function_count = dom_function_count;
    jube_register_static_module(&dom_module);
}
