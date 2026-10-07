#include "lambda/jube/jube.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef JUBE_TEST_MODULE_NAME
#error "build the fixture with -DJUBE_TEST_MODULE_NAME=<module it impersonates>"
#endif

static void jube_test_write_marker(const char* environment_name, const char* contents) {
    const char* marker_path = getenv(environment_name);
    if (!marker_path || !*marker_path) return;
    FILE* marker = fopen(marker_path, "wb");
    if (!marker) return;
    fputs(contents, marker);
    fclose(marker);
}

static int jube_test_init_failure(const JubeHostAPI* host) {
#if defined(JUBE_TEST_SUCCESS_INIT)
    jube_test_write_marker("JUBE_DEPENDENCY_INIT_MARKER", "init\n");
    return host ? 0 : -72;
#elif defined(JUBE_TEST_REQUIRES_UNSUPPORTED_REQUIREMENTS) || \
      defined(JUBE_TEST_REQUIRES_UNDERSIZED_NODE_API)
    jube_test_write_marker("JUBE_DESCRIPTOR_INIT_MARKER", "init\n");
    return host ? 0 : -72;
#elif defined(JUBE_TEST_UNSUPPORTED_ABI) || defined(JUBE_TEST_UNDERSIZED_DESCRIPTOR)
    jube_test_write_marker("JUBE_DESCRIPTOR_INIT_MARKER", "init\n");
    return host ? 0 : -72;
#else
    return host ? -71 : -72;
#endif
}

static void jube_test_init_failure_shutdown(void) {
#if defined(JUBE_TEST_SUCCESS_INIT)
    jube_test_write_marker("JUBE_DEPENDENCY_SHUTDOWN_MARKER", "shutdown\n");
#else
    jube_test_write_marker("JUBE_INIT_FAILURE_MARKER", "shutdown\n");
#endif
}

#if defined(JUBE_TEST_REQUIRES_UNSUPPORTED_REQUIREMENTS)
static const JubeModuleRequirements jube_test_unsupported_requirements = {
    sizeof(JubeModuleRequirements),
    JUBE_HOST_API_VERSION,
    sizeof(JubeHostAPI),
    0,
    UINT64_C(1) << 63,
    0,
    0,
};
#define JUBE_TEST_REQUIREMENTS &jube_test_unsupported_requirements
#elif defined(JUBE_TEST_REQUIRES_UNSUPPORTED_NODE_VERSION)
static const JubeModuleRequirements jube_test_unsupported_node_requirements = {
    sizeof(JubeModuleRequirements),
    JUBE_HOST_API_VERSION,
    sizeof(JubeHostAPI),
    0,
    JUBE_HOST_CAP_NODE_RUNTIME,
    JUBE_HOST_SERVICE_API_VERSION + 1,
    sizeof(JubeHostNodeAPI),
};
#define JUBE_TEST_REQUIREMENTS &jube_test_unsupported_node_requirements
#elif defined(JUBE_TEST_REQUIRES_UNDERSIZED_NODE_API)
static const JubeModuleRequirements jube_test_undersized_node_requirements = {
    sizeof(JubeModuleRequirements),
    JUBE_HOST_API_VERSION,
    sizeof(JubeHostAPI),
    0,
    JUBE_HOST_CAP_NODE_RUNTIME,
    JUBE_HOST_SERVICE_API_VERSION,
    (uint32_t)(sizeof(JubeHostNodeAPI) + 1),
};
#define JUBE_TEST_REQUIREMENTS &jube_test_undersized_node_requirements
#elif defined(JUBE_TEST_REQUIRES_UNDERSIZED_VALUE_API)
static const JubeModuleRequirements jube_test_undersized_value_requirements = {
    sizeof(JubeModuleRequirements),
    JUBE_HOST_API_VERSION,
    sizeof(JubeHostAPI),
    0,
    JUBE_HOST_CAP_NODE_RUNTIME,
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNodeAPI),
    (uint32_t)(sizeof(JubeHostValueAPI) + 1),
    0,
    0,
};
#define JUBE_TEST_REQUIREMENTS &jube_test_undersized_value_requirements
#else
#define JUBE_TEST_REQUIREMENTS NULL
#endif

// The specifier catalog loads a module only when its descriptor attests every
// manifest provider (D7.3.4); the fixture provides exactly its own name.
static Item jube_test_namespace_build(void) {
    Item empty = {};
    return empty;
}
static const char* const jube_test_specifiers[] = {JUBE_TEST_MODULE_NAME};
static const JubeNamespaceDef jube_test_namespaces[] = {
    {jube_test_specifiers, 1, jube_test_namespace_build, NULL, 0},
};

#if defined(JUBE_TEST_UNSUPPORTED_ABI)
#define JUBE_TEST_DESCRIPTOR_ABI (JUBE_ABI_VERSION + 1)
#else
#define JUBE_TEST_DESCRIPTOR_ABI JUBE_ABI_VERSION
#endif

#if defined(JUBE_TEST_UNDERSIZED_DESCRIPTOR)
#define JUBE_TEST_DESCRIPTOR_SIZE (JUBE_MODULE_DEF_V1_SIZE - 1)
#else
#define JUBE_TEST_DESCRIPTOR_SIZE sizeof(JubeModuleDef)
#endif

static const JubeModuleDef jube_test_init_failure_module = {
    JUBE_TEST_DESCRIPTOR_ABI,
    JUBE_TEST_DESCRIPTOR_SIZE,
    JUBE_TEST_MODULE_NAME,
    "0.1.0",
    "Test-only module whose initializer fails after descriptor validation",
    NULL,
    0,
    NULL,
    0,
    jube_test_namespaces,
    1,
    jube_test_init_failure,
    jube_test_init_failure_shutdown,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    JUBE_TEST_REQUIREMENTS,
};

extern "C" const JubeModuleDef* jube_module(void) {
    return &jube_test_init_failure_module;
}
