#pragma once

#include "jube.h"

#ifdef __cplusplus
extern "C" {
#endif

int jube_register_static_module(const JubeModuleDef* module);
void jube_register_builtin_modules(void);
int jube_static_module_count(void);
const JubeModuleDef* jube_static_module_at(int index);
const JubeModuleDef* jube_find_static_module(const char* name);

#ifdef __cplusplus
}
#endif
