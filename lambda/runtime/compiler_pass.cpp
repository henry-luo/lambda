#include "compiler_timing.hpp"

#include <string.h>

extern "C" void compiler_pass_manager_init(CompilerPassManager* manager,
        uint32_t initial_facts) {
    if (!manager) return;
    memset(manager, 0, sizeof(*manager));
    manager->facts = initial_facts;
}

extern "C" int compiler_pass_manager_add(CompilerPassManager* manager,
        const CompilerPassSpec* pass) {
    if (!manager || !pass || !pass->name || !pass->run ||
            manager->pass_count >= sizeof(manager->passes) / sizeof(manager->passes[0])) {
        return 0;
    }
    manager->passes[manager->pass_count++] = *pass;
    return 1;
}

extern "C" int compiler_pass_manager_run(CompilerPassManager* manager, void* context) {
    if (!manager) return 0;
    for (uint32_t i = 0; i < manager->pass_count; i++) {
        CompilerPassSpec* pass = &manager->passes[i];
        if ((manager->facts & pass->required_facts) != pass->required_facts) return 0;
        if (!pass->run(context)) return 0;
        manager->facts |= pass->produced_facts;
    }
    return 1;
}

extern "C" uint32_t compiler_pass_manager_facts(const CompilerPassManager* manager) {
    return manager ? manager->facts : COMPILER_FACT_NONE;
}
