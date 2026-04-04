// bash_builtins_ext.cpp — Extended Builtins (Phase F — Module 9)
//
// Implements builtins needed by GNU tests that are not in bash_builtins.cpp:
// mapfile, wait, hash, enable, builtin, umask, trap -p.

#include "bash_builtins_ext.h"
#include "bash_runtime.h"
#include "bash_errors.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

// ============================================================================
// mapfile / readarray
// ============================================================================

extern "C" Item bash_builtin_mapfile(Item* args, int argc) {
    // defaults
    char delim = '\n';
    int max_count = -1;     // -1 = unlimited
    int origin = 0;         // -O origin
    int skip_count = 0;     // -s count
    int strip_trailing = 0; // -t: strip delimiter
    int use_fd = -1;        // -u fd (default: stdin)
    const char* array_name = "MAPFILE";

    // parse options
    int i = 0;
    while (i < argc) {
        Item arg_item = bash_to_string(args[i]);
        String* arg = it2s(arg_item);
        if (!arg || arg->len == 0) { i++; continue; }

        if (arg->chars[0] != '-') {
            // positional: array name
            array_name = arg->chars;
            i++;
            continue;
        }

        if (arg->len == 2) {
            switch (arg->chars[1]) {
            case 'd':
                if (i + 1 < argc) {
                    Item d = bash_to_string(args[++i]);
                    String* ds = it2s(d);
                    if (ds && ds->len > 0) delim = ds->chars[0];
                }
                break;
            case 'n':
                if (i + 1 < argc) {
                    max_count = (int)bash_to_int_val(args[++i]);
                }
                break;
            case 'O':
                if (i + 1 < argc) {
                    origin = (int)bash_to_int_val(args[++i]);
                }
                break;
            case 's':
                if (i + 1 < argc) {
                    skip_count = (int)bash_to_int_val(args[++i]);
                }
                break;
            case 't':
                strip_trailing = 1;
                break;
            case 'u':
                if (i + 1 < argc) {
                    use_fd = (int)bash_to_int_val(args[++i]);
                }
                break;
            case 'C':
                // callback — skip (not implemented for now)
                if (i + 1 < argc) i++;
                break;
            case 'c':
                // quantum — skip (not implemented for now)
                if (i + 1 < argc) i++;
                break;
            }
        }
        i++;
    }

    // create or get the target array
    Item name_item = (Item){.item = s2it(heap_create_name(array_name, (int)strlen(array_name)))};
    Item arr = bash_ensure_array(name_item);

    // determine input source
    FILE* fp = NULL;
    Item stdin_item = ItemNull;
    int from_stdin_item = 0;

    if (use_fd >= 0) {
        fp = fdopen(dup(use_fd), "r");
        if (!fp) {
            bash_errmsg("mapfile: %d: invalid file descriptor", use_fd);
            return bash_from_exit_code(1);
        }
    } else {
        // check if there's piped stdin content
        stdin_item = bash_get_stdin_item();
        if (get_type_id(stdin_item) != LMD_TYPE_NULL) {
            from_stdin_item = 1;
        } else {
            fp = stdin;
        }
    }

    int line_idx = 0;
    int stored = 0;

    if (from_stdin_item) {
        // read from stdin item (pipeline content)
        String* content = it2s(bash_to_string(stdin_item));
        if (content && content->len > 0) {
            const char* p = content->chars;
            const char* end = p + content->len;

            while (p < end) {
                const char* line_start = p;
                while (p < end && *p != delim) p++;

                int line_len = (int)(p - line_start);
                int include_delim = (p < end) ? 1 : 0;
                if (p < end) p++;  // skip delimiter

                if (line_idx < skip_count) {
                    line_idx++;
                    continue;
                }

                if (max_count >= 0 && stored >= max_count) break;

                int store_len = line_len + (include_delim && !strip_trailing ? 1 : 0);
                Item val = (Item){.item = s2it(heap_create_name(line_start, store_len))};
                Item idx_item = bash_int_to_item(origin + stored);
                bash_array_set(arr, idx_item, val);
                stored++;
                line_idx++;
            }
        }
        bash_clear_stdin_item();
    } else if (fp) {
        // read from file descriptor
        char buf[8192];
        StrBuf* line_buf = strbuf_new_cap(256);

        while (1) {
            if (!fgets(buf, sizeof(buf), fp)) break;

            strbuf_append_str(line_buf, buf);

            // check if line ends with delimiter
            int len = (int)line_buf->length;
            if (len > 0 && line_buf->str[len - 1] == delim) {
                if (line_idx >= skip_count) {
                    if (max_count >= 0 && stored >= max_count) break;

                    int store_len = len;
                    if (strip_trailing && store_len > 0 &&
                        line_buf->str[store_len - 1] == delim) {
                        store_len--;
                    }

                    Item val = (Item){.item = s2it(heap_create_name(line_buf->str, store_len))};
                    Item idx_item = bash_int_to_item(origin + stored);
                    bash_array_set(arr, idx_item, val);
                    stored++;
                }
                line_idx++;
                strbuf_reset(line_buf);
            }
        }

        // handle last line without trailing delimiter
        if (line_buf->length > 0) {
            if (line_idx >= skip_count && (max_count < 0 || stored < max_count)) {
                int store_len = (int)line_buf->length;
                if (strip_trailing && store_len > 0 &&
                    line_buf->str[store_len - 1] == delim) {
                    store_len--;
                }
                Item val = (Item){.item = s2it(heap_create_name(line_buf->str, store_len))};
                Item idx_item = bash_int_to_item(origin + stored);
                bash_array_set(arr, idx_item, val);
                stored++;
            }
        }

        strbuf_free(line_buf);
        if (fp != stdin && use_fd >= 0) fclose(fp);
    }

    // store the array in the variable table
    bash_set_var(name_item, arr);

    log_debug("bash_builtin_mapfile: read %d lines into %s (origin=%d)", stored, array_name, origin);
    return bash_from_exit_code(0);
}

