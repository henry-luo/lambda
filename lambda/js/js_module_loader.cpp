#include "js_execution_internal.hpp"
#include "../core/mark_reader.hpp"
#include "../input/input.hpp"
#include "../jube/jube_registry.h"
#include "../../lib/file.h"
#include "../../lib/url.h"
#include "../../lib/mem.h"
#include <limits.h>

static bool jm_path_has_known_js_ext(const char* path) {
    int len = path ? (int)strlen(path) : 0;
    return (len >= 3 && strcmp(path + len - 3, ".js") == 0) ||
           (len >= 4 && strcmp(path + len - 4, ".mjs") == 0) ||
           (len >= 4 && strcmp(path + len - 4, ".cjs") == 0) ||
           (len >= 5 && strcmp(path + len - 5, ".json") == 0) ||
           jm_path_is_lambda_source(path);
}

static bool jm_module_specifier_is_url_path(const char* specifier, int spec_len) {
    if (!specifier || spec_len <= 0) return false;
    return specifier[0] == '/' ||
        (specifier[0] == '.' && spec_len >= 2 && specifier[1] == '/') ||
        (specifier[0] == '.' && spec_len >= 3 && specifier[1] == '.' &&
            specifier[2] == '/') ||
        (spec_len >= 7 && strncmp(specifier, "http://", 7) == 0) ||
        (spec_len >= 8 && strncmp(specifier, "https://", 8) == 0);
}

static bool jm_resolve_http_module_path(const char* base_file,
        const char* specifier, int spec_len, char* out, int out_size) {
    if (!js_path_is_http_url(base_file) ||
            !jm_module_specifier_is_url_path(specifier, spec_len) ||
            spec_len >= 2048) {
        return false;
    }

    char specifier_text[2048];
    snprintf(specifier_text, sizeof(specifier_text), "%.*s", spec_len, specifier);
    Url* base_url = url_parse(base_file);
    Url* resolved_url = base_url
        ? url_parse_with_base(specifier_text, base_url) : NULL;
    const char* href = resolved_url ? url_get_href(resolved_url) : NULL;
    bool fits = href && (int)strlen(href) < out_size;
    if (fits) {
        // Browser modules resolve path and root-relative specifiers as URLs.
        snprintf(out, out_size, "%s", href);
    }
    if (resolved_url) url_destroy(resolved_url);
    if (base_url) url_destroy(base_url);
    return fits;
}

