#include "compiler_timing.hpp"

#include "../../lib/time_util.h"
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

extern "C" void compiler_pass_manager_set_observer(CompilerPassManager* manager,
        CompilerPassObserver observer, void* context) {
    if (!manager) return;
    manager->observer = observer;
    manager->observer_context = context;
}

extern "C" void compiler_pass_manager_note(CompilerPassManager* manager,
        const char* name, uint64_t elapsed_us) {
    if (!manager || !manager->observer || !name) return;
    manager->observer(name, elapsed_us, manager->observer_context);
}

extern "C" int compiler_pass_manager_run(CompilerPassManager* manager, void* context) {
    if (!manager) return 0;
    for (uint32_t i = manager->next_pass; i < manager->pass_count; i++) {
        CompilerPassSpec* pass = &manager->passes[i];
        if ((manager->facts & pass->required_facts) != pass->required_facts) return 0;
        uint64_t started = manager->observer ? time_now_us() : 0;
        int succeeded = pass->run(pass->context ? pass->context : context);
        if (manager->observer) {
            manager->observer(pass->name, time_now_us() - started,
                manager->observer_context);
        }
        if (!succeeded) return 0;
        manager->facts |= pass->produced_facts;
        manager->next_pass = i + 1;
    }
    return 1;
}
