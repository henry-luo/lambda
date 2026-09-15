// npm_lockfile.cpp — Read/write lambda-node.lock (JSON format)

#include "npm_lockfile.h"
#include "../../../lib/file.h"
#include "../../../lib/log.h"
#include "../../../lib/mem_grow.hpp"
#include "../../../lib/memtrack.h"
#include "../../../lib/mempool.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/escape.h"
#include "../../../lib/stringbuf.h"
#include "../../../lambda-data.hpp"
#include "../../../core/mark_reader.hpp"
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
    if (!lf) return NULL;
    lf->version = 1;
    return lf;
}

static void npm_lock_entry_free(NpmLockEntry* entry) {
    if (!entry) return;
    mem_free(entry->name);
    mem_free(entry->version);
    mem_free(entry->resolved);
    mem_free(entry->integrity);
    for (int i = 0; i < entry->dep_count; i++) {
        mem_free(entry->dep_names[i]);
        mem_free(entry->dep_versions[i]);
    }
    mem_free(entry->dep_names);
    mem_free(entry->dep_versions);
    memset(entry, 0, sizeof(*entry));
}

void npm_lockfile_free(NpmLockFile* lockfile) {
    if (!lockfile) return;
    for (int i = 0; i < lockfile->entry_count; i++) {
        npm_lock_entry_free(&lockfile->entries[i]);
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
    if (!lockfile || !key || dep_count < 0 || lockfile->entry_count == INT_MAX) return;

    if (!lam::mem_grow_array(&lockfile->entries, &lockfile->entry_cap,
                             lockfile->entry_count + 1, 32, MEM_CAT_JS_RUNTIME)) {
        return;
    }

    NpmLockEntry* entry = &lockfile->entries[lockfile->entry_count++];
    memset(entry, 0, sizeof(*entry));
    entry->name = mem_strdup(key, MEM_CAT_JS_RUNTIME);
    entry->version = version ? mem_strdup(version, MEM_CAT_JS_RUNTIME) : NULL;
    entry->resolved = resolved ? mem_strdup(resolved, MEM_CAT_JS_RUNTIME) : NULL;
    entry->integrity = integrity ? mem_strdup(integrity, MEM_CAT_JS_RUNTIME) : NULL;

    if (dep_count > 0 && dep_names && dep_versions) {
        entry->dep_names = (char**)mem_calloc(dep_count, sizeof(char*), MEM_CAT_JS_RUNTIME);
        entry->dep_versions = (char**)mem_calloc(dep_count, sizeof(char*), MEM_CAT_JS_RUNTIME);
        for (int i = 0; i < dep_count; i++) {
            entry->dep_names[i] = mem_strdup(dep_names[i], MEM_CAT_JS_RUNTIME);
            entry->dep_versions[i] = mem_strdup(dep_versions[i], MEM_CAT_JS_RUNTIME);
        }
        entry->dep_count = dep_count;
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

    Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "npm.lockfile");
    Input* input = Input::create(pool);
    Item root = parse_json_to_item(input, content);
    mem_free(content);

    if (root.item == ITEM_NULL) {
        mem_pool_destroy(pool);
        return NULL;
    }

    ItemReader root_reader(root.to_const());
    if (!root_reader.isMap()) {
        mem_pool_destroy(pool);
        return NULL;
    }

    MapReader root_map = root_reader.asMap();

    NpmLockFile* lf = npm_lockfile_create();
    if (!lf) {
        mem_pool_destroy(pool);
        return NULL;
    }

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

    mem_pool_destroy(pool);
    return lf;
}

// ---------------------------------------------------------------------------
// Write (generate JSON)
// ---------------------------------------------------------------------------

static void json_escape_string(StringBuf* sb, const char* s) {
    const char* value = s ? s : "";
    escape_append_json_stringbuf(sb, value, strlen(value), true, false);
}

int npm_lockfile_write(const NpmLockFile* lockfile, const char* path) {
    if (!lockfile || !path) return -1;

    Pool* pool = mem_pool_create(NULL, MEM_ROLE_INPUT, "npm.lockfile");
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

    mem_pool_destroy(pool);
    return ret;
}