// ============================================================================
// wait
// ============================================================================

extern "C" Item bash_builtin_wait(Item* args, int argc) {
    // simple implementation: wait for a specific pid or all children
    if (argc == 0) {
        // wait for all child processes
        int status = 0;
        while (waitpid(-1, &status, 0) > 0) {
            // keep waiting
        }
        return bash_from_exit_code(0);
    }

    int last_status = 0;
    for (int i = 0; i < argc; i++) {
        int64_t pid = bash_to_int_val(args[i]);
        if (pid <= 0) continue;

        int status = 0;
        pid_t result = waitpid((pid_t)pid, &status, 0);
        if (result < 0) {
            // process not found or not a child
            bash_errmsg("wait: pid %lld is not a child of this shell", (long long)pid);
            last_status = 127;
        } else if (WIFEXITED(status)) {
            last_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            last_status = 128 + WTERMSIG(status);
        }
    }

    return bash_from_exit_code(last_status);
}

// ============================================================================
// hash
// ============================================================================

// simple hash table for command path caching
#define HASH_TABLE_SIZE 256

typedef struct HashEntry {
    char name[256];
    char path[1024];
    int hits;
    int active;
} HashEntry;

static HashEntry hash_table[HASH_TABLE_SIZE];
static int hash_count = 0;

static void hash_clear(void) {
    memset(hash_table, 0, sizeof(hash_table));
    hash_count = 0;
}

static HashEntry* hash_find(const char* name) {
    for (int i = 0; i < hash_count; i++) {
        if (hash_table[i].active && strcmp(hash_table[i].name, name) == 0) {
            return &hash_table[i];
        }
    }
    return NULL;
}

static void hash_add(const char* name, const char* path) {
    HashEntry* entry = hash_find(name);
    if (!entry) {
        if (hash_count >= HASH_TABLE_SIZE) return;
        entry = &hash_table[hash_count++];
    }
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->hits = 0;
    entry->active = 1;
}

