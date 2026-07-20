#pragma once

#include "jube.h"

#ifdef __cplusplus
extern "C" {
#endif

// Finds a registered hosted language by its canonical name or alias.
const JubeLanguageDef* jube_find_language(const char* name);

// Finds a registered hosted language by the extension in a source path.
const JubeLanguageDef* jube_find_language_for_path(const char* path);

// Enumerates extension candidates advertised by registered hosted languages.
// Discovery is performed only from an import/CLI fallback, never by Lambda or
// JavaScript evaluation.
int jube_hosted_extension_count(void);
const char* jube_hosted_extension_at(int index);

// Creates a short-lived session and invokes a language through its public
// descriptor. The registry owns no language-specific execution state.
int jube_run_language(const char* name, const JubeLanguageRunRequest* request);

// Loads a module through the language selected by its source extension. The
// host does not inspect namespace representation beyond the neutral Item.
bool jube_load_hosted_module(void* host_context, const char* source_path,
                             const char* importer_path, Item* out_namespace);

// Resolves a source module through the registered hosted-language registry,
// then through the reviewed built-in language bridge where applicable. This is
// an import-time operation; evaluators and generated code never call it.
bool jube_load_language_module(void* host_context, const char* source_path,
                               const char* importer_path, Item* out_namespace);

// Validates the language tail before a module is made visible to the registry.
// Dynamic and static registration intentionally share this path.
int jube_language_validate_registration(const JubeModuleDef* module);

#ifdef __cplusplus
}
#endif
