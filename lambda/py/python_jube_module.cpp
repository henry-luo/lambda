#include "../jube/jube_language.h"
#include "py_transpiler.hpp"

#include <string.h>

extern "C" bool python_jube_register_runtime_imports(
    const JubeRuntimeCatalogAPI* runtime_catalog);

static const JubeHostLangAPI* python_jube_hosted_api = NULL;

// This adapter is the external lang-python module entry point. It confines
// Python command and module dispatch to JubeLanguageDef while main.cpp stays
// language-neutral; host execution services own activation and recovery.
struct JubeLanguageSession {
    uint32_t marker;
};

static void python_jube_write(const JubeLanguageRunRequest* request,
                              const char* bytes, size_t length, bool stderr_output) {
    if (!request || !bytes || length == 0) return;
    JubeLanguageWriteFn writer = stderr_output ? request->write_stderr : request->write_stdout;
    if (writer) writer(request->output_user, bytes, length);
}

static void python_jube_write_cstr(const JubeLanguageRunRequest* request,
                                   const char* text, bool stderr_output) {
    if (!text) return;
    python_jube_write(request, text, strlen(text), stderr_output);
}

static void python_jube_write_file_error(const JubeLanguageRunRequest* request,
                                         const char* prefix, const char* path) {
    python_jube_write_cstr(request, prefix ? prefix : "Python error", true);
    if (path && *path) {
        python_jube_write_cstr(request, ": ", true);
        python_jube_write_cstr(request, path, true);
    }
    python_jube_write_cstr(request, "\n", true);
}

static int python_jube_create_session(const JubeLanguageSessionConfig* config,
                                      JubeLanguageSession** out_session) {
    if (!config || config->struct_size < JUBE_LANGUAGE_SESSION_CONFIG_V1_SIZE || !out_session) {
        return -1;
    }
    if (!python_jube_hosted_api ||
        python_jube_hosted_api->struct_size < JUBE_HOST_LANG_API_H7A_SIZE ||
        !python_jube_hosted_api->session_memory ||
        python_jube_hosted_api->session_memory->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        python_jube_hosted_api->session_memory->struct_size < JUBE_SESSION_MEMORY_API_V1_SIZE ||
        !python_jube_hosted_api->session_memory->session_alloc ||
        !python_jube_hosted_api->session_memory->session_free) {
        return -1;
    }
    JubeLanguageSession* session = (JubeLanguageSession*)
        python_jube_hosted_api->session_memory->session_alloc(sizeof(JubeLanguageSession));
    if (!session) return -1;
    session->marker = 0x50594a53u;
    *out_session = session;
    return 0;
}

static void python_jube_destroy_session(JubeLanguageSession* session) {
    if (!session) return;
    session->marker = 0;
    if (python_jube_hosted_api && python_jube_hosted_api->session_memory &&
        python_jube_hosted_api->session_memory->session_free) {
        python_jube_hosted_api->session_memory->session_free(session);
    }
}

static int python_jube_run(JubeLanguageSession* session,
                           const JubeLanguageRunRequest* request) {
    if (!session || session->marker != 0x50594a53u || !request ||
        request->struct_size < JUBE_LANGUAGE_RUN_REQUEST_V1_SIZE) {
        return -1;
    }
    if (request->show_help) {
        python_jube_write_cstr(request,
            "Lambda hosted Python\n\nUsage: lambda.exe py [file.py]\n", false);
        return 0;
    }
    if (!request->source_path || !*request->source_path) return 0;

    JubeHostedSource source = {JUBE_HOSTED_SOURCE_V1_SIZE};
    if (!python_jube_hosted_api || !python_jube_hosted_api->source ||
        !python_jube_hosted_api->source->source_read ||
        !python_jube_hosted_api->source->source_release ||
        python_jube_hosted_api->source->source_read(request->source_path, &source) != 0) {
        python_jube_write_file_error(request, "Python could not read file", request->source_path);
        return -1;
    }

    void* execution = python_jube_hosted_api->execution->execution_create();
    if (!execution) {
        python_jube_hosted_api->source->source_release(&source);
        python_jube_write_file_error(request, "Python could not create execution", request->source_path);
        return -1;
    }
    Item result = transpile_py_to_mir(execution, source.bytes,
        source.canonical_path ? source.canonical_path : request->source_path);
    python_jube_hosted_api->source->source_release(&source);
    if (result.item == ITEM_ERROR) {
        python_jube_hosted_api->execution->execution_destroy(execution);
        return -1;
    }
    python_jube_hosted_api->output->write_result(request, result);
    python_jube_hosted_api->execution->execution_destroy(execution);
    return 0;
}

