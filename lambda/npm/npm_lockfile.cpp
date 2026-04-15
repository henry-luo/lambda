// npm_lockfile.cpp — Read/write lambda-node.lock (JSON format)

#include "npm_lockfile.h"
#include "../lib/file.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/mempool.h"
#include "../lib/stringbuf.h"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
// forward-declare JSON parser to avoid transitive input.hpp linkage issues
class Input;
Item parse_json_to_item(Input* input, const char* json_string);

#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Create / Free
// ---------------------------------------------------------------------------

NpmLockFile* npm_lockfile_create(void) {
    NpmLockFile* lf = (NpmLockFile*)mem_calloc(1, sizeof(NpmLockFile), MEM_CAT_JS_RUNTIME);
    lf->version = 1;
    lf->entry_cap = 32;
    lf->entries = (NpmLockEntry*)mem_calloc(lf->entry_cap, sizeof(NpmLockEntry), MEM_CAT_JS_RUNTIME);
    return lf;
}

void npm_lockfile_free(NpmLockFile* lockfile) {
    if (!lockfile) return;
    for (int i = 0; i < lockfile->entry_count; i++) {
        NpmLockEntry* e = &lockfile->entries[i];
        if (e->name) mem_free(e->name);
        if (e->version) mem_free(e->version);
        if (e->resolved) mem_free(e->resolved);
        if (e->integrity) mem_free(e->integrity);
        for (int j = 0; j < e->dep_count; j++) {
            if (e->dep_names[j]) mem_free(e->dep_names[j]);
            if (e->dep_versions[j]) mem_free(e->dep_versions[j]);
        }
        if (e->dep_names) mem_free(e->dep_names);
        if (e->dep_versions) mem_free(e->dep_versions);
    }
    if (lockfile->entries) mem_free(lockfile->entries);
    mem_free(lockfile);
}

// ---------------------------------------------------------------------------
// Add entry
// ---------------------------------------------------------------------------

