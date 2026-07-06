#include "../../jube/jube_registry.h"
#include "../../../lib/log.h"

static int radiant_module_init(const JubeHostAPI* host) {
    if (!host) {
        log_error("JUBE_RADIANT: missing host API during module init");
        return -1;
    }
    log_info("JUBE_RADIANT: static radiant module initialized");
    return 0;
}

static const JubeTypeDef radiant_types[] = {
    {"dom_node", 0},
};

static const JubeModuleDef radiant_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "radiant",
    "0.1.0",
    "Radiant DOM and layout access",
    radiant_types,
    (int32_t)(sizeof(radiant_types) / sizeof(radiant_types[0])),
    NULL,
    0,
    NULL,
    0,
    radiant_module_init,
    NULL,
};

extern "C" const JubeModuleDef* radiant_jube_module(void) {
    return &radiant_module;
}

extern "C" void radiant_jube_register_static(void) {
    jube_register_static_module(&radiant_module);
}