extern "C" Item bash_builtin_hash(Item* args, int argc) {
    int opt_r = 0;      // -r: forget all
    int opt_d = 0;      // -d: forget named
    int opt_t = 0;      // -t: print path only
    const char* opt_p = NULL;  // -p path

    int i = 0;
    while (i < argc) {
        Item arg_item = bash_to_string(args[i]);
        String* arg = it2s(arg_item);
        if (!arg) { i++; continue; }

        if (arg->len >= 2 && arg->chars[0] == '-') {
            for (int j = 1; j < arg->len; j++) {
                switch (arg->chars[j]) {
                case 'r': opt_r = 1; break;
                case 'd': opt_d = 1; break;
                case 't': opt_t = 1; break;
                case 'p':
                    if (i + 1 < argc) {
                        Item p = bash_to_string(args[++i]);
                        String* ps = it2s(p);
                        if (ps) opt_p = ps->chars;
                    }
                    j = arg->len; // stop inner loop
                    break;
                }
            }
            i++;
            continue;
        }

        // name argument
        if (opt_r) {
            hash_clear();
            i++;
            continue;
        }

        if (opt_d) {
            HashEntry* entry = hash_find(arg->chars);
            if (entry) entry->active = 0;
            i++;
            continue;
        }

        if (opt_p) {
            hash_add(arg->chars, opt_p);
            i++;
            continue;
        }

        // lookup and print
        HashEntry* entry = hash_find(arg->chars);
        if (entry) {
            if (opt_t) {
                bash_raw_write(entry->path, (int)strlen(entry->path));
                bash_raw_putc('\n');
            } else {
                char buf[1536];
                int len = snprintf(buf, sizeof(buf), "hits\tcommand\n   %d\t%s\n",
                                   entry->hits, entry->path);
                bash_raw_write(buf, len);
            }
        } else {
            bash_errmsg("hash: %s: not found", arg->chars);
            return bash_from_exit_code(1);
        }
        i++;
    }

    if (opt_r && i == 0) {
        // hash -r with no names: clear all
        hash_clear();
    }

    if (argc == 0) {
        // print all hashed commands
        int any = 0;
        for (int j = 0; j < hash_count; j++) {
            if (!hash_table[j].active) continue;
            char buf[1536];
            int len = snprintf(buf, sizeof(buf), "hits\tcommand\n   %d\t%s\n",
                               hash_table[j].hits, hash_table[j].path);
            bash_raw_write(buf, len);
            any = 1;
        }
        if (!any) {
            bash_errmsg("hash: hash table empty");
        }
    }

    return bash_from_exit_code(0);
}

// ============================================================================
// enable
// ============================================================================

extern "C" Item bash_builtin_enable(Item* args, int argc) {
    // basic implementation: report builtins as enabled
    // -a: list all (enabled and disabled)
    // -n: disable

    int opt_a = 0;
    int opt_n = 0;

    int i = 0;
    while (i < argc) {
        Item arg_item = bash_to_string(args[i]);
        String* arg = it2s(arg_item);
        if (!arg) { i++; continue; }

        if (arg->len >= 2 && arg->chars[0] == '-') {
            for (int j = 1; j < arg->len; j++) {
                switch (arg->chars[j]) {
                case 'a': opt_a = 1; break;
                case 'n': opt_n = 1; break;
                }
            }
            i++;
            continue;
        }

        // named builtins — just acknowledge
        if (opt_n) {
            log_debug("bash_builtin_enable: disable '%s' (no-op)", arg->chars);
        }
        i++;
    }

    if (argc == 0 || opt_a) {
        // list all builtins as enabled
        static const char* all_builtins[] = {
            ".", ":", "[", "alias", "bg", "bind", "break", "builtin",
            "caller", "cd", "command", "compgen", "complete", "compopt",
            "continue", "declare", "dirs", "disown", "echo", "enable",
            "eval", "exec", "exit", "export", "false", "fc", "fg",
            "getopts", "hash", "help", "history", "jobs", "kill", "let",
            "local", "logout", "mapfile", "popd", "printf", "pushd",
            "pwd", "read", "readarray", "readonly", "return", "set",
            "shift", "shopt", "source", "suspend", "test", "times",
            "trap", "true", "type", "typeset", "ulimit", "umask",
            "unalias", "unset", "wait",
            NULL
        };
        for (int j = 0; all_builtins[j]; j++) {
            const char* b = all_builtins[j];
            bash_raw_write("enable ", 7);
            bash_raw_write(b, (int)strlen(b));
            bash_raw_putc('\n');
        }
    }

    return bash_from_exit_code(0);
}

// ============================================================================
// builtin
// ============================================================================

extern "C" Item bash_builtin_builtin(Item* args, int argc) {
    if (argc == 0) {
        return bash_from_exit_code(0);
    }

    // dispatch to the named builtin, bypassing function lookup
    // this calls back through the transpiler's exec path but with
    // the function-lookup step skipped
    Item name = bash_to_string(args[0]);
    Item result = bash_call_rt_func(name, args + 1, argc - 1);

    // if not found as a runtime function, try calling as external
    if (get_type_id(result) == LMD_TYPE_NULL) {
        bash_errmsg("builtin: %s: not a shell builtin", it2s(name) ? it2s(name)->chars : "");
        return bash_from_exit_code(1);
    }

    return result;
}

