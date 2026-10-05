// io_file_search.hpp — what io.grep and io.text_search share: option reading,
// sources, the walk options of GRP14/GRP22, and result values naming files
// (vibe/Lambda_Lib_Grep.md §9B, vibe/Lambda_IO_Fulltext_Search.md FTX12).
#pragma once

#include "transpiler.hpp"
#include "sys_func_registry.h"
#include "lambda-error.h"
#include "../../lib/grep/grep.h"
#include "../../lib/arraylist.h"
#include "../../lib/strbuf.h"

// An error value for an io search function, logged; `fmt` is printf-style.
Item io_fs_error(LambdaErrorCode code, const char* fmt, ...);

// One options map being read. `fn` names the function in messages ("io.grep").
struct IoFsOptionMap {
    const char* fn;
    SysFunc id;
    TypeMap* shape;           // NULL when no options were given
    void* data;
};

// Opens `options` (null or a map). S17.8.1: a name the function does not
// define is ignored with a warning (a literal at the call was checked at
// compile time).
bool io_fs_options_open(Item options, const char* fn, SysFunc id, IoFsOptionMap* out, Item* error);
// Each reader leaves *out unchanged when the option is absent or null.
bool io_fs_option_bool(const IoFsOptionMap* m, const char* name, bool* out, Item* error);
bool io_fs_option_count(const IoFsOptionMap* m, const char* name, int64_t* out, Item* error);
// the option's value, or ItemNull when absent
Item io_fs_option_get(const IoFsOptionMap* m, const char* name);

// The options io.grep and io.text_search share (FTX12). Callers preset the
// defaults that differ (ignore_case, word, text); reading only overrides.
struct IoFsCommonOptions {
    GrepWalkOptions walk;     // hidden, ignore, include/exclude, max_depth, max_size
    bool ignore_case;
    bool word;
    bool want_line;
    bool want_byte_offset;
    bool want_text;
    bool want_line_ending;
    bool files_only;
    bool count;
    bool binary;
    int before;
    int after;
    uint64_t limit;           // 0: none
    uint64_t limit_per_file;  // 0: none
    ArrayList* include;       // mem-owned glob strings
    ArrayList* exclude;
};

void io_fs_common_init(IoFsCommonOptions* o);
bool io_fs_common_read(const IoFsOptionMap* m, IoFsCommonOptions* o, Item* error);
void io_fs_common_release(IoFsCommonOptions* o);

// A source as given: a path, a wildcard path, or a string (§9B.1).
struct IoFsSource {
    Item item;                // as given
    Path* path;               // NULL for a text source
    StrBuf* os;               // local path to search
    int max_depth;            // -1, or 1 for a trailing `*` wildcard
    bool is_dir;
};

bool io_fs_resolve_source(const char* fn, Item item, IoFsSource* src, Item* error);
// Resolves `source` (one source or an array); *out is mem-owned, free with
// io_fs_sources_free. A missing source raises E401.
bool io_fs_resolve_sources(const char* fn, Item source, IoFsSource** out, int* count, Item* error);
void io_fs_sources_free(IoFsSource* sources, int count);

// The source a reported path was found under.
struct IoFsRoot {
    Path* root_path;          // NULL when the source was given as text
    Item root_text;           // a text source as given (rooted by the caller)
    const char* root_os;
    size_t root_os_len;
};

void io_fs_root_of(const IoFsSource* src, IoFsRoot* root);
Item io_fs_make_string(const char* s, size_t len);
// the reported file as the caller named its source: a path below a path
// source, or the source text joined with the file's place below it
Item io_fs_file_value(const IoFsRoot* root, const char* reported);
// a line's terminator as text (GRP31); null when the input ended without one
Item io_fs_line_ending_item(GrepLineEnding e);