// Resolve a module specifier relative to the importing file's directory
static void jm_resolve_module_path_unmapped(const char* base_file, const char* specifier, int spec_len,
                                   char* out, int out_size) {
    if (jm_resolve_http_module_path(base_file, specifier, spec_len, out, out_size)) {
        return;
    }
    const char* last_slash = strrchr(base_file, '/');
#ifdef _WIN32
    // canonical Windows module paths use backslashes after file_realpath().
    const char* last_backslash = strrchr(base_file, '\\');
    if (last_backslash && (!last_slash || last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
#endif
    int dir_len = last_slash ? (int)(last_slash - base_file + 1) : 0;

    if (spec_len >= 2 && specifier[0] == '.' && specifier[1] == '/') {
        // Relative: ./utils.js → dir/utils.js
        snprintf(out, out_size, "%.*s%.*s", dir_len, base_file, spec_len - 2, specifier + 2);
    } else if (spec_len >= 3 && specifier[0] == '.' && specifier[1] == '.' && specifier[2] == '/') {
        // Parent: ../utils.js
        snprintf(out, out_size, "%.*s%.*s", dir_len, base_file, spec_len, specifier);
    } else if (spec_len >= 1 && specifier[0] == '/') {
        // Absolute path
        snprintf(out, out_size, "%.*s", spec_len, specifier);
    } else {
        // Bare specifiers remain unchanged here; external package resolution
        // is not part of the Lambda host link closure.
        char spec_buf[512];
        snprintf(spec_buf, sizeof(spec_buf), "%.*s", spec_len, specifier);

        // skip node: builtins (handled by js_module_get)
        bool has_node_prefix = (spec_len >= 5 && strncmp(specifier, "node:", 5) == 0);

        bool is_builtin = has_node_prefix || jube_specifier_is_builtin(spec_buf);

        // Built-ins are checked by js_module_get; non-built-in package names
        // fall through to the normal file and directory probes below.
        snprintf(out, out_size, "%.*s", spec_len, specifier);
        if (is_builtin) return;  // builtins don't need .js extension
    }

    // If doesn't end in a known JS extension, use Node-style file and
    // directory fallbacks for static literal require/import resolution.
    // Recognized extensions: .js, .mjs, .cjs, .json, .ls
    int len = (int)strlen(out);
    bool has_node_prefix = (len >= 5 && strncmp(out, "node:", 5) == 0);
    if (!has_node_prefix) {
        bool has_ext = jm_path_has_known_js_ext(out);
        if (!has_ext) {
            if (file_is_file(out)) return;

            char js_candidate[512];
            if (len + 3 < (int)sizeof(js_candidate)) {
                snprintf(js_candidate, sizeof(js_candidate), "%s.js", out);
                if (file_is_file(js_candidate) && (int)strlen(js_candidate) < out_size) {
                    snprintf(out, out_size, "%s", js_candidate);
                    return;
                }
            }

            char index_candidate[512];
            const char* sep = (len > 0 && out[len - 1] == '/') ? "" : "/";
            if (len + (int)strlen(sep) + 8 < (int)sizeof(index_candidate)) {
                snprintf(index_candidate, sizeof(index_candidate), "%s%sindex.js", out, sep);
                if (file_is_file(index_candidate) && (int)strlen(index_candidate) < out_size) {
                    snprintf(out, out_size, "%s", index_candidate);
                    return;
                }
            }

            if (len + 3 < (int)out_size) {
                snprintf(out + len, out_size - (size_t)len, "%s", ".js");
            }
        }
    }
}

static bool jm_import_map_address(const char* base,const char* address,char* out,int capacity) {
    if (!base || !address || (!strchr(address,':') && address[0]!='.' && address[0]!='/')) return false;
    Url* base_url=url_parse_path_or_url(base,nullptr);
    Url* target=base_url?url_parse_with_base(address,base_url):nullptr;
    const char* path=target?(url_get_scheme(target)==URL_SCHEME_FILE?url_get_pathname(target):url_get_href(target)):nullptr;
    bool valid=path && strlen(path)<(size_t)capacity;
    if(valid) str_copy(out,capacity,path,strlen(path));
    if(target) url_destroy(target);if(base_url) url_destroy(base_url);return valid;
}
static bool jm_import_map_match(Runtime* runtime,MapReader mappings,const char* requested,char* out,int capacity,bool* blocked) {
    bool found=false,prefix=false;ItemReader address;size_t size=0;
    auto entries=mappings.entries();const char* key;ItemReader value;
    while(entries.next(&key,&value)) {
        char normalized[2048];const char* match=jm_import_map_address(runtime->js_document_base_url,key,normalized,sizeof(normalized))?normalized:key;
        size_t length=strlen(match);
        if(!length || length<size || (strcmp(match,requested)!=0 && (match[length-1]!='/'||strncmp(match,requested,length)!=0))) continue;
        found=true;prefix=match[length-1]=='/';address=value;size=length;
    }
    if(!found) return false;
    *blocked=true;
    if(!address.isString()) return true;
    const char* target=address.cstring();size_t length=strlen(target);
    if(prefix && (!length||target[length-1]!='/')) return true;
    char resolved[2048];if(!jm_import_map_address(runtime->js_document_base_url,target,resolved,sizeof(resolved))) return true;
    if(strlen(resolved)+strlen(requested+size)>=(size_t)capacity) return true;
    str_copy(out,capacity,resolved,strlen(resolved));
    if(prefix) str_cat(out,strlen(out),capacity,requested+size,strlen(requested+size));
    *blocked=false;return true;
}
static bool jm_resolve_import_map(Runtime* runtime,const char* importer,const char* specifier,int length,char* out,int capacity) {
    if(!runtime || !runtime->js_document_base_url || get_type_id(runtime->js_import_maps)!=LMD_TYPE_ARRAY || length<=0 || length>=2048) return false;
    char request[2048];str_copy(request,sizeof(request),specifier,length);
    char normalized[2048];if(jm_import_map_address(importer?importer:runtime->js_document_base_url,request,normalized,sizeof(normalized))) str_copy(request,sizeof(request),normalized,strlen(normalized));
    ArrayReader maps=ArrayReader(runtime->js_import_maps.array);
    for(int64_t i=0;i<maps.length();i++) {
        MapReader map=maps.get(i).asMap();MapReader scopes=map.get("scopes").asMap();
        // a missing entry in the most specific scope falls through its enclosing scopes.
        size_t limit=(size_t)-1;bool blocked=false;
        for(;;) {
            MapReader scoped;size_t longest=0;auto entries=scopes.entries();const char* key;ItemReader value;
            while(entries.next(&key,&value)) {
                char scope[2048];if(!jm_import_map_address(runtime->js_document_base_url,key,scope,sizeof(scope))) continue;
                size_t n=strlen(scope);if(importer && n>longest && n<limit && strncmp(importer,scope,n)==0 && value.isMap()) { scoped=value.asMap();longest=n; }
            }
            if(!longest) break;
            if(jm_import_map_match(runtime,scoped,request,out,capacity,&blocked)) { if(blocked) out[0]='\0';return true; }
            limit=longest;
        }
        if(jm_import_map_match(runtime,map.get("imports").asMap(),request,out,capacity,&blocked)) {
            if(blocked) out[0]='\0';return true;
        }
    }
    return false;
}
const char* jm_document_script_base_url(Runtime* runtime, const char* script_reference) {
    // inline modules retain private registry identities while their host URL
    // remains the document's prepared base, including across history updates.
    return runtime && runtime->js_document_base_url && script_reference &&
        script_reference[0] == '<' ? runtime->js_document_base_url : script_reference;
}

void jm_resolve_module_path(const char* base_file,const char* specifier,int length,char* out,int capacity) {
    base_file = jm_document_script_base_url(js_current_runtime(), base_file);
    if(jm_resolve_import_map(js_current_runtime(),base_file,specifier,length,out,capacity)) return;
    jm_resolve_module_path_unmapped(base_file,specifier,length,out,capacity);
}

bool jm_resolve_document_module_path(Runtime* runtime, const char* script_reference,
        const char* specifier, int spec_len, char* out, int out_size) {
    const char* base_url = jm_document_script_base_url(runtime, script_reference);
    if (jm_resolve_import_map(runtime,base_url,specifier,spec_len,out,out_size)) return true;
    if (!runtime || !runtime->js_document_base_url || !runtime->js_document_base_url[0] ||
            (script_reference && script_reference[0] != '<')) {
        return false;
    }
    // Inline page scripts have synthetic labels for diagnostics, but the web
    // platform resolves their module specifiers from the document URL.
    jm_resolve_module_path(runtime->js_document_base_url, specifier, spec_len, out, out_size);
    return true;
}