// ============================================================================
// umask
// ============================================================================

extern "C" Item bash_builtin_umask(Item* args, int argc) {
    int opt_p = 0;
    int opt_S = 0;

    int i = 0;
    while (i < argc) {
        Item arg_item = bash_to_string(args[i]);
        String* arg = it2s(arg_item);
        if (!arg) { i++; continue; }

        if (arg->len >= 2 && arg->chars[0] == '-') {
            for (int j = 1; j < arg->len; j++) {
                switch (arg->chars[j]) {
                case 'p': opt_p = 1; break;
                case 'S': opt_S = 1; break;
                }
            }
            i++;
            continue;
        }

        // set umask from octal string
        unsigned int mask = 0;
        for (int j = 0; j < arg->len; j++) {
            if (arg->chars[j] >= '0' && arg->chars[j] <= '7') {
                mask = (mask << 3) | (unsigned int)(arg->chars[j] - '0');
            } else {
                bash_errmsg("umask: %s: invalid octal number", arg->chars);
                return bash_from_exit_code(1);
            }
        }
        umask((mode_t)mask);
        return bash_from_exit_code(0);
    }

    // display current umask
    mode_t current = umask(0);
    umask(current);  // restore

    if (opt_S) {
        // symbolic format: u=rwx,g=rx,o=rx
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "u=%s%s%s,g=%s%s%s,o=%s%s%s",
                           (current & S_IRUSR) ? "" : "r",
                           (current & S_IWUSR) ? "" : "w",
                           (current & S_IXUSR) ? "" : "x",
                           (current & S_IRGRP) ? "" : "r",
                           (current & S_IWGRP) ? "" : "w",
                           (current & S_IXGRP) ? "" : "x",
                           (current & S_IROTH) ? "" : "r",
                           (current & S_IWOTH) ? "" : "w",
                           (current & S_IXOTH) ? "" : "x");
        bash_raw_write(buf, len);
        bash_raw_putc('\n');
    } else {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%04o", (unsigned int)current);
        if (opt_p) {
            bash_raw_write("umask ", 6);
        }
        bash_raw_write(buf, len);
        bash_raw_putc('\n');
    }

    return bash_from_exit_code(0);
}

// ============================================================================
// trap -p (print)
// ============================================================================

// trap handler table is defined in bash_runtime.cpp — we access it via these indices
// the trap_handlers array is static there, so we need a helper function exposed
// from bash_runtime.cpp, or we define the print function there.
// For now, implement here by querying trap state through bash_eval_string.

// signal names mapping (index → name)
static const char* trap_idx_to_name[] = {
    "EXIT", "ERR", "DEBUG", "HUP", "INT", "QUIT", "TERM", "RETURN"
};
#define TRAP_COUNT 8

// single-quote escape: replace ' with '\'' inside the action string
static void trap_print_escaped(const char* action) {
    bash_raw_write("trap -- '", 9);

    const char* p = action;
    while (*p) {
        if (*p == '\'') {
            bash_raw_write("'\\''", 4);
        } else {
            bash_raw_putc(*p);
        }
        p++;
    }

    bash_raw_write("' ", 2);
}

extern "C" void bash_trap_print_all(void) {
    // we need access to the trap handlers array from bash_runtime.cpp
    // since it's static, we call bash_trap_get_handler() if available
    // For now, use the extern access pattern
    extern char* bash_trap_handlers[];

    for (int i = 0; i < TRAP_COUNT; i++) {
        if (bash_trap_handlers[i]) {
            trap_print_escaped(bash_trap_handlers[i]);
            bash_raw_write(trap_idx_to_name[i], (int)strlen(trap_idx_to_name[i]));
            bash_raw_putc('\n');
        }
    }
}

extern "C" void bash_trap_print_one(Item signal_name) {
    extern char* bash_trap_handlers[];

    String* sig = it2s(bash_to_string(signal_name));
    if (!sig) return;

    // find index
    for (int i = 0; i < TRAP_COUNT; i++) {
        if ((int)strlen(trap_idx_to_name[i]) == sig->len &&
            memcmp(trap_idx_to_name[i], sig->chars, sig->len) == 0) {
            if (bash_trap_handlers[i]) {
                trap_print_escaped(bash_trap_handlers[i]);
                bash_raw_write(trap_idx_to_name[i], (int)strlen(trap_idx_to_name[i]));
                bash_raw_putc('\n');
            }
            return;
        }
    }
}
