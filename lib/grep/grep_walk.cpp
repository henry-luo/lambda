// grep_walk.cpp — searching files and directory trees (vibe/Lambda_Lib_Grep.md §9).
//
// Traversal and search share one thread pool (GRP13): a directory job lists
// its entries and submits a job per subdirectory and per file. The walk itself
// (grep_walk_paths) only selects files and hands each to a visitor on a worker
// slot; lib/fts reuses it (FTX10). grep_search_paths is a visitor: each file
// borrows its slot's searcher, so there is one RE2 per worker (GRP6). In sorted
// mode a file's records are buffered and delivered on the calling thread in
// path order once everything is searched, which makes total limits
// deterministic (GRP25); otherwise records go to the sink as files finish, one
// at a time.

#include "grep_walk.hpp"
#include "../arraylist.h"
#include "../byte_builder.h"
#include "../file.h"
#include "../file_utils.h"
#include "../log.h"
#include "../memtrack.h"
#include "../thread_pool.h"

#include <pthread.h>
#include <string.h>

#define GREP_DEFAULT_MAX_THREADS 8

// non-hidden directory names that are dependencies or caches in every
// ecosystem that uses them (GRP14 layer 3); ambiguous names such as build,
// dist or target are left to ignore files
static const char* const GREP_BUILTIN_SKIP_DIRS[] = {
    "node_modules", "bower_components", "__pycache__", "venv", "site-packages",
};

struct GrepRec {
    bool context;
    bool has_line;
    GrepLineEnding line_ending;
    uint64_t byte_offset;
    uint64_t char_offset;
    uint64_t line_number;
    size_t text_off, text_len;
    size_t line_off, line_len;
};

struct GrepFileResult {
    char* path;               // reported path (mem-owned)
    size_t root_index;
    GrepRec* recs;
    size_t nrec, cap;
    ByteBuilder bytes;        // the text the records point into
    uint64_t match_count;     // from file_done: selected records, or lines counted (GRP30)
    uint64_t last_line_abs;   // the previous record's line, to store a line once
    size_t last_line_len, last_line_off;
    bool has_last_line;
    bool searched;            // reached file_done
    bool failed;
};

// the walk: selection, threads and worker slots
struct GrepWalk {
    const GrepWalkOptions* w;
    const GrepWalkVisitor* visitor;
    ThreadPool* tp;           // NULL: jobs run inline on the calling thread
    pthread_mutex_t mu;       // guards everything below
    ArrayList* free_slots;    // slot ids (as uintptr_t) not held by a visit
    ArrayList* nodes;         // GrepIgnoreNode* to free at the end
    GrepIgnoreRule* includes;
    GrepIgnoreRule* excludes;
    size_t include_count, exclude_count;
    bool stop;
};

// grep_search_paths' visitor state
struct GrepSearchRun {
    const GrepMatcher* m;
    const GrepWalkOptions* w;
    const GrepSink* user;
    pthread_mutex_t mu;       // guards everything below, and serializes direct delivery
    GrepSearcher** searchers; // one per walk slot, created on first use (GRP6)
    int slot_count;
    ArrayList* results;       // GrepFileResult* (sorted mode)
    bool sorted;
    bool stop;
    uint64_t delivered;       // direct mode: selected records delivered
    char* limit_path;         // direct mode: the file the total limit ended, owed its file_done
};

struct GrepDirJob {
    GrepWalk* walk;
    char* full;               // path to open (as reported: the root plus rel)
    char* rel;                // relative to the walk root, '/'-separated
    int depth;
    size_t root_index;
    GrepIgnoreNode* node;
};

struct GrepFileJob {
    GrepWalk* walk;
    char* open_path;
    char* label;
    size_t root_index;
};

static bool walk_stopped(GrepWalk* w) {
    pthread_mutex_lock(&w->mu);
    bool stop = w->stop;
    pthread_mutex_unlock(&w->mu);
    return stop;
}

static void walk_submit(GrepWalk* w, TpJobFn fn, void* job) {
    if (w->tp && tp_submit(w->tp, fn, job)) return;
    fn(job);
}

