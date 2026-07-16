#include "input-graph.h"
#include "../../lib/arraylist.h"
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/memtrack.h"
#include "../mark_builder.hpp"
#include "../mark_reader.hpp"
#include <string.h>

using namespace lambda;

static const size_t STRUCTURIZR_INCLUDE_MAX_DEPTH = 32;
static const size_t STRUCTURIZR_INCLUDE_MAX_FILES = 256;
static const size_t STRUCTURIZR_INCLUDE_MAX_BYTES = 16 * 1024 * 1024;

struct StructurizrIncludeResolver {
    Input* input;
    Element* root;
    char* root_dir;
    ArrayList* stack;
    size_t file_count;
    size_t byte_count;

    StructurizrIncludeResolver(Input* source_input, Element* source_root, char* source_dir)
        : input(source_input), root(source_root), root_dir(source_dir),
          stack(arraylist_new(8)), file_count(0), byte_count(0) {}

    ~StructurizrIncludeResolver() {
        while (stack && stack->length > 0) mem_free(arraylist_pop(stack));
        if (stack) arraylist_free(stack);
        if (root_dir) mem_free(root_dir);
    }

    static bool is_include(Element* value) {
        ElementReader reader(value);
        const char* keyword = reader.get_attr_string("keyword");
        return reader.hasTag("statement") && keyword && strcmp(keyword, "!include") == 0;
    }

    static const char* include_target(Element* directive) {
        ElementReader reader(directive);
        for (int64_t i = 0; i < reader.childCount(); i++) {
            ItemReader child = reader.childAt(i);
            if (!child.isElement()) continue;
            ElementReader argument = child.asElement();
            if (argument.hasTag("argument")) return argument.get_attr_string("value");
        }
        return nullptr;
    }

    static const char* child_context(Element* value) {
        ElementReader reader(value);
        const char* tag = reader.tagName();
        if (!tag) return "generic";
        if (strcmp(tag, "workspace") == 0) return "workspace";
        if (strcmp(tag, "model") == 0) return "model";
        if (strcmp(tag, "views") == 0) return "views";
        if (strcmp(tag, "view") == 0 || strcmp(tag, "parallel") == 0) return "view";
        if (strcmp(tag, "styles") == 0) return "styles";
        if (strcmp(tag, "style-rule") == 0) return "style";
        if (strcmp(tag, "archetypes") == 0) return "archetypes";
        if (strcmp(tag, "declaration") == 0) {
            const char* keyword = reader.get_attr_string("keyword");
            return keyword && (strstr(keyword, "deployment") || strstr(keyword, "Instance"))
                ? "deployment" : "element";
        }
        return "generic";
    }

    void diagnostic(const char* code, const char* message, Element* source,
                    const char* target) {
        MarkBuilder builder(input);
        ElementReader source_reader(source);
        ElementBuilder value = builder.element("diagnostic");
        value.attr("code", code).attr("severity", "error").attr("message", message)
            .attr("target", target ? target : "")
            .attr("source-start", source_reader.get_int_attr("source-start", 0))
            .attr("source-line", source_reader.get_int_attr("source-line", 0))
            .attr("source-column", source_reader.get_int_attr("source-column", 0));
        Element* diagnostics = builder.element("diagnostics").final().element;
        add_node_to_graph(input, diagnostics, value.final().element);
        add_node_to_graph(input, root, diagnostics);
    }

    static bool is_absolute(const char* path) {
        if (!path || !*path) return false;
#ifdef _WIN32
        return (path[0] == '/' || path[0] == '\\' ||
            (path[1] == ':' && ((path[0] >= 'A' && path[0] <= 'Z') ||
                                (path[0] >= 'a' && path[0] <= 'z'))));
#else
        return path[0] == '/';
#endif
    }

    bool within_root(const char* path) const {
        size_t root_len = strlen(root_dir);
#ifdef _WIN32
        bool prefix = _strnicmp(path, root_dir, root_len) == 0;
#else
        bool prefix = strncmp(path, root_dir, root_len) == 0;
#endif
        bool root_has_boundary = root_len > 0 &&
            (root_dir[root_len - 1] == '/' || root_dir[root_len - 1] == '\\');
        // Filesystem roots already end at a separator; requiring another boundary
        // rejects every descendant of "/" and Windows drive roots.
        return prefix && (root_has_boundary || path[root_len] == '\0' ||
                          path[root_len] == '/' || path[root_len] == '\\');
    }

    bool on_stack(const char* path) const {
        for (int i = 0; i < stack->length; i++) {
            if (strcmp((const char*)arraylist_get(stack, i), path) == 0) return true;
        }
        return false;
    }

    void annotate(Element* value, const char* source_file) {
        add_graph_attribute(input, value, "source-file", source_file);
        ElementReader reader(value);
        int64_t child_count = reader.childCount();
        for (int64_t i = 0; i < child_count; i++) {
            ItemReader child = reader.childAt(i);
            if (child.isElement()) annotate((Element*)child.asElement().element(), source_file);
        }
    }

    void resolve_children(Element* owner, const char* context, const char* containing_dir,
                          size_t depth) {
        ElementReader reader(owner);
        int64_t child_count = reader.childCount();
        for (int64_t i = 0; i < child_count; i++) {
            ItemReader child = reader.childAt(i);
            if (!child.isElement()) continue;
            Element* element = (Element*)child.asElement().element();
            if (is_include(element)) resolve_directive(element, context, containing_dir, depth);
            else resolve_children(element, child_context(element), containing_dir, depth);
        }
    }

