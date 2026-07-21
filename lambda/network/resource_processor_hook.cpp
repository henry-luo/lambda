#include "resource_processor_hook.h"

static const NetworkResourceProcessor* g_resource_processor = nullptr;

void network_resource_processor_register(const NetworkResourceProcessor* processor) {
    g_resource_processor = processor;
}

const NetworkResourceProcessor* network_resource_processor_get() {
    return g_resource_processor;
}