static char* join_path(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la == 0) return mem_strdup(b, MEM_CAT_TEMP);
    bool sep = a[la - 1] != '/';
    char* out = (char*)mem_alloc(la + lb + 2, MEM_CAT_TEMP);
    if (!out) return NULL;
    memcpy(out, a, la);
    if (sep) out[la++] = '/';
    memcpy(out + la, b, lb + 1);
    return out;
}

// ── worker slots ───────────────────────────────────────────────────────

static int acquire_slot(GrepWalk* w) {
    pthread_mutex_lock(&w->mu);
    int slot = arraylist_length(w->free_slots) > 0 ? (int)(uintptr_t)arraylist_pop(w->free_slots) : -1;
    pthread_mutex_unlock(&w->mu);
    return slot;
}

static void release_slot(GrepWalk* w, int slot) {
    pthread_mutex_lock(&w->mu);
    arraylist_append(w->free_slots, (void*)(uintptr_t)slot);
    pthread_mutex_unlock(&w->mu);
}

// ── sorted mode: buffering ─────────────────────────────────────────────

static GrepAction buffer_record(GrepFileResult* r, const GrepMatch* gm, bool context) {
    if (r->failed) return GREP_SKIP_FILE;
    if (r->nrec == r->cap) {
        size_t cap = r->cap ? r->cap * 2 : 16;
        GrepRec* grown = (GrepRec*)mem_realloc(r->recs, cap * sizeof(GrepRec), MEM_CAT_TEMP);
        if (!grown) {
            r->failed = true;
            return GREP_SKIP_FILE;
        }
        r->recs = grown;
        r->cap = cap;
    }
    GrepRec* rec = &r->recs[r->nrec];
    memset(rec, 0, sizeof(*rec));
    rec->context = context;
    rec->byte_offset = gm->byte_offset;
    rec->char_offset = gm->char_offset;
    rec->line_number = gm->line_number;
    rec->line_ending = gm->line_ending;
    rec->text_off = r->bytes.length;
    rec->text_len = gm->length;
    bool ok = byte_builder_append(&r->bytes, gm->text, gm->length);
    if (ok && gm->line) {
        // several matches in one line store the line once
        uint64_t line_abs = gm->byte_offset - (uint64_t)(gm->text - gm->line);
        if (r->has_last_line && r->last_line_abs == line_abs && r->last_line_len == gm->line_length) {
            rec->line_off = r->last_line_off;
        } else {
            rec->line_off = r->bytes.length;
            ok = byte_builder_append(&r->bytes, gm->line, gm->line_length);
            r->last_line_abs = line_abs;
            r->last_line_len = gm->line_length;
            r->last_line_off = rec->line_off;
            r->has_last_line = true;
        }
        rec->line_len = gm->line_length;
        rec->has_line = true;
    }
    if (!ok) {
        r->failed = true;
        return GREP_SKIP_FILE;
    }
    r->nrec++;
    return GREP_CONTINUE;
}

static GrepAction buffer_matched(void* ud, const GrepMatch* gm) { return buffer_record((GrepFileResult*)ud, gm, false); }
static GrepAction buffer_context(void* ud, const GrepMatch* gm) { return buffer_record((GrepFileResult*)ud, gm, true); }
static GrepAction buffer_file_done(void* ud, const char*, uint64_t count) {
    GrepFileResult* r = (GrepFileResult*)ud;
    r->match_count = count;
    r->searched = true;
    return GREP_CONTINUE;
}

static void free_result(GrepFileResult* r) {
    if (!r) return;
    if (r->path) mem_free(r->path);
    if (r->recs) mem_free(r->recs);
    byte_builder_destroy(&r->bytes);
    mem_free(r);
}

// path order compares component by component: '/' sorts before every other
// byte, so a directory's entries stay together (depth-first order)
int grep_path_order(const char* a, const char* b) {
    for (;; a++, b++) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca == cb) {
            if (!ca) return 0;
            continue;
        }
        if (ca == '/') ca = 1;
        if (cb == '/') cb = 1;
        if (!*a) ca = 0;
        if (!*b) cb = 0;
        return ca < cb ? -1 : 1;
    }
}

