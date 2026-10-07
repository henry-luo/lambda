#ifndef LAMBDA_INPUT_PARSE_OPTIONS_H
#define LAMBDA_INPUT_PARSE_OPTIONS_H

#include <stdbool.h>

// Parse options beyond type and flavor.
typedef struct InputParseOptions {
    bool source_positions;  // parse({sourcepos: true}): markup blocks carry their source lines
    bool embedded_math;     // markup: attach each <math>'s parsed `ast` and list it on
                            // Input::embedded_math, for a display that renders it directly
    // latex/tex: the TeX expansion engine (Lambda_Pkg_Latex3 §9)
    bool tex_expand;        // latex: expand with the engine before the direct parser
    bool tex_ini;           // tex: primitives only, no LaTeX kernel
    const char* tex_base;   // document path or URL; beside-document files resolve from it
    const char* const* tex_adapters;  // package/class names answered by script adapters
    int tex_adapter_count;
    const char* const* tex_raw_commands;  // adapter commands whose arguments stay unexpanded
    int tex_raw_command_count;
} InputParseOptions;

#endif