    void resolve_file(Element* directive, const char* context, const char* path,
                      size_t depth) {
        if (depth >= STRUCTURIZR_INCLUDE_MAX_DEPTH) {
            diagnostic("structurizr.include-limit",
                "Structurizr include depth limit exceeded", directive, path);
            return;
        }
        if (on_stack(path)) {
            diagnostic("structurizr.include-cycle",
                "Structurizr local include cycle detected", directive, path);
            return;
        }
        int64_t size = file_size(path);
        if (size < 0) {
            diagnostic("structurizr.include-missing",
                "Structurizr local include file is unavailable", directive, path);
            return;
        }
        if (file_count >= STRUCTURIZR_INCLUDE_MAX_FILES ||
            (size_t)size > STRUCTURIZR_INCLUDE_MAX_BYTES - byte_count) {
            diagnostic("structurizr.include-limit",
                "Structurizr include file or byte limit exceeded", directive, path);
            return;
        }

        char* source = read_text_file(path);
        if (!source) {
            diagnostic("structurizr.include-missing",
                "Structurizr local include file could not be read", directive, path);
            return;
        }
        file_count++;
        byte_count += (size_t)size;
        arraylist_append(stack, mem_strdup(path, MEM_CAT_TEMP));
        ElementReader before(directive);
        int64_t first_new_child = before.childCount();
        parse_graph_structurizr_fragment(input, directive, source, context);
        mem_free(source);

        char* containing = file_path_dirname(path);
        ElementReader after(directive);
        for (int64_t i = first_new_child; i < after.childCount(); i++) {
            ItemReader child = after.childAt(i);
            if (!child.isElement()) continue;
            Element* element = (Element*)child.asElement().element();
            annotate(element, path);
            // An included fragment may itself start with !include; descending only
            // into that statement would inspect its argument and skip the directive.
            if (is_include(element)) resolve_directive(element, context, containing, depth + 1);
            else resolve_children(element, child_context(element), containing, depth + 1);
        }
        mem_free(containing);
        mem_free(arraylist_pop(stack));
    }

    void resolve_directory(Element* directive, const char* context, const char* path,
                           size_t depth) {
        ArrayList* entries = dir_list(path);
        if (!entries) {
            diagnostic("structurizr.include-missing",
                "Structurizr local include directory is unavailable", directive, path);
            return;
        }
        for (int i = 0; i < entries->length; i++) {
            DirEntry* entry = (DirEntry*)arraylist_get(entries, i);
            if (!entry->is_dir && !entry->is_symlink &&
                (file_path_has_ext_ci(entry->name, "dsl") ||
                 file_path_has_ext_ci(entry->name, "structurizr"))) {
                char* joined = file_path_join(path, entry->name);
                char* canonical = joined ? file_realpath(joined) : nullptr;
                if (canonical) resolve_file(directive, context, canonical, depth);
                else diagnostic("structurizr.include-missing",
                    "Structurizr local include file is unavailable", directive, joined);
                if (canonical) mem_free(canonical);
                if (joined) mem_free(joined);
            }
            dir_entry_free(entry);
        }
        arraylist_free(entries);
    }

    void resolve_directive(Element* directive, const char* context,
                           const char* containing_dir, size_t depth) {
        const char* target = include_target(directive);
        if (!target || !*target) {
            diagnostic("structurizr.include-missing",
                "Structurizr include requires a local path", directive, target);
            return;
        }
        if (strstr(target, "://")) {
            diagnostic("structurizr.include-policy",
                "Structurizr network includes are disabled", directive, target);
            return;
        }
        char* joined = is_absolute(target)
            ? mem_strdup(target, MEM_CAT_TEMP) : file_path_join(containing_dir, target);
        char* canonical = joined ? file_realpath(joined) : nullptr;
        if (!canonical) {
            diagnostic("structurizr.include-missing",
                "Structurizr local include path is unavailable", directive, target);
        } else if (!within_root(canonical)) {
            diagnostic("structurizr.include-policy",
                "Structurizr include escapes the allowed root", directive, target);
        } else if (file_is_dir(canonical)) {
            resolve_directory(directive, context, canonical, depth);
        } else {
            resolve_file(directive, context, canonical, depth);
        }
        if (canonical) mem_free(canonical);
        if (joined) mem_free(joined);
    }

    void resolve(const char* root_path) {
        char* canonical_root = file_realpath(root_path);
        if (!canonical_root) return;
        arraylist_append(stack, canonical_root);
        resolve_children(root, "workspace", root_dir, 0);
        mem_free(arraylist_pop(stack));
        add_graph_integer_attribute(input, root, "include-file-count", (int64_t)file_count);
        add_graph_integer_attribute(input, root, "include-byte-count", (int64_t)byte_count);
    }
};

void resolve_graph_structurizr_local_includes(Input* input, const char* root_path) {
    if (!input || !root_path || input->root.type_id() != LMD_TYPE_ELEMENT) return;
    char* canonical_root = file_realpath(root_path);
    if (!canonical_root) return;
    char* root_dir = file_path_dirname(canonical_root);
    mem_free(canonical_root);
    if (!root_dir) return;
    StructurizrIncludeResolver resolver(input, input->root.element, root_dir);
    resolver.resolve(root_path);
}