static int result_order(ArrayListValue va, ArrayListValue vb) {
    const GrepFileResult* a = (const GrepFileResult*)va;
    const GrepFileResult* b = (const GrepFileResult*)vb;
    if (a->root_index != b->root_index) return a->root_index < b->root_index ? -1 : 1;
    return grep_path_order(a->path, b->path);
}

static void deliver_sorted(GrepSearchRun* w) {
    arraylist_sort(w->results, result_order);
    const GrepSink* user = w->user;
    uint64_t limit = w->w->max_matches_total;
    uint64_t total = 0;
    for (int i = 0; i < arraylist_length(w->results); i++) {
        GrepFileResult* r = (GrepFileResult*)arraylist_get(w->results, i);
        if (!r->searched || r->failed) continue;
        uint64_t here = 0;
        bool stop = false, skip = false, cut = false;
        if (w->m->options.count_lines) {
            // a count buffers no records: the limit cuts the number (GRP30)
            here = limit && r->match_count > limit - total ? limit - total : r->match_count;
            total += here;
        }
        for (size_t k = 0; k < r->nrec && !skip; k++) {
            const GrepRec* rec = &r->recs[k];
            if (!rec->context && limit && total >= limit) {
                // the limit ends this file early: it still gets its file_done
                cut = true;
                break;
            }
            GrepMatch gm;
            memset(&gm, 0, sizeof(gm));
            gm.path = r->path;
            gm.text = (const char*)r->bytes.data + rec->text_off;
            gm.length = rec->text_len;
            gm.byte_offset = rec->byte_offset;
            gm.char_offset = rec->char_offset;
            gm.line_number = rec->line_number;
            gm.line_ending = rec->line_ending;
            if (rec->has_line) {
                gm.line = (const char*)r->bytes.data + rec->line_off;
                gm.line_length = rec->line_len;
            }
            GrepAction action = GREP_CONTINUE;
            if (rec->context) {
                if (user->context) action = user->context(user->user_data, &gm);
            } else {
                if (user->matched) action = user->matched(user->user_data, &gm);
                total++;
                here++;
            }
            if (action == GREP_STOP) stop = true;
            if (action != GREP_CONTINUE) skip = true;
        }
        if (stop) break;
        if (user->file_done && user->file_done(user->user_data, r->path, here) == GREP_STOP) break;
        if (cut || (limit && total >= limit)) break;
    }
}

// ── direct mode: serialized delivery ───────────────────────────────────

static GrepAction direct_matched(void* ud, const GrepMatch* gm) {
    GrepSearchRun* w = (GrepSearchRun*)ud;
    uint64_t limit = w->w->max_matches_total;
    pthread_mutex_lock(&w->mu);
    GrepAction action = GREP_STOP;
    if (!w->stop) {
        action = w->user->matched ? w->user->matched(w->user->user_data, gm) : GREP_CONTINUE;
        w->delivered++;
        if (action == GREP_STOP) {
            w->stop = true;
        } else if (limit && w->delivered >= limit) {
            // the limit ends this file early: it still gets its file_done, as
            // in sorted mode; every other file stops
            w->stop = true;
            w->limit_path = mem_strdup(gm->path ? gm->path : "", MEM_CAT_TEMP);
            action = GREP_SKIP_FILE;
        }
    }
    pthread_mutex_unlock(&w->mu);
    return action;
}

static GrepAction direct_context(void* ud, const GrepMatch* gm) {
    GrepSearchRun* w = (GrepSearchRun*)ud;
    pthread_mutex_lock(&w->mu);
    GrepAction action = GREP_STOP;
    if (!w->stop) {
        action = w->user->context(w->user->user_data, gm);
        if (action == GREP_STOP) w->stop = true;
    }
    pthread_mutex_unlock(&w->mu);
    return action;
}

static GrepAction direct_file_done(void* ud, const char* path, uint64_t count) {
    GrepSearchRun* w = (GrepSearchRun*)ud;
    uint64_t limit = w->w->max_matches_total;
    pthread_mutex_lock(&w->mu);
    GrepAction action = GREP_STOP;
    bool cut = w->limit_path && path && strcmp(path, w->limit_path) == 0;
    if (!w->stop || cut) {
        if (w->m->options.count_lines && limit) {
            // a count arrives only here: the limit cuts the number (GRP30)
            if (count > limit - w->delivered) count = limit - w->delivered;
            w->delivered += count;
            cut = w->delivered >= limit;
        }
        action = w->user->file_done ? w->user->file_done(w->user->user_data, path, count) : GREP_CONTINUE;
        if (action == GREP_STOP || cut) w->stop = true;
        if (cut) {
            if (w->limit_path) mem_free(w->limit_path);
            w->limit_path = NULL;
            action = GREP_STOP;
        }
    }
    pthread_mutex_unlock(&w->mu);
    return action;
}

