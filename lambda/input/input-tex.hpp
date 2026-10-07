// TeX expansion engine for the LaTeX input boundary (Lambda_Pkg_Latex3 §9).
//
// The engine expands a document and the package files beside it, then hands
// the direct LaTeX parser a reconstructed source plus an offset map
// (vibe/impl/Lambda_Impl_Latex_Phase4.md, "Integration shape").
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace tex {

struct Engine;

struct EngineOptions {
    const char* base_path;         // main document path or URL; NULL disables local files
    bool ini;                      // primitives only (conformance tests); no kernel
    const char* const* adapters;   // package/class names answered by script adapters
    int adapter_count;
    const char* const* raw_commands;  // adapter commands that read their arguments as tokens
    int raw_command_count;
    uint64_t max_expansions;       // 0 selects the default budget
    bool expl3;                    // start from the format with expl3 preloaded (built on first use)
};

// One span of the reconstructed text and where it came from in the main file.
struct OffsetSegment {
    uint32_t out_start;
    uint32_t length;
    uint32_t src_start;            // main-file offset; the call site when synthesized
    bool synthesized;
};

struct Diagnostic {
    const char* code;              // stable kebab-case code, e.g. "tex-undefined-cs"
    const char* message;
    uint32_t offset;               // main-file offset of the cause or its call site
    const char* file;              // file name when the cause is in another file
    uint32_t line;                 // line in `file` (or the main file) where it was read
};

struct Result {
    const char* text;              // reconstructed source (NUL-terminated)
    size_t length;
    const OffsetSegment* segments;
    size_t segment_count;
    const Diagnostic* diagnostics;
    size_t diagnostic_count;
    const char* const* messages;   // terminal lines: \message, \immediate\write16, \show...
    size_t message_count;
    const char* const* loaded_packages;  // beside-document packages/classes the engine ran
    size_t loaded_package_count;
};

Engine* engine_create(const EngineOptions* options);
// Expands `source`; result storage lives until engine_destroy().
bool engine_run(Engine* engine, const char* source, size_t length, Result* result);
// The run stopped because the document asked for expl3, which only the expl3 format
// provides (as LaTeX preloads it): run it again from a new engine with `expl3` set.
bool engine_wants_expl3(const Engine* engine);
void engine_destroy(Engine* engine);

// Map a reconstructed-text offset back to the main file.
size_t result_map_offset(const Result* result, size_t out_offset);

// Directory of the engine's bundled resources (unmodified upstream programming
// packages shipped with Lambda, §9.8). The runtime registers it at startup;
// without it, bundled packages are unavailable and reported as such.
void set_resource_dir(const char* dir);

} // namespace tex