static int python_jube_load_module(JubeLanguageSession* session,
                                   const JubeLanguageModuleRequest* request,
                                   Item* out_namespace) {
    if (out_namespace) *out_namespace = ItemNull;
    if (!session || session->marker != 0x50594a53u || !request ||
        request->struct_size < JUBE_LANGUAGE_MODULE_REQUEST_V1_SIZE ||
        !request->host_context || !request->source_path || !out_namespace) {
        return -1;
    }
    // This adapter passes an opaque execution token; no shared importer knows
    // Python's compiler entry point or its namespace representation.
    *out_namespace = load_py_module(request->host_context, request->source_path);
    return out_namespace->item == ItemNull.item ? -1 : 0;
}

static const char* const python_jube_aliases[] = {"py"};
static const char* const python_jube_extensions[] = {".py"};

static const JubeLanguageDef python_jube_language = {
    JUBE_LANGUAGE_ABI_VERSION,
    sizeof(JubeLanguageDef),
    "python",
    python_jube_aliases,
    1,
    python_jube_extensions,
    1,
    python_jube_create_session,
    python_jube_destroy_session,
    python_jube_run,
    python_jube_load_module,
    JUBE_HOST_CAP_GC_ROOTS |
        JUBE_HOST_CAP_NEUTRAL_DATA |
        JUBE_HOST_CAP_RUNTIME_CATALOG |
        JUBE_HOST_CAP_MODULE_GRAPH |
        JUBE_HOST_CAP_GUEST_EXECUTION,
    JUBE_HOSTED_LANG_CAP_SOURCE |
        JUBE_HOSTED_LANG_CAP_DIAGNOSTICS |
        JUBE_HOSTED_LANG_CAP_RESULT_FORMAT |
        JUBE_HOSTED_LANG_CAP_SESSION_MEMORY |
        JUBE_HOSTED_LANG_CAP_EXECUTION |
        JUBE_HOSTED_LANG_CAP_MODULE_GRAPH,
    JUBE_HOST_BUILD_ID,
};

static void python_jube_heap_cleanup(void* host_heap) {
    py_module_heap_cleanup(host_heap);
}