// ── jobs ───────────────────────────────────────────────────────────────

// grep_search_paths' visitor: search one file with the slot's searcher
static GrepAction search_visit(void* ud, int slot, const char* open_path, const char* label,
                               size_t root_index) {
    GrepSearchRun* w = (GrepSearchRun*)ud;
    pthread_mutex_lock(&w->mu);
    bool stopped = w->stop;
    pthread_mutex_unlock(&w->mu);
    if (stopped) return GREP_STOP;
    // the slot is this visit's alone, so its searcher needs no lock
    GrepSearcher* s = w->searchers[slot];
    if (!s) s = w->searchers[slot] = grep_searcher_create(w->m);
    if (!s) return GREP_CONTINUE;
    // a total limit caps every file at that many: no file can contribute
    // more, and the merge keeps the first ones in path order (GRP25)
    uint64_t cap = w->w->max_matches_total;
    if (w->sorted) {
        GrepFileResult* r = (GrepFileResult*)mem_calloc(1, sizeof(GrepFileResult), MEM_CAT_TEMP);
        if (r && byte_builder_init(&r->bytes, 256, MEM_CAT_TEMP, false)) {
            r->path = mem_strdup(label, MEM_CAT_TEMP);
            r->root_index = root_index;
            GrepSink sink = {r, buffer_matched, w->user->context ? buffer_context : NULL, buffer_file_done};
            if (!r->path || grep_search_file_as(s, open_path, r->path, &sink, cap) != GREP_OK) r->failed = true;
            pthread_mutex_lock(&w->mu);
            arraylist_append(w->results, r);
            pthread_mutex_unlock(&w->mu);
        } else if (r) {
            mem_free(r);
        }
    } else {
        GrepSink sink = {w, direct_matched, w->user->context ? direct_context : NULL, direct_file_done};
        grep_search_file_as(s, open_path, label, &sink, cap);
    }
    pthread_mutex_lock(&w->mu);
    stopped = w->stop;
    pthread_mutex_unlock(&w->mu);
    return stopped ? GREP_STOP : GREP_CONTINUE;
}

static void file_job(void* arg) {
    GrepFileJob* job = (GrepFileJob*)arg;
    GrepWalk* w = job->walk;
    int slot = walk_stopped(w) ? -1 : acquire_slot(w);
    if (slot >= 0) {
        GrepAction action = w->visitor->visit(w->visitor->user_data, slot, job->open_path, job->label,
                                              job->root_index);
        release_slot(w, slot);
        if (action == GREP_STOP) {
            pthread_mutex_lock(&w->mu);
            w->stop = true;
            pthread_mutex_unlock(&w->mu);
        }
    }
    if (job->label) mem_free(job->label);
    if (job->open_path) mem_free(job->open_path);
    mem_free(job);
}

static void submit_file(GrepWalk* w, char* open_path, char* label, size_t root_index) {
    GrepFileJob* job = (GrepFileJob*)mem_calloc(1, sizeof(GrepFileJob), MEM_CAT_TEMP);
    if (!job || !open_path || !label) {
        if (job) mem_free(job);
        if (open_path) mem_free(open_path);
        if (label) mem_free(label);
        return;
    }
    job->walk = w;
    job->open_path = open_path;
    job->label = label;
    job->root_index = root_index;
    walk_submit(w, file_job, job);
}

static bool builtin_skip(const char* name) {
    for (size_t i = 0; i < sizeof(GREP_BUILTIN_SKIP_DIRS) / sizeof(GREP_BUILTIN_SKIP_DIRS[0]); i++) {
        if (strcmp(name, GREP_BUILTIN_SKIP_DIRS[i]) == 0) return true;
    }
    return false;
}