void npm_lockfile_add(NpmLockFile* lockfile, const char* key,
                      const char* version, const char* resolved,
                      const char* integrity,
                      const char** dep_names, const char** dep_versions,
                      int dep_count) {
    if (!lockfile || !key) return;

    // grow if needed
    if (lockfile->entry_count >= lockfile->entry_cap) {
        lockfile->entry_cap *= 2;
        lockfile->entries = (NpmLockEntry*)mem_realloc(
            lockfile->entries,
            lockfile->entry_cap * sizeof(NpmLockEntry),
            MEM_CAT_JS_RUNTIME);
    }

    NpmLockEntry* e = &lockfile->entries[lockfile->entry_count++];
    memset(e, 0, sizeof(NpmLockEntry));
    e->name = mem_strdup(key, MEM_CAT_JS_RUNTIME);
    e->version = version ? mem_strdup(version, MEM_CAT_JS_RUNTIME) : NULL;
    e->resolved = resolved ? mem_strdup(resolved, MEM_CAT_JS_RUNTIME) : NULL;
    e->integrity = integrity ? mem_strdup(integrity, MEM_CAT_JS_RUNTIME) : NULL;

    if (dep_count > 0 && dep_names && dep_versions) {
        e->dep_names = (char**)mem_calloc(dep_count, sizeof(char*), MEM_CAT_JS_RUNTIME);
        e->dep_versions = (char**)mem_calloc(dep_count, sizeof(char*), MEM_CAT_JS_RUNTIME);
        for (int i = 0; i < dep_count; i++) {
            e->dep_names[i] = mem_strdup(dep_names[i], MEM_CAT_JS_RUNTIME);
            e->dep_versions[i] = mem_strdup(dep_versions[i], MEM_CAT_JS_RUNTIME);
        }
        e->dep_count = dep_count;
    }
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

const NpmLockEntry* npm_lockfile_lookup(const NpmLockFile* lockfile, const char* key) {
    if (!lockfile || !key) return NULL;
    for (int i = 0; i < lockfile->entry_count; i++) {
        if (lockfile->entries[i].name && strcmp(lockfile->entries[i].name, key) == 0) {
            return &lockfile->entries[i];
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Read (parse JSON)
// ---------------------------------------------------------------------------

NpmLockFile* npm_lockfile_read(const char* path) {
    char* content = read_text_file(path);
    if (!content) return NULL;

    Pool* pool = pool_create();
    Input* input = Input::create(pool);
    Item root = parse_json_to_item(input, content);
    mem_free(content);

    if (root.item == ITEM_NULL) {
        pool_destroy(pool);
        return NULL;
    }

    ItemReader root_reader(root.to_const());
    if (!root_reader.isMap()) {
        pool_destroy(pool);
        return NULL;
    }

    MapReader root_map = root_reader.asMap();

    NpmLockFile* lf = npm_lockfile_create();

    // version
    ItemReader ver_item = root_map.get("version");
    if (ver_item.isInt()) {
        lf->version = (int)ver_item.asInt();
    }

    // packages
    ItemReader pkgs_item = root_map.get("packages");
    if (pkgs_item.isMap()) {
        MapReader pkgs_map = pkgs_item.asMap();
        auto entries = pkgs_map.entries();
        const char* key = NULL;
        ItemReader val;

        while (entries.next(&key, &val)) {
            if (!val.isMap()) continue;
            MapReader entry_map = val.asMap();

            const char* resolved = NULL;
            const char* integrity = NULL;
            ItemReader resolved_item = entry_map.get("resolved");
            if (resolved_item.isString()) resolved = resolved_item.cstring();
            ItemReader integrity_item = entry_map.get("integrity");
            if (integrity_item.isString()) integrity = integrity_item.cstring();

            // extract version from key (e.g. "lodash@4.17.21" → "4.17.21")
            const char* at = strrchr(key, '@');
            const char* version_str = at ? at + 1 : NULL;

            // parse dependencies
            const char* d_names[256];
            const char* d_versions[256];
            int d_count = 0;

            ItemReader deps_item = entry_map.get("dependencies");
            if (deps_item.isMap()) {
                MapReader deps_map = deps_item.asMap();
                auto dep_entries = deps_map.entries();
                const char* dk = NULL;
                ItemReader dv;
                while (dep_entries.next(&dk, &dv) && d_count < 256) {
                    d_names[d_count] = dk;
                    d_versions[d_count] = dv.cstring() ? dv.cstring() : "*";
                    d_count++;
                }
            }

            npm_lockfile_add(lf, key, version_str, resolved, integrity,
                           d_names, d_versions, d_count);
        }
    }

    pool_destroy(pool);
    return lf;
}

// ---------------------------------------------------------------------------
// Write (generate JSON)
// ---------------------------------------------------------------------------

// escape a JSON string value (minimal: just backslash and quotes)
static void json_escape_string(StringBuf* sb, const char* s) {
    stringbuf_append_char(sb, '"');
    if (s) {
        for (const char* p = s; *p; p++) {
            switch (*p) {
                case '"':  stringbuf_append_str(sb, "\\\""); break;
                case '\\': stringbuf_append_str(sb, "\\\\"); break;
                case '\n': stringbuf_append_str(sb, "\\n"); break;
                case '\r': stringbuf_append_str(sb, "\\r"); break;
                case '\t': stringbuf_append_str(sb, "\\t"); break;
                default:   stringbuf_append_char(sb, *p); break;
            }
        }
    }
    stringbuf_append_char(sb, '"');
}

int npm_lockfile_write(const NpmLockFile* lockfile, const char* path) {
    if (!lockfile || !path) return -1;

    Pool* pool = pool_create();
    StringBuf* sb = stringbuf_new(pool);

    stringbuf_append_str(sb, "{\n");
    stringbuf_append_str(sb, "  \"version\": 1,\n");
    stringbuf_append_str(sb, "  \"packages\": {\n");

    for (int i = 0; i < lockfile->entry_count; i++) {
        const NpmLockEntry* e = &lockfile->entries[i];
        if (!e->name) continue;

        stringbuf_append_str(sb, "    ");
        json_escape_string(sb, e->name);
        stringbuf_append_str(sb, ": {\n");

        if (e->resolved) {
            stringbuf_append_str(sb, "      \"resolved\": ");
            json_escape_string(sb, e->resolved);
            stringbuf_append_str(sb, ",\n");
        }
        if (e->integrity) {
            stringbuf_append_str(sb, "      \"integrity\": ");
            json_escape_string(sb, e->integrity);
            stringbuf_append_str(sb, ",\n");
        }

        stringbuf_append_str(sb, "      \"dependencies\": {");
        if (e->dep_count > 0) {
            stringbuf_append_str(sb, "\n");
            for (int j = 0; j < e->dep_count; j++) {
                stringbuf_append_str(sb, "        ");
                json_escape_string(sb, e->dep_names[j]);
                stringbuf_append_str(sb, ": ");
                json_escape_string(sb, e->dep_versions[j]);
                if (j < e->dep_count - 1) stringbuf_append_str(sb, ",");
                stringbuf_append_str(sb, "\n");
            }
            stringbuf_append_str(sb, "      ");
        }
        stringbuf_append_str(sb, "}\n");

        stringbuf_append_str(sb, "    }");
        if (i < lockfile->entry_count - 1) stringbuf_append_str(sb, ",");
        stringbuf_append_str(sb, "\n");
    }

    stringbuf_append_str(sb, "  }\n");
    stringbuf_append_str(sb, "}\n");

    String* result = stringbuf_to_string(sb);
    int ret = write_text_file_atomic(path, result->chars);

    pool_destroy(pool);
    return ret;
}