static int python_jube_init(const JubeHostAPI* host) {
    if (!host || host->struct_size < JUBE_HOST_API_DATA_SIZE ||
        !host->runtime_catalog ||
        host->runtime_catalog->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->runtime_catalog->struct_size < JUBE_RUNTIME_CATALOG_API_V1_SIZE ||
        !host->runtime_catalog->register_imports ||
        !host->runtime_catalog->lookup_import_metadata || !host->data ||
        host->data->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->data->struct_size < JUBE_HOST_DATA_API_FULL_SIZE ||
        !host->data->name_from_utf8 || !host->data->map_set ||
        !host->data->float_from_f64 || !host->data->format_json ||
        !host->data->closure_env_alloc || !host->data->closure_env_store ||
        !host->data->closure_env_load || !host->data->item_slots_store ||
        !host->data->item_slots_load || !host->data->map_new ||
        !host->data->function_new ||
        !host->hosted_language ||
        host->hosted_language->api_version != JUBE_HOST_LANG_API_VERSION ||
        host->hosted_language->struct_size < JUBE_HOST_LANG_API_H7E2_ROOTS_SIZE ||
        !host->hosted_language->source || !host->hosted_language->diagnostic ||
        !host->hosted_language->output || !host->hosted_language->session_memory ||
        !host->hosted_language->execution || !host->hosted_language->module_graph ||
        !host->hosted_language->roots ||
        host->hosted_language->source->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->hosted_language->source->struct_size < JUBE_SOURCE_API_V1_SIZE ||
        !host->hosted_language->source->source_read ||
        !host->hosted_language->source->source_release ||
        host->hosted_language->diagnostic->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->hosted_language->diagnostic->struct_size < JUBE_DIAGNOSTIC_API_V1_SIZE ||
        !host->hosted_language->diagnostic->write_diagnostic ||
        host->hosted_language->output->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->hosted_language->output->struct_size < JUBE_OUTPUT_API_V1_SIZE ||
        !host->hosted_language->output->write_result ||
        host->hosted_language->session_memory->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->hosted_language->session_memory->struct_size < JUBE_SESSION_MEMORY_API_V1_SIZE ||
        !host->hosted_language->session_memory->session_alloc ||
        !host->hosted_language->session_memory->session_free ||
        host->hosted_language->execution->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->hosted_language->execution->struct_size < JUBE_GUEST_EXECUTION_API_H7C_REGISTER_LOOKUP_SIZE ||
        !host->hosted_language->execution->execution_create ||
        !host->hosted_language->execution->execution_destroy ||
        !host->hosted_language->execution->execution_link_module ||
        !host->hosted_language->execution->mir_context_create ||
        !host->hosted_language->execution->mir_context_destroy ||
        !host->hosted_language->execution->mir_module_create ||
        !host->hosted_language->execution->mir_module_finalize_and_load ||
        !host->hosted_language->execution->mir_function_lookup ||
        !host->hosted_language->execution->mir_function_finish ||
        !host->hosted_language->execution->execution_activate ||
        !host->hosted_language->execution->execution_activate_import ||
        !host->hosted_language->execution->execution_run_main ||
        !host->hosted_language->execution->execution_finish_guest ||
        !host->hosted_language->execution->execution_frame_runtime_slot ||
        !host->hosted_language->execution->mir_item_function_create ||
        !host->hosted_language->execution->mir_function_forward_create ||
        !host->hosted_language->execution->mir_item_function_proto_create ||
        !host->hosted_language->execution->mir_function_register_lookup ||
        host->hosted_language->roots->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->hosted_language->roots->struct_size < JUBE_HOST_ROOT_API_H5_PERSISTENT_SIZE ||
        !host->hosted_language->roots->root_frame_begin ||
        !host->hosted_language->roots->root_frame_take_slot ||
        !host->hosted_language->roots->root_frame_end ||
        !host->hosted_language->roots->persistent_root_register) {
        return -1;
    }
    if (host->hosted_language->module_graph->api_version != JUBE_HOST_SERVICE_API_VERSION ||
        host->hosted_language->module_graph->struct_size < JUBE_MODULE_GRAPH_API_V1_SIZE ||
        !host->hosted_language->module_graph->loading_namespace ||
        !host->hosted_language->module_graph->load_lambda_module ||
        !host->hosted_language->module_graph->module_state ||
        !host->hosted_language->module_graph->module_begin_loading ||
        !host->hosted_language->module_graph->module_publish) {
        return -1;
    }
    python_jube_hosted_api = host->hosted_language;
    py_set_hosted_module_graph_api(host->hosted_language->module_graph);
    py_set_hosted_source_api(host->hosted_language->source);
    py_set_hosted_execution_api(host->hosted_language->execution);
    py_set_hosted_root_api(host->hosted_language->roots);
    py_set_hosted_runtime_catalog_api(host->runtime_catalog);
    py_set_hosted_data_api(host->data);
    // Register imports at descriptor activation so static and dlopen paths
    // publish the same MIR runtime surface before Python code is compiled.
    return python_jube_register_runtime_imports(host->runtime_catalog) ? 0 : -1;
}

static const JubeModuleDef python_jube_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "lang-python",
    "0.1.0",
    "Hosted Python language adapter",
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    python_jube_init,
    NULL,
    NULL,
    NULL,
    0,
    NULL,
    python_jube_heap_cleanup,
    &python_jube_language,
};

// Hosted languages are always discovered through the module loader. The
// standalone target exports the conventional entry point; a legacy monolithic
// build receives a private name so it cannot accidentally self-register.
#if defined(LAMBDA_PYTHON_DYNAMIC_MODULE)
extern "C" const JubeModuleDef* jube_module(void) {
#else
extern "C" const JubeModuleDef* lang_python_jube_module(void) {
#endif
    return &python_jube_module;
}
