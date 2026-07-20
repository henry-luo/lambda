#include "jube_language.h"

#include "jube_interface.h"
#include "jube_registry.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"

#include <string.h>

static bool jube_language_has_capability_tail(const JubeLanguageDef* language) {
    return language && language->struct_size >= JUBE_LANGUAGE_DEF_CAPABILITIES_SIZE;
}

static uint64_t jube_language_required_host_capabilities(const JubeLanguageDef* language) {
    return jube_language_has_capability_tail(language)
        ? language->required_host_capabilities : JUBE_HOST_CAP_NONE;
}

static uint64_t jube_language_required_hosted_capabilities(const JubeLanguageDef* language) {
    return jube_language_has_capability_tail(language)
        ? language->required_hosted_capabilities : JUBE_HOST_CAP_NONE;
}

static const char* jube_language_required_host_build_id(const JubeLanguageDef* language) {
    return jube_language_has_capability_tail(language)
        ? language->required_host_build_id : NULL;
}

static bool jube_language_ascii_equal(const char* left, const char* right) {
    if (!left || !right) return false;
    while (*left && *right) {
        char a = *left;
        char b = *right;
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return false;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool jube_language_extension_equal(const char* left, const char* right) {
    if (!left || !right) return false;
    if (*left == '.') left++;
    if (*right == '.') right++;
    return jube_language_ascii_equal(left, right);
}

const JubeLanguageDef* jube_module_language(const JubeModuleDef* module) {
    if (!module) return NULL;
    size_t field_end = offsetof(JubeModuleDef, language) + sizeof(module->language);
    if (module->struct_size < field_end) return NULL;
    return module->language;
}

static bool jube_language_matches_name(const JubeLanguageDef* language, const char* name) {
    if (!language || !name || !*name) return false;
    if (jube_language_ascii_equal(language->name, name)) return true;
    for (int32_t i = 0; i < language->alias_count; i++) {
        if (jube_language_ascii_equal(language->aliases[i], name)) return true;
    }
    return false;
}

static bool jube_language_matches_extension(const JubeLanguageDef* language,
                                            const char* extension) {
    if (!language || !extension || !*extension) return false;
    for (int32_t i = 0; i < language->extension_count; i++) {
        if (jube_language_extension_equal(language->extensions[i], extension)) return true;
    }
    return false;
}

const JubeLanguageDef* jube_find_language(const char* name) {
    if (!name || !*name) return NULL;
    for (int i = 0; i < jube_static_module_count(); i++) {
        const JubeLanguageDef* language = jube_module_language(jube_static_module_at(i));
        if (jube_language_matches_name(language, name)) return language;
    }
    return NULL;
}

const JubeLanguageDef* jube_find_language_for_path(const char* path) {
    if (!path || !*path) return NULL;
    const char* extension = NULL;
    for (const char* cursor = path; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') extension = NULL;
        else if (*cursor == '.') extension = cursor;
    }
    if (!extension || !extension[1]) return NULL;
    for (int i = 0; i < jube_static_module_count(); i++) {
        const JubeLanguageDef* language = jube_module_language(jube_static_module_at(i));
        if (jube_language_matches_extension(language, extension)) return language;
    }
    return NULL;
}

int jube_hosted_extension_count(void) {
    jube_register_builtin_modules();
    int count = 0;
    for (int i = 0; i < jube_static_module_count(); i++) {
        const JubeLanguageDef* language = jube_module_language(jube_static_module_at(i));
        if (!language) continue;
        count += language->extension_count;
    }
    return count;
}

const char* jube_hosted_extension_at(int index) {
    if (index < 0) return NULL;
    jube_register_builtin_modules();
    for (int i = 0; i < jube_static_module_count(); i++) {
        const JubeLanguageDef* language = jube_module_language(jube_static_module_at(i));
        if (!language) continue;
        if (index < language->extension_count) return language->extensions[index];
        index -= language->extension_count;
    }
    return NULL;
}

static bool jube_language_name_conflicts(const JubeLanguageDef* candidate,
                                         const JubeLanguageDef* existing) {
    if (!candidate || !existing) return false;
    if (jube_language_matches_name(existing, candidate->name)) return true;
    for (int32_t i = 0; i < candidate->alias_count; i++) {
        if (jube_language_matches_name(existing, candidate->aliases[i])) return true;
    }
    return false;
}

static bool jube_language_extension_conflicts(const JubeLanguageDef* candidate,
                                              const JubeLanguageDef* existing) {
    if (!candidate || !existing) return false;
    for (int32_t i = 0; i < candidate->extension_count; i++) {
        if (jube_language_matches_extension(existing, candidate->extensions[i])) return true;
    }
    return false;
}

int jube_language_validate_registration(const JubeModuleDef* module) {
    const JubeLanguageDef* language = jube_module_language(module);
    if (!language) return 0;
    if (language->abi_version != JUBE_LANGUAGE_ABI_VERSION ||
        language->struct_size < JUBE_LANGUAGE_DEF_V1_SIZE ||
        !language->name || !*language->name ||
        language->alias_count < 0 || language->extension_count < 0 ||
        (language->alias_count > 0 && !language->aliases) ||
        (language->extension_count > 0 && !language->extensions) ||
        !language->create_session || !language->destroy_session || !language->run) {
        log_error("JUBE_LANG: module '%s' has an invalid language descriptor",
                  module && module->name ? module->name : "(unknown)");
        return -1;
    }

    for (int32_t i = 0; i < language->alias_count; i++) {
        if (!language->aliases[i] || !*language->aliases[i]) {
            log_error("JUBE_LANG: language '%s' has an empty alias", language->name);
            return -1;
        }
    }
    for (int32_t i = 0; i < language->extension_count; i++) {
        if (!language->extensions[i] || !language->extensions[i][0]) {
            log_error("JUBE_LANG: language '%s' has an empty extension", language->name);
            return -1;
        }
    }

    const JubeHostAPI* host = jube_internal_host_api();
    if (!host || host->api_version != JUBE_HOST_API_VERSION ||
        host->struct_size < sizeof(JubeHostAPI)) {
        log_error("JUBE_LANG: host API is unavailable for language '%s'", language->name);
        return -1;
    }
    uint64_t required_host = jube_language_required_host_capabilities(language);
    if ((required_host & host->capabilities) != required_host) {
        log_error("JUBE_LANG: language '%s' requires unavailable host capabilities 0x%llx",
                  language->name, (unsigned long long)(required_host & ~host->capabilities));
        return -1;
    }
    uint64_t required_hosted = jube_language_required_hosted_capabilities(language);
    const JubeHostLangAPI* hosted = host->hosted_language;
    if (!hosted || hosted->api_version != JUBE_HOST_LANG_API_VERSION ||
        hosted->struct_size < sizeof(JubeHostLangAPI) ||
        (required_hosted & hosted->capabilities) != required_hosted) {
        log_error("JUBE_LANG: language '%s' requires unavailable hosted capabilities 0x%llx",
                  language->name, (unsigned long long)(required_hosted &
                  ~(hosted ? hosted->capabilities : 0)));
        return -1;
    }
    const char* required_build_id = jube_language_required_host_build_id(language);
    if (required_build_id && (!host->host_build_id ||
        strcmp(required_build_id, host->host_build_id) != 0)) {
        log_error("JUBE_LANG: language '%s' requires host build '%s', got '%s'",
                  language->name, required_build_id,
                  host->host_build_id ? host->host_build_id : "(none)");
        return -1;
    }

    for (int i = 0; i < jube_static_module_count(); i++) {
        const JubeLanguageDef* existing = jube_module_language(jube_static_module_at(i));
        if (!existing) continue;
        if (jube_language_name_conflicts(language, existing) ||
            jube_language_extension_conflicts(language, existing)) {
            log_error("JUBE_LANG: language '%s' conflicts with registered language '%s'",
                      language->name, existing->name);
            return -1;
        }
    }
    return 0;
}

int jube_run_language(const char* name, const JubeLanguageRunRequest* request) {
    const JubeLanguageDef* language = jube_find_language(name);
    if (!language) return -1;
    if (!request || request->struct_size < JUBE_LANGUAGE_RUN_REQUEST_V1_SIZE) {
        log_error("JUBE_LANG: run request for '%s' is invalid", language->name);
        return -1;
    }

    JubeLanguageSessionConfig config = {JUBE_LANGUAGE_SESSION_CONFIG_V1_SIZE};
    JubeLanguageSession* session = NULL;
    int rc = language->create_session(&config, &session);
    if (rc != 0 || !session) {
        log_error("JUBE_LANG: failed to create '%s' session", language->name);
        return -1;
    }
    rc = language->run(session, request);
    language->destroy_session(session);
    return rc;
}

bool jube_load_hosted_module(void* host_context, const char* source_path,
                             const char* importer_path, Item* out_namespace) {
    if (out_namespace) *out_namespace = ItemNull;
    // Module discovery occurs only on the import fallback path; it is never
    // part of Lambda or JavaScript evaluation/JIT dispatch.
    jube_register_builtin_modules();
    const JubeLanguageDef* language = jube_find_language_for_path(source_path);
    if (!language || !language->load_module) return false;

    void* import_execution = jube_create_import_execution(host_context);
    if (!import_execution) {
        log_error("JUBE_LANG: could not create import execution for '%s'", source_path);
        return false;
    }

    JubeLanguageModuleRequest request = {
        JUBE_LANGUAGE_MODULE_REQUEST_V1_SIZE,
        import_execution,
        source_path,
        importer_path,
    };
    JubeLanguageSessionConfig config = {JUBE_LANGUAGE_SESSION_CONFIG_V1_SIZE};
    JubeLanguageSession* session = NULL;
    int rc = language->create_session(&config, &session);
    if (rc != 0 || !session) {
        log_error("JUBE_LANG: failed to create '%s' module session", language->name);
        jube_destroy_import_execution(import_execution);
        return false;
    }
    Item namespace_obj = ItemNull;
    rc = language->load_module(session, &request, &namespace_obj);
    language->destroy_session(session);
    // A standalone imported language owns the activation until the runtime
    // heap cleanup hook releases it. Nested imports restore immediately here.
    if (rc != 0 || !jube_import_execution_is_retained(import_execution)) {
        jube_destroy_import_execution(import_execution);
    }
    if (rc != 0) return false;
    if (out_namespace) *out_namespace = namespace_obj;
    return namespace_obj.item != ItemNull.item;
}

bool jube_load_language_module(void* host_context, const char* source_path,
                               const char* importer_path, Item* out_namespace) {
    if (out_namespace) *out_namespace = ItemNull;
    if (!host_context || !source_path || !*source_path) return false;
    if (jube_load_hosted_module(host_context, source_path, importer_path, out_namespace)) {
        return true;
    }

    const char* extension = NULL;
    for (const char* cursor = source_path; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') extension = NULL;
        else if (*cursor == '.') extension = cursor;
    }
    if (!extension || !jube_language_extension_equal(extension, ".js")) return false;

    // JS remains a host-native language, but cross-language imports use this
    // reviewed import-time bridge instead of exposing JS loader internals to a
    // hosted module.
    Runtime* runtime = (Runtime*)jube_execution_runtime_handle(host_context);
    Item namespace_obj = runtime ? load_js_module(runtime, source_path) : ItemNull;
    if (namespace_obj.item == ItemNull.item) return false;
    if (out_namespace) *out_namespace = namespace_obj;
    return true;
}