static bool any_rule(const GrepIgnoreRule* rules, size_t count, const char* rel, bool is_dir) {
    for (size_t i = 0; i < count; i++) {
        if (grep_rule_match(&rules[i], rel, is_dir)) return true;
    }
    return false;
}

static void dir_job(void* arg);

static void submit_dir(GrepWalk* w, char* full, char* rel, int depth, size_t root_index, GrepIgnoreNode* node) {
    GrepDirJob* job = (GrepDirJob*)mem_calloc(1, sizeof(GrepDirJob), MEM_CAT_TEMP);
    if (!job || !full || !rel) {
        if (job) mem_free(job);
        if (full) mem_free(full);
        if (rel) mem_free(rel);
        return;
    }
    job->walk = w;
    job->full = full;
    job->rel = rel;
    job->depth = depth;
    job->root_index = root_index;
    job->node = node;
    walk_submit(w, dir_job, job);
}

static void dir_job(void* arg) {
    GrepDirJob* job = (GrepDirJob*)arg;
    GrepWalk* w = job->walk;
    const GrepWalkOptions* o = w->w;
    ArrayList* entries = NULL;
    GrepIgnoreNode* node = job->node;
    if (walk_stopped(w)) goto done;
    if (!o->no_ignore) {
        GrepIgnoreNode* own = grep_ignore_load(job->full, job->rel, NULL, node);
        if (own) {
            pthread_mutex_lock(&w->mu);
            arraylist_append(w->nodes, own);
            pthread_mutex_unlock(&w->mu);
            node = own;
        }
    }
    entries = dir_list(job->full);
    if (!entries) {
        log_error("grep walk: cannot list '%s'", job->full);
        goto done;
    }
    for (int i = 0; i < arraylist_length(entries); i++) {
        DirEntry* e = (DirEntry*)arraylist_get(entries, i);
        const char* name = e->name;
        // symlinks found by the walk are not followed (GRP19)
        if (!name || e->is_symlink) continue;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (!o->hidden && name[0] == '.') continue;
        if (e->is_dir && !o->no_ignore && builtin_skip(name)) continue;
        int depth = job->depth + 1;
        // entries at depth d are searched when d <= max_depth
        if (o->max_depth >= 0 && (e->is_dir ? depth >= o->max_depth : depth > o->max_depth)) continue;
        char* rel = join_path(job->rel, name);
        if (!rel) continue;
        bool skip = any_rule(w->excludes, w->exclude_count, rel, e->is_dir);
        if (!skip && !e->is_dir && w->include_count && !any_rule(w->includes, w->include_count, rel, false)) skip = true;
        if (!skip && !o->no_ignore && grep_ignore_decide(node, rel, e->is_dir) == 1) skip = true;
        char* full = skip ? NULL : join_path(job->full, name);
        if (!skip && full && !e->is_dir && o->max_file_size) {
            FileStat st = file_stat(full);
            if (st.size >= 0 && (uint64_t)st.size > o->max_file_size) skip = true;
        }
        if (skip || !full) {
            mem_free(rel);
            if (full) mem_free(full);
            continue;
        }
        if (e->is_dir) {
            submit_dir(w, full, rel, depth, job->root_index, node);
        } else {
            mem_free(rel);
            submit_file(w, full, mem_strdup(full, MEM_CAT_TEMP), job->root_index);
        }
    }
done:
    if (entries) {
        for (int i = 0; i < arraylist_length(entries); i++) dir_entry_free((DirEntry*)arraylist_get(entries, i));
        arraylist_free(entries);
    }
    mem_free(job->full);
    mem_free(job->rel);
    mem_free(job);
}

// Ignore files of the repository enclosing a root directory apply to it too:
// walk up from the root to the first directory holding .git and chain their
// rules, outermost first. Outside a repository nothing above the root counts.
// the directory abs[0, k) where abs[k] is a '/', keeping a root's own slash
static char* ancestor_dir(const char* abs, size_t k) {
    if (k == 0) return mem_strdup("/", MEM_CAT_TEMP);
    if (abs[k - 1] == ':') return mem_dup_n(abs, k + 1, MEM_CAT_TEMP);  // "C:/"
    return mem_dup_n(abs, k, MEM_CAT_TEMP);
}

