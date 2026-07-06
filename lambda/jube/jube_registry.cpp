#include "jube_registry.h"
#include "../../lib/log.h"
#include <string.h>

#define JUBE_STATIC_MODULE_CAPACITY 64

typedef struct JubeStaticModuleEntry {
    const JubeModuleDef* module;
    bool initialized;
} JubeStaticModuleEntry;

static JubeStaticModuleEntry jube_static_modules[JUBE_STATIC_MODULE_CAPACITY];
static int jube_static_modules_count = 0;
static JubeHostAPI jube_host_api = {
    JUBE_ABI_VERSION,
};

extern "C" void radiant_jube_register_static(void);

static bool jube_module_name_equals(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static int jube_find_static_module_index(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < jube_static_modules_count; i++) {
        const JubeModuleDef* module = jube_static_modules[i].module;
        if (module && jube_module_name_equals(module->name, name)) return i;
    }
    return -1;
}

int jube_register_static_module(const JubeModuleDef* module) {
    if (!module || !module->name) {
        log_error("JUBE_REG: cannot register null static module");
        return -1;
    }
    if (module->abi_version != JUBE_ABI_VERSION) {
        log_error("JUBE_REG: module '%s' ABI mismatch: got %u expected %u",
                  module->name, module->abi_version, JUBE_ABI_VERSION);
        return -1;
    }
    if (module->struct_size < sizeof(JubeModuleDef)) {
        log_error("JUBE_REG: module '%s' descriptor is too small: got %u expected %zu",
                  module->name, module->struct_size, sizeof(JubeModuleDef));
        return -1;
    }

    int existing = jube_find_static_module_index(module->name);
    if (existing >= 0) {
        log_debug("JUBE_REG: static module '%s' already registered", module->name);
        return 0;
    }
    if (jube_static_modules_count >= JUBE_STATIC_MODULE_CAPACITY) {
        log_error("JUBE_REG: static module capacity exceeded while registering '%s'", module->name);
        return -1;
    }

    int slot = jube_static_modules_count++;
    jube_static_modules[slot].module = module;
    jube_static_modules[slot].initialized = false;

    if (module->init) {
        int rc = module->init(&jube_host_api);
        if (rc != 0) {
            jube_static_modules_count--;
            jube_static_modules[slot].module = NULL;
            jube_static_modules[slot].initialized = false;
            log_error("JUBE_REG: static module '%s' init failed with code %d", module->name, rc);
            return -1;
        }
        jube_static_modules[slot].initialized = true;
    }

    log_info("JUBE_REG: registered static module '%s' version '%s'",
             module->name, module->version ? module->version : "(none)");
    return 0;
}

void jube_register_builtin_modules(void) {
    radiant_jube_register_static();
}

int jube_static_module_count(void) {
    return jube_static_modules_count;
}

const JubeModuleDef* jube_static_module_at(int index) {
    if (index < 0 || index >= jube_static_modules_count) return NULL;
    return jube_static_modules[index].module;
}

const JubeModuleDef* jube_find_static_module(const char* name) {
    int index = jube_find_static_module_index(name);
    if (index < 0) return NULL;
    return jube_static_modules[index].module;
}
