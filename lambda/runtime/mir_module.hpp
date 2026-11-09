#pragma once

#include <mir.h>

// MVP eager and satellite images share the immutable module-state ABI.
struct MirModuleNames {
    const char* module;
    const char* consts_bss;
    const char* layout_bss;
    const char* type_list_bss;
    const char* property_specs_bss;
};

struct MirModuleArtifacts {
    MIR_item_t consts_bss;
    MIR_item_t layout_bss;
    MIR_item_t type_list_bss;
    MIR_item_t property_specs_bss;
};

static const MirModuleNames MIR_DEFAULT_MODULE_NAMES = {
    "lambda_script", "_mod_consts_ptr", "_mod_layout",
    "_mod_type_list_ptr", "_mod_property_specs",
};
