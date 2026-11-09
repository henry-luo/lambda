#include "test_wiki_script_helper.hpp"
#include "lambda/format/format.h"
#include "lib/log.h"
#include "lib/mempool.h"
#include "lib/test_utils.h"
#include <stdio.h>
#include <stdlib.h>

// Preserve the writer's public Lambda CLI boundary; native input has no script engine.
const char* format_wiki_script(Pool* pool, Item item) {
    String* mark = format_mark(pool, item);
    if (!mark) return nullptr;
    char* source = tu_write_temp("wiki_format_source", "mark", mark->chars);
    char* destination = tu_write_temp("wiki_format_output", "wiki", "");
    if (!source || !destination) { free(source); free(destination); return nullptr; }
    char command[2048];
    snprintf(command, sizeof(command), "./lambda.exe convert '%s' -f mark -t wiki -o '%s'", source, destination);
    int status = -1;
    char* diagnostics = tu_run(command, &status);
    if (status) log_error("wiki-script-format: CLI conversion failed: %s", diagnostics ? diagnostics : "no output");
    char* output = status == 0 ? tu_slurp(destination) : nullptr;
    const char* result = output ? pool_strdup(pool, output) : nullptr;
    remove(source); remove(destination);
    free(output); free(diagnostics); free(source); free(destination);
    return result;
}
