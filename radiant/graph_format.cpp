#include "../lib/file.h"
#include "../lib/memtrack.h"
#include "../lambda/input/input.hpp"

#include <cstring>

static const char* graph_detect_flavor(const char* graph_file) {
    if (!graph_file) return nullptr;
    const char* flavor = input_detect_graph_flavor(graph_file, nullptr, 0);
    if (flavor) return flavor;
    char* source = read_text_file(graph_file);
    if (!source) return nullptr;
    flavor = input_detect_graph_flavor(graph_file, source, strlen(source));
    mem_free(source);
    return flavor;
}

bool graph_path_is_graph(const char* graph_file) {
    return graph_detect_flavor(graph_file) != nullptr;
}