static bool dir_has_git(const char* dir) {
    char* probe = join_path(dir, ".git");
    bool has = probe && file_exists(probe);
    if (probe) mem_free(probe);
    return has;
}

static GrepIgnoreNode* ancestor_nodes(GrepWalk* w, const char* root) {
    char* abs = file_realpath(root);
    if (!abs) return NULL;
    for (char* p = abs; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    size_t n = strlen(abs);
    while (n > 1 && abs[n - 1] == '/') abs[--n] = '\0';
    GrepIgnoreNode* chain = NULL;
    if (!dir_has_git(abs)) {
        // the repository root: the nearest ancestor holding .git
        size_t top = 0;
        bool found = false;
        for (size_t k = n; k-- > 0 && !found;) {
            if (abs[k] != '/') continue;
            char* dir = ancestor_dir(abs, k);
            if (dir && dir_has_git(dir)) {
                top = k;
                found = true;
            }
            if (dir) mem_free(dir);
        }
        // chain from the repository root down to the root's parent; each
        // node's paths are relative to it, so it carries the way down
        for (size_t k = top; found && k < n; k++) {
            if (abs[k] != '/') continue;
            char* dir = ancestor_dir(abs, k);
            GrepIgnoreNode* node = dir ? grep_ignore_load(dir, NULL, abs + k + 1, chain) : NULL;
            if (dir) mem_free(dir);
            if (node) {
                pthread_mutex_lock(&w->mu);
                arraylist_append(w->nodes, node);
                pthread_mutex_unlock(&w->mu);
                chain = node;
            }
        }
    }
    mem_free(abs);
    return chain;
}

static bool parse_globs(const char* const* globs, size_t count, GrepIgnoreRule** out) {
    *out = NULL;
    if (!count) return true;
    GrepIgnoreRule* rules = (GrepIgnoreRule*)mem_calloc(count, sizeof(GrepIgnoreRule), MEM_CAT_TEMP);
    if (!rules) return false;
    for (size_t i = 0; i < count; i++) {
        if (!globs[i] || !grep_rule_parse(globs[i], strlen(globs[i]), false, &rules[i])) {
            // an empty glob matches nothing
            rules[i].pat = "";
            rules[i].len = 0;
            rules[i].anchored = true;
        }
    }
    *out = rules;
    return true;
}

GrepStatus grep_walk_paths(const char* const* paths, size_t count, const GrepWalkOptions* walk_options,
                           const GrepWalkVisitor* visitor) {
    if (!paths || !visitor || !visitor->visit) return GREP_ERR_ARGUMENT;
    GrepWalkOptions defaults;
    memset(&defaults, 0, sizeof(defaults));
    defaults.max_depth = -1;
    const GrepWalkOptions* o = walk_options ? walk_options : &defaults;

    // every root must exist before anything is searched
    bool any_dir = false;
    for (size_t i = 0; i < count; i++) {
        FileStat st = file_stat(paths[i]);
        if (!paths[i] || !st.exists || (!st.is_dir && !st.is_file)) {
            log_error("grep: no such file or directory: '%s'", paths[i] ? paths[i] : "(null)");
            return GREP_ERR_IO;
        }
        any_dir = any_dir || st.is_dir;
    }

    GrepWalk w;
    memset(&w, 0, sizeof(w));
    w.w = o;
    w.visitor = visitor;
    pthread_mutex_init(&w.mu, NULL);
    w.free_slots = arraylist_new(16);
    w.nodes = arraylist_new(16);
    GrepStatus status = GREP_OK;
    if (!w.free_slots || !w.nodes ||
        !parse_globs(o->include_globs, o->include_count, &w.includes) ||
        !parse_globs(o->exclude_globs, o->exclude_count, &w.excludes)) {
        status = GREP_ERR_MEMORY;
    }
    w.include_count = w.includes ? o->include_count : 0;
    w.exclude_count = w.excludes ? o->exclude_count : 0;

    int threads = o->threads > 0 ? o->threads : tp_hardware_threads();
    if (o->threads <= 0 && threads > GREP_DEFAULT_MAX_THREADS) threads = GREP_DEFAULT_MAX_THREADS;
    if (status == GREP_OK && threads > 1 && (any_dir || count > 1)) w.tp = tp_create(threads);
    // a job runs inline on the submitting thread when tp_submit fails, so the
    // calling thread may hold a slot beside every worker
    int slots = w.tp ? threads + 1 : 1;
    for (int i = slots - 1; status == GREP_OK && i >= 0; i--) {
        if (!arraylist_append(w.free_slots, (void*)(uintptr_t)i)) status = GREP_ERR_MEMORY;
    }
    if (status == GREP_OK && visitor->begin && !visitor->begin(visitor->user_data, slots)) status = GREP_ERR_MEMORY;

    for (size_t i = 0; status == GREP_OK && i < count; i++) {
        // a root is searched whatever the ignore rules and filters say (GRP22)
        size_t len = strlen(paths[i]);
        while (len > 1 && paths[i][len - 1] == '/') len--;
        char* root = mem_dup_n(paths[i], len, MEM_CAT_TEMP);
        if (!root) {
            status = GREP_ERR_MEMORY;
            break;
        }
        FileStat st = file_stat(root);
        if (st.is_dir) {
            GrepIgnoreNode* above = o->no_ignore ? NULL : ancestor_nodes(&w, root);
            submit_dir(&w, root, mem_strdup("", MEM_CAT_TEMP), 0, i, above);
        } else {
            // a symlinked root is resolved and searched (GRP19)
            char* open_path = st.is_symlink ? file_realpath(root) : mem_strdup(root, MEM_CAT_TEMP);
            submit_file(&w, open_path, root, i);
        }
    }
    if (w.tp) {
        tp_wait_all(w.tp);
        tp_destroy(w.tp);
        w.tp = NULL;
    }

    for (int i = 0; w.nodes && i < arraylist_length(w.nodes); i++) grep_ignore_free((GrepIgnoreNode*)arraylist_get(w.nodes, i));
    if (w.free_slots) arraylist_free(w.free_slots);
    if (w.nodes) arraylist_free(w.nodes);
    if (w.includes) mem_free(w.includes);
    if (w.excludes) mem_free(w.excludes);
    pthread_mutex_destroy(&w.mu);
    return status;
}

static bool search_begin(void* ud, int slot_count) {
    GrepSearchRun* run = (GrepSearchRun*)ud;
    run->searchers = (GrepSearcher**)mem_calloc((size_t)slot_count, sizeof(GrepSearcher*), MEM_CAT_TEMP);
    run->slot_count = run->searchers ? slot_count : 0;
    return run->searchers != NULL;
}

GrepStatus grep_search_paths(const GrepMatcher* matcher, const char* const* paths, size_t count,
                             const GrepWalkOptions* walk_options, const GrepSink* sink) {
    if (!matcher || !paths || !sink) return GREP_ERR_ARGUMENT;
    GrepWalkOptions defaults;
    memset(&defaults, 0, sizeof(defaults));
    defaults.max_depth = -1;
    const GrepWalkOptions* o = walk_options ? walk_options : &defaults;

    GrepSearchRun run;
    memset(&run, 0, sizeof(run));
    run.m = matcher;
    run.w = o;
    run.user = sink;
    run.sorted = o->sorted;
    pthread_mutex_init(&run.mu, NULL);
    run.results = arraylist_new(64);
    GrepStatus status = run.results ? GREP_OK : GREP_ERR_MEMORY;
    GrepWalkVisitor visitor = {&run, search_begin, search_visit};
    if (status == GREP_OK) status = grep_walk_paths(paths, count, o, &visitor);
    if (status == GREP_OK && run.sorted) deliver_sorted(&run);

    for (int i = 0; run.results && i < arraylist_length(run.results); i++) free_result((GrepFileResult*)arraylist_get(run.results, i));
    for (int i = 0; i < run.slot_count; i++) {
        if (run.searchers[i]) grep_searcher_destroy(run.searchers[i]);
    }
    if (run.searchers) mem_free(run.searchers);
    if (run.results) arraylist_free(run.results);
    if (run.limit_path) mem_free(run.limit_path);
    pthread_mutex_destroy(&run.mu);
    return status;
}
