/**
 * Lambda Unified Font Module — Font Database
 *
 * System font discovery, scanning, TTF/OTF metadata parsing, and matching.
 * Adapted from lib/font_config.c — removes global singleton, uses FontContext
 * ownership, and supports WOFF/WOFF2 file extensions during scanning.
 *
 * Scanning uses a 3-phase approach:
 *   Phase 1: Fast directory walk — create placeholders (no parsing)
 *   Phase 2: Parse priority fonts (web-safe: Arial, Times, Courier, etc.)
 *   Phase 3: Organize into families (hashmaps for fast lookup)
 * Remaining fonts are parsed lazily on first access.
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include "../str.h"
#include "../strbuf.h"
#include "../file.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#include <strings.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

// ============================================================================
// Constants
// ============================================================================

#define FONT_CACHE_MAGIC        0x4C464E54  // 'LFNT'
#define FONT_CACHE_VERSION      1
#define MAX_FONT_FAMILY_NAME    256
#define MAX_FONT_FILE_PATH      1024
#define MAX_TTC_FONTS           4
#define FONT_MATCH_SCORE_THRESHOLD 0.1f

// TTF/OTF table tags (big-endian values after conversion)
#define TTF_TAG_NAME 0x6E616D65  // 'name'
#define TTF_TAG_CMAP 0x636D6170  // 'cmap'
#define TTF_TAG_OS2  0x4F532F32  // 'OS/2'
#define TTF_TAG_HEAD 0x68656164  // 'head'

// Name ID constants
#define NAME_ID_FAMILY_NAME      1
#define NAME_ID_SUBFAMILY_NAME   2
#define NAME_ID_POSTSCRIPT_NAME  6

// OS/2 table constants
#define OS2_WEIGHT_CLASS_OFFSET  4
#define OS2_SELECTION_OFFSET     62
#define OS2_SELECTION_ITALIC     0x0001

// ============================================================================
// Internal TTF Structures
// ============================================================================

typedef struct TTF_Header {
    uint32_t scaler_type;
    uint16_t num_tables;
    uint16_t search_range;
    uint16_t entry_selector;
    uint16_t range_shift;
} TTF_Header;

typedef struct TTF_Table_Dir {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} TTF_Table_Dir;

typedef struct TTC_Header {
    uint32_t signature;
    uint32_t version;
    uint32_t num_fonts;
} TTC_Header;

// ============================================================================
// Priority font families (web-safe fonts parsed first for fast startup)
// ============================================================================

static const char* priority_font_families[] = {
    "Arial", "Helvetica", "Times", "Times New Roman",
    "Courier", "Courier New", "Verdana", "Georgia",
    "Trebuchet MS", "Comic Sans MS", "Impact",
    "Helvetica Neue", "Monaco", "Menlo",
    "San Francisco", "SF Pro Display", "SF Pro Text",
    "DejaVu Sans", "DejaVu Serif", "Liberation Sans", "Liberation Serif",
    NULL
};

// ============================================================================
// Generic font family mappings (CSS generic → concrete preferences)
// ============================================================================

static const struct {
    const char* generic;
    const char* preferred[8];
} generic_families[] = {
    {"serif",      {"Times New Roman", "Times", "Georgia", "DejaVu Serif", NULL}},
    {"sans-serif", {"Arial", "Helvetica", "DejaVu Sans", "Liberation Sans", NULL}},
    {"monospace",  {"Courier New", "Courier", "Monaco", "DejaVu Sans Mono", NULL}},
    {"cursive",    {"Comic Sans MS", "Apple Chancery", "Bradley Hand", NULL}},
    {"fantasy",    {"Impact", "Papyrus", "Herculanum", NULL}},
    {NULL, {NULL}}
};

// ============================================================================
// Byte-order helpers
// ============================================================================

static uint32_t be32toh_local(uint32_t v) {
    return ((v & 0xFF000000) >> 24) | ((v & 0x00FF0000) >> 8) |
           ((v & 0x0000FF00) << 8)  | ((v & 0x000000FF) << 24);
}

static uint16_t be16toh_local(uint16_t v) {
    return (uint16_t)(((v & 0xFF00) >> 8) | ((v & 0x00FF) << 8));
}

// ============================================================================
// Hashmap callbacks
// ============================================================================

static uint64_t family_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const FontFamily* f = (const FontFamily*)item;
    if (!f || !f->family_name) return 0;
    size_t len = strlen(f->family_name);
    char lower[256];
    size_t n = len < sizeof(lower) - 1 ? len : sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)f->family_name[i]);
    lower[n] = '\0';
    return hashmap_xxhash3(lower, n, seed0, seed1);
}

static int family_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const FontFamily* fa = (const FontFamily*)a;
    const FontFamily* fb = (const FontFamily*)b;
    if (!fa || !fb || !fa->family_name || !fb->family_name) return -1;
    return str_icmp(fa->family_name, strlen(fa->family_name),
                    fb->family_name, strlen(fb->family_name));
}

static uint64_t file_path_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const FontEntry* e = (const FontEntry*)item;
    if (!e || !e->file_path) return 0;
    return hashmap_xxhash3(e->file_path, strlen(e->file_path), seed0, seed1);
}

static int file_path_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const FontEntry* ea = (const FontEntry*)a;
    const FontEntry* eb = (const FontEntry*)b;
    if (!ea || !eb || !ea->file_path || !eb->file_path) return -1;
    return strcmp(ea->file_path, eb->file_path);
}

static uint64_t ps_name_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const FontEntry* e = (const FontEntry*)item;
    if (!e || !e->postscript_name) return 0;
    return hashmap_xxhash3(e->postscript_name, strlen(e->postscript_name), seed0, seed1);
}

static int ps_name_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const FontEntry* ea = (const FontEntry*)a;
    const FontEntry* eb = (const FontEntry*)b;
    if (!ea || !eb || !ea->postscript_name || !eb->postscript_name) return -1;
    return strcmp(ea->postscript_name, eb->postscript_name);
}

// ============================================================================
// Database lifecycle
// ============================================================================

FontDatabase* font_database_create_internal(Pool* pool, Arena* arena) {
    FontDatabase* db = (FontDatabase*)pool_calloc(pool, sizeof(FontDatabase));
    if (!db) return NULL;

    db->pool = pool;
    db->arena = arena;
    db->scanned = false;

    // create hashmaps
    db->families        = hashmap_new(sizeof(FontFamily), 64, 0, 0, family_hash, family_compare, NULL, NULL);
    db->postscript_names = hashmap_new(sizeof(FontEntry), 64, 0, 0, ps_name_hash, ps_name_compare, NULL, NULL);
    db->file_paths      = hashmap_new(sizeof(FontEntry), 256, 0, 0, file_path_hash, file_path_compare, NULL, NULL);

    // create arraylists
    db->all_fonts        = arraylist_new(0);
    db->font_files       = arraylist_new(0);
    db->scan_directories = arraylist_new(0);

    log_info("font_database_create_internal: created");
    return db;
}

void font_database_destroy_internal(FontDatabase* db) {
    if (!db) return;

    if (db->families)         hashmap_free(db->families);
    if (db->postscript_names) hashmap_free(db->postscript_names);
    if (db->file_paths)       hashmap_free(db->file_paths);
    if (db->all_fonts)        arraylist_free(db->all_fonts);
    if (db->font_files)       arraylist_free(db->font_files);
    if (db->scan_directories) arraylist_free(db->scan_directories);

    // FontEntry and string data live in arena, freed on arena_destroy
    log_info("font_database_destroy_internal: destroyed");
}

// ============================================================================
// TTF/OTF Parsing (adapted from font_config.c)
// ============================================================================

static bool read_ttf_table_directory(FILE* file, TTF_Header* header, TTF_Table_Dir** tables) {
    *tables = (TTF_Table_Dir*)calloc(header->num_tables, sizeof(TTF_Table_Dir));
    if (!*tables) return false;

    for (int i = 0; i < header->num_tables; i++) {
        if (fread(&(*tables)[i], sizeof(TTF_Table_Dir), 1, file) != 1) {
            free(*tables);
            *tables = NULL;
            return false;
        }
        (*tables)[i].tag    = be32toh_local((*tables)[i].tag);
        (*tables)[i].offset = be32toh_local((*tables)[i].offset);
        (*tables)[i].length = be32toh_local((*tables)[i].length);
    }
    return true;
}

static TTF_Table_Dir* find_ttf_table(TTF_Table_Dir* tables, int count, uint32_t tag) {
    for (int i = 0; i < count; i++) {
        if (tables[i].tag == tag) return &tables[i];
    }
    return NULL;
}

static bool parse_name_table(FILE* file, TTF_Table_Dir* name_table, FontEntry* entry, Arena* arena) {
    if (fseek(file, name_table->offset, SEEK_SET) != 0) return false;

    uint16_t format, count, string_offset;
    if (fread(&format, 2, 1, file) != 1) return false;
    if (fread(&count, 2, 1, file) != 1) return false;
    if (fread(&string_offset, 2, 1, file) != 1) return false;

    format = be16toh_local(format);
    count  = be16toh_local(count);
    string_offset = be16toh_local(string_offset);

    bool found_family = false;
    bool found_subfamily = false;
    bool found_postscript = false;

    for (int i = 0; i < count && !(found_family && found_subfamily && found_postscript); i++) {
        uint16_t platform_id, encoding_id, language_id, name_id, length, offset;
        if (fread(&platform_id, 2, 1, file) != 1) break;
        if (fread(&encoding_id, 2, 1, file) != 1) break;
        if (fread(&language_id, 2, 1, file) != 1) break;
        if (fread(&name_id, 2, 1, file) != 1) break;
        if (fread(&length, 2, 1, file) != 1) break;
        if (fread(&offset, 2, 1, file) != 1) break;

        platform_id = be16toh_local(platform_id);
        encoding_id = be16toh_local(encoding_id);
        language_id = be16toh_local(language_id);
        name_id     = be16toh_local(name_id);
        length      = be16toh_local(length);
        offset      = be16toh_local(offset);

        // only process family, subfamily, and postscript names
        if (name_id != NAME_ID_FAMILY_NAME &&
            name_id != NAME_ID_SUBFAMILY_NAME &&
            name_id != NAME_ID_POSTSCRIPT_NAME) continue;

        // skip if already found this name
        if (name_id == NAME_ID_FAMILY_NAME && found_family) continue;
        if (name_id == NAME_ID_SUBFAMILY_NAME && found_subfamily) continue;
        if (name_id == NAME_ID_POSTSCRIPT_NAME && found_postscript) continue;

        // Platform 1 (Mac) or Platform 3 (Windows)
        if (platform_id != 1 && platform_id != 3) continue;

        // save position, read string, restore
        long saved_pos = ftell(file);
        if (fseek(file, name_table->offset + string_offset + offset, SEEK_SET) != 0) {
            fseek(file, saved_pos, SEEK_SET);
            continue;
        }

        char name_buf[MAX_FONT_FAMILY_NAME];
        memset(name_buf, 0, sizeof(name_buf));

        if (platform_id == 3) {
            // UTF-16 BE
            int chars_to_read = length / 2;
            if (chars_to_read >= MAX_FONT_FAMILY_NAME) chars_to_read = MAX_FONT_FAMILY_NAME - 1;
            int out_idx = 0;
            for (int c = 0; c < chars_to_read; c++) {
                uint8_t hi, lo;
                if (fread(&hi, 1, 1, file) != 1 || fread(&lo, 1, 1, file) != 1) break;
                if (hi == 0 && lo >= 0x20 && lo < 0x7F) {
                    name_buf[out_idx++] = (char)lo;
                }
            }
            name_buf[out_idx] = '\0';
        } else {
            // MacRoman
            int to_read = length < MAX_FONT_FAMILY_NAME - 1 ? length : MAX_FONT_FAMILY_NAME - 1;
            if ((int)fread(name_buf, 1, to_read, file) != to_read) {
                fseek(file, saved_pos, SEEK_SET);
                continue;
            }
            name_buf[to_read] = '\0';
        }

        fseek(file, saved_pos, SEEK_SET);

        if (name_buf[0] == '\0') continue;

        // assign to appropriate field
        if (name_id == NAME_ID_FAMILY_NAME && !found_family) {
            entry->family_name = arena_strdup(arena, name_buf);
            found_family = true;
        } else if (name_id == NAME_ID_SUBFAMILY_NAME && !found_subfamily) {
            entry->subfamily_name = arena_strdup(arena, name_buf);
            found_subfamily = true;
        } else if (name_id == NAME_ID_POSTSCRIPT_NAME && !found_postscript) {
            entry->postscript_name = arena_strdup(arena, name_buf);
            found_postscript = true;
        }
    }

    return found_family;
}

static bool parse_os2_table(FILE* file, TTF_Table_Dir* os2_table, FontEntry* entry) {
    if (fseek(file, os2_table->offset, SEEK_SET) != 0) return false;

    // read version + usWeightClass (at offset 4 from table start)
    uint16_t version;
    if (fread(&version, 2, 1, file) != 1) return false;

    // skip xAvgCharWidth
    fseek(file, 2, SEEK_CUR);

    uint16_t weight_class;
    if (fread(&weight_class, 2, 1, file) != 1) return false;
    entry->weight = be16toh_local(weight_class);

    // read fsSelection for italic bit
    if (os2_table->length >= OS2_SELECTION_OFFSET + 2) {
        if (fseek(file, os2_table->offset + OS2_SELECTION_OFFSET, SEEK_SET) == 0) {
            uint16_t fs_selection;
            if (fread(&fs_selection, 2, 1, file) == 1) {
                fs_selection = be16toh_local(fs_selection);
                if (fs_selection & OS2_SELECTION_ITALIC) {
                    entry->style = FONT_SLANT_ITALIC;
                }
            }
        }
    }

    return true;
}

static bool parse_font_metadata(const char* file_path, FontEntry* entry, Arena* arena) {
    FILE* file = fopen(file_path, "rb");
    if (!file) return false;

    // read TTF header
    TTF_Header header;
    if (fread(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        return false;
    }

    header.scaler_type   = be32toh_local(header.scaler_type);
    header.num_tables    = be16toh_local(header.num_tables);

    // validate — reject TTC (handled separately)
    if (header.scaler_type == 0x74746366) { // 'ttcf'
        fclose(file);
        return false;
    }

    // read table directory
    TTF_Table_Dir* tables = NULL;
    if (!read_ttf_table_directory(file, &header, &tables)) {
        fclose(file);
        return false;
    }

    bool success = false;

    // parse name table
    TTF_Table_Dir* name_tbl = find_ttf_table(tables, header.num_tables, TTF_TAG_NAME);
    if (name_tbl) {
        success = parse_name_table(file, name_tbl, entry, arena);
    }

    // parse OS/2 table
    TTF_Table_Dir* os2_tbl = find_ttf_table(tables, header.num_tables, TTF_TAG_OS2);
    if (os2_tbl) {
        parse_os2_table(file, os2_tbl, entry);
    }

    free(tables);
    fclose(file);

    // fallback: use filename as family name
    if (!entry->family_name && file_path) {
        const char* base = strrchr(file_path, '/');
        if (!base) base = strrchr(file_path, '\\');
        if (base) base++; else base = file_path;

        char buf[MAX_FONT_FAMILY_NAME];
        strncpy(buf, base, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        // strip extension
        char* dot = strrchr(buf, '.');
        if (dot) *dot = '\0';

        entry->family_name = arena_strdup(arena, buf);
        success = true;
    }

    return success;
}

static bool parse_ttc_font_metadata(const char* file_path, FontDatabase* db, Arena* arena) {
    FILE* file = fopen(file_path, "rb");
    if (!file) return false;

    TTC_Header ttc;
    if (fread(&ttc, sizeof(ttc), 1, file) != 1) {
        fclose(file);
        return false;
    }

    ttc.signature = be32toh_local(ttc.signature);
    ttc.version   = be32toh_local(ttc.version);
    ttc.num_fonts = be32toh_local(ttc.num_fonts);

    if (ttc.signature != 0x74746366) { // 'ttcf'
        fclose(file);
        return false;
    }

    int num_to_parse = ttc.num_fonts < MAX_TTC_FONTS ? (int)ttc.num_fonts : MAX_TTC_FONTS;

    for (int i = 0; i < num_to_parse; i++) {
        uint32_t offset;
        if (fread(&offset, 4, 1, file) != 1) break;
        offset = be32toh_local(offset);

        long saved_pos = ftell(file);

        // seek to sub-font and parse
        if (fseek(file, offset, SEEK_SET) != 0) {
            fseek(file, saved_pos, SEEK_SET);
            continue;
        }

        TTF_Header header;
        if (fread(&header, sizeof(header), 1, file) != 1) {
            fseek(file, saved_pos, SEEK_SET);
            continue;
        }
        header.scaler_type = be32toh_local(header.scaler_type);
        header.num_tables  = be16toh_local(header.num_tables);

        TTF_Table_Dir* tables = NULL;
        if (!read_ttf_table_directory(file, &header, &tables)) {
            fseek(file, saved_pos, SEEK_SET);
            continue;
        }

        FontEntry* entry = (FontEntry*)pool_calloc(db->pool, sizeof(FontEntry));
        entry->file_path = arena_strdup(arena, file_path);
        entry->format = FONT_FORMAT_TTC;
        entry->is_collection = true;
        entry->collection_index = i;
        entry->weight = 400;
        entry->style = FONT_SLANT_NORMAL;

        TTF_Table_Dir* name_tbl = find_ttf_table(tables, header.num_tables, TTF_TAG_NAME);
        if (name_tbl) parse_name_table(file, name_tbl, entry, arena);

        TTF_Table_Dir* os2_tbl = find_ttf_table(tables, header.num_tables, TTF_TAG_OS2);
        if (os2_tbl) parse_os2_table(file, os2_tbl, entry);

        free(tables);

        if (entry->family_name) {
            arraylist_append(db->all_fonts, entry);
        }

        fseek(file, saved_pos, SEEK_SET);
    }

    fclose(file);
    return true;
}

// ============================================================================
// File scanning helpers
// ============================================================================

static bool is_font_file(const char* filename) {
    if (!filename) return false;
    size_t len = strlen(filename);

    // check extensions: .ttf, .otf, .ttc, .woff, .woff2
    if (len >= 4) {
        const char* ext = filename + len - 4;
        if (strcasecmp(ext, ".ttf") == 0) return true;
        if (strcasecmp(ext, ".otf") == 0) return true;
        if (strcasecmp(ext, ".ttc") == 0) return true;
    }
    if (len >= 5) {
        if (strcasecmp(filename + len - 5, ".woff") == 0) return true;
    }
    if (len >= 6) {
        if (strcasecmp(filename + len - 6, ".woff2") == 0) return true;
    }

    return false;
}

static bool is_valid_font_file_size(off_t size) {
    return size >= 1024 && size <= 50 * 1024 * 1024;
}

static bool should_skip_directory(const char* name) {
    if (!name) return true;
    static const char* skip_dirs[] = {
        "Cache", "Temp", "Logs", "Documentation",
        "Removed", "Obsolete", "Backup", "__MACOSX",
        NULL
    };
    for (int i = 0; skip_dirs[i]; i++) {
        if (strcmp(name, skip_dirs[i]) == 0) return true;
    }
    return false;
}

static bool is_priority_font_family(const char* name) {
    if (!name) return false;
    for (int i = 0; priority_font_families[i]; i++) {
        if (strcasecmp(name, priority_font_families[i]) == 0) return true;
    }
    return false;
}

// create a placeholder FontEntry from a font file path (no parsing)
static FontEntry* create_font_placeholder(const char* file_path, Pool* pool, Arena* arena) {
    FontEntry* entry = (FontEntry*)pool_calloc(pool, sizeof(FontEntry));
    if (!entry) return NULL;

    entry->file_path = arena_strdup(arena, file_path);
    entry->is_placeholder = true;
    entry->weight = 400;
    entry->style = FONT_SLANT_NORMAL;

    // detect format from extension
    entry->format = font_detect_format_ext(file_path);

    // guess family name from filename (heuristic — refined on parse)
    const char* base = strrchr(file_path, '/');
    if (!base) base = strrchr(file_path, '\\');
    if (base) base++; else base = file_path;

    char buf[MAX_FONT_FAMILY_NAME];
    strncpy(buf, base, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // strip extension
    char* dot = strrchr(buf, '.');
    if (dot) *dot = '\0';

    // strip common suffixes like -Regular, -Bold, etc.
    char* dash = strrchr(buf, '-');
    if (dash) {
        if (strcasecmp(dash, "-Regular") == 0 || strcasecmp(dash, "-Bold") == 0 ||
            strcasecmp(dash, "-Italic") == 0 || strcasecmp(dash, "-BoldItalic") == 0 ||
            strcasecmp(dash, "-Light") == 0 || strcasecmp(dash, "-Medium") == 0 ||
            strcasecmp(dash, "-Semibold") == 0 || strcasecmp(dash, "-Thin") == 0 ||
            strcasecmp(dash, "-Black") == 0 || strcasecmp(dash, "-ExtraBold") == 0 ||
            strcasecmp(dash, "-ExtraLight") == 0 || strcasecmp(dash, "-Heavy") == 0) {
            *dash = '\0';
        }
    }

    entry->family_name = arena_strdup(arena, buf);
    return entry;
}

#ifndef _WIN32
static void scan_directory_recursive(FontDatabase* db, const char* dir_path, int max_depth) {
    if (max_depth <= 0) return;

    DIR* dir = opendir(dir_path);
    if (!dir) return;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        // skip hidden files and . / ..
        if (ent->d_name[0] == '.') continue;

        char full_path[MAX_FONT_FILE_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (!should_skip_directory(ent->d_name)) {
                scan_directory_recursive(db, full_path, max_depth - 1);
            }
        } else if (S_ISREG(st.st_mode)) {
            if (is_font_file(ent->d_name) && is_valid_font_file_size(st.st_size)) {
                FontEntry* placeholder = create_font_placeholder(full_path, db->pool, db->arena);
                if (placeholder) {
                    placeholder->file_mtime = st.st_mtime;
                    placeholder->file_size = (size_t)st.st_size;
                    arraylist_append(db->all_fonts, placeholder);
                }
            }
        }

        // stop if we have enough fonts discovered
        if (db->all_fonts->length > 500) break;
    }

    closedir(dir);
}
#else
static void scan_directory_recursive(FontDatabase* db, const char* dir_path, int max_depth) {
    (void)db; (void)dir_path; (void)max_depth;
    // TODO: Windows directory scanning with FindFirstFile/FindNextFile
    log_debug("font_database: Windows directory scanning not implemented yet");
}
#endif

// parse a placeholder in-place
static bool parse_placeholder_font(FontEntry* entry, Arena* arena) {
    if (!entry || !entry->is_placeholder || !entry->file_path) return false;

    // check if TTC
    FontFormat format = font_detect_format_ext(entry->file_path);
    if (format == FONT_FORMAT_TTC) {
        entry->is_placeholder = false;
        return false; // TTC handled separately
    }

    // WOFF/WOFF2 placeholders: we skip binary parsing (need decompression first),
    // keep the filename-guessed family name from create_font_placeholder
    if (format == FONT_FORMAT_WOFF || format == FONT_FORMAT_WOFF2) {
        entry->is_placeholder = false;
        return true; // keep guessed metadata
    }

    bool ok = parse_font_metadata(entry->file_path, entry, arena);
    entry->is_placeholder = false;
    return ok;
}

// on-demand font loading
static FontEntry* lazy_load_font(FontDatabase* db, const char* file_path) {
    // check if already loaded
    FontEntry search = {.file_path = (char*)file_path};
    FontEntry* existing = (FontEntry*)hashmap_get(db->file_paths, &search);
    if (existing && !existing->is_placeholder) return existing;

    FontFormat format = font_detect_format_ext(file_path);

    if (format == FONT_FORMAT_TTC) {
        parse_ttc_font_metadata(file_path, db, db->arena);
        // find first entry from this file
        for (int i = 0; i < db->all_fonts->length; i++) {
            FontEntry* e = (FontEntry*)db->all_fonts->data[i];
            if (e->file_path && strcmp(e->file_path, file_path) == 0 && !e->is_placeholder) {
                return e;
            }
        }
        return NULL;
    }

    // single font file
    FontEntry* entry = (FontEntry*)pool_calloc(db->pool, sizeof(FontEntry));
    entry->file_path = arena_strdup(db->arena, file_path);
    entry->weight = 400;
    entry->style = FONT_SLANT_NORMAL;
    entry->format = format;

    if (format == FONT_FORMAT_WOFF || format == FONT_FORMAT_WOFF2) {
        // WOFF/WOFF2: use filename as family name; actual parsing on load
        const char* base = strrchr(file_path, '/');
        if (!base) base = strrchr(file_path, '\\');
        if (base) base++; else base = file_path;
        char buf[256];
        strncpy(buf, base, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* dot = strrchr(buf, '.');
        if (dot) *dot = '\0';
        entry->family_name = arena_strdup(db->arena, buf);
    } else {
        parse_font_metadata(file_path, entry, db->arena);
    }

    if (entry->family_name) {
        arraylist_append(db->all_fonts, entry);
        return entry;
    }

    return NULL;
}

// ============================================================================
// Organize fonts into families
// ============================================================================

static void organize_fonts_into_families(FontDatabase* db) {
    for (int i = 0; i < db->all_fonts->length; i++) {
        FontEntry* entry = (FontEntry*)db->all_fonts->data[i];
        if (!entry || !entry->family_name) continue;

        // find or create family
        FontFamily search = {.family_name = entry->family_name};
        FontFamily* family = (FontFamily*)hashmap_get(db->families, &search);

        if (!family) {
            FontFamily new_fam = {0};
            new_fam.family_name = entry->family_name;
            new_fam.fonts = arraylist_new(0);
            new_fam.is_system_family = true;
            hashmap_set(db->families, &new_fam);
            family = (FontFamily*)hashmap_get(db->families, &search);
        }

        if (family && family->fonts) {
            arraylist_append(family->fonts, entry);
        }

        // index by postscript name
        if (entry->postscript_name) {
            hashmap_set(db->postscript_names, entry);
        }

        // index by file path
        if (entry->file_path) {
            hashmap_set(db->file_paths, entry);
        }
    }
}

// ============================================================================
// Font matching
// ============================================================================

static float calculate_match_score(FontEntry* entry, FontDatabaseCriteria* criteria) {
    if (!entry || !criteria) return 0.0f;
    float score = 0.0f;

    // family name match (40 points)
    if (entry->family_name && criteria->family_name[0]) {
        if (str_ieq(entry->family_name, strlen(entry->family_name),
                     criteria->family_name, strlen(criteria->family_name))) {
            score += 40.0f;
        } else {
            // check generic family fallback
            for (int g = 0; generic_families[g].generic; g++) {
                if (str_ieq(criteria->family_name, strlen(criteria->family_name),
                             generic_families[g].generic, strlen(generic_families[g].generic))) {
                    for (int p = 0; generic_families[g].preferred[p]; p++) {
                        if (str_ieq(entry->family_name, strlen(entry->family_name),
                                     generic_families[g].preferred[p],
                                     strlen(generic_families[g].preferred[p]))) {
                            score += 25.0f;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    // weight proximity (20 points max)
    if (criteria->weight > 0) {
        int diff = abs(entry->weight - criteria->weight);
        if (diff == 0) score += 20.0f;
        else if (diff <= 100) score += 15.0f;
        else if (diff <= 200) score += 10.0f;
        else if (diff <= 300) score += 5.0f;
    }

    // style match (15 points)
    if (entry->style == criteria->style) {
        score += 15.0f;
    }

    // monospace preference (10 points)
    if (criteria->prefer_monospace && entry->is_monospace) {
        score += 10.0f;
    }

    // unicode codepoint support (15 points, hard requirement if specified)
    if (criteria->required_codepoint > 0) {
        bool supported = false;
        FontUnicodeRange* range = entry->unicode_ranges;
        while (range) {
            if (criteria->required_codepoint >= range->start_codepoint &&
                criteria->required_codepoint <= range->end_codepoint) {
                supported = true;
                break;
            }
            range = range->next;
        }
        if (!range && criteria->required_codepoint >= 0x20 && criteria->required_codepoint <= 0x7E) {
            supported = true; // basic ASCII fallback
        }
        if (supported) score += 15.0f;
    }

    return score;
}

// ============================================================================
// Public-facing query functions
// ============================================================================

FontDatabaseResult font_database_find_best_match_internal(FontDatabase* db, FontDatabaseCriteria* criteria) {
    FontDatabaseResult result = {0};
    if (!db || !criteria || !criteria->family_name[0]) return result;

    // ensure scan has happened
    if (!db->scanned) {
        font_database_scan_internal(db);
    }

    // first: check if family already organized
    FontFamily search_fam = {.family_name = criteria->family_name};
    FontFamily* family = (FontFamily*)hashmap_get(db->families, &search_fam);

    // if not found, try lazy loading matching placeholders
    if (!family) {
        for (int i = 0; i < db->all_fonts->length; i++) {
            FontEntry* e = (FontEntry*)db->all_fonts->data[i];
            if (!e || !e->is_placeholder || !e->family_name) continue;
            if (str_ieq(e->family_name, strlen(e->family_name),
                         criteria->family_name, strlen(criteria->family_name))) {
                if (e->format == FONT_FORMAT_TTC) {
                    parse_ttc_font_metadata(e->file_path, db, db->arena);
                } else {
                    parse_placeholder_font(e, db->arena);
                }
            }
        }
        // re-organize
        organize_fonts_into_families(db);
        family = (FontFamily*)hashmap_get(db->families, &search_fam);
    }

    // score all fonts in the family (or all fonts if no exact family)
    float best_score = -1.0f;
    FontEntry* best_font = NULL;

    if (family && family->fonts) {
        for (int i = 0; i < family->fonts->length; i++) {
            FontEntry* e = (FontEntry*)family->fonts->data[i];
            if (!e) continue;
            float score = calculate_match_score(e, criteria);
            if (score > best_score) {
                best_score = score;
                best_font = e;
            }
        }
    }

    if (!best_font) {
        // try all fonts
        for (int i = 0; i < db->all_fonts->length; i++) {
            FontEntry* e = (FontEntry*)db->all_fonts->data[i];
            if (!e || e->is_placeholder) continue;
            float score = calculate_match_score(e, criteria);
            if (score > best_score) {
                best_score = score;
                best_font = e;
            }
        }
    }

    if (best_font) {
        result.font = best_font;
        result.match_score = best_score / 100.0f;
        result.exact_family_match = (best_score >= 40.0f);
    }

    return result;
}

ArrayList* font_database_find_all_matches_internal(FontDatabase* db, const char* family_name) {
    if (!db || !family_name) return NULL;

    FontFamily search = {.family_name = (char*)family_name};
    FontFamily* family = (FontFamily*)hashmap_get(db->families, &search);

    if (!family || !family->fonts) return NULL;

    // return a copy of the fonts array
    ArrayList* result = arraylist_new(0);
    for (int i = 0; i < family->fonts->length; i++) {
        arraylist_append(result, family->fonts->data[i]);
    }
    return result;
}

FontEntry* font_database_get_by_postscript_name_internal(FontDatabase* db, const char* ps_name) {
    if (!db || !ps_name) return NULL;
    FontEntry search = {.postscript_name = (char*)ps_name};
    return (FontEntry*)hashmap_get(db->postscript_names, &search);
}

// ============================================================================
// Database scanning — 3-phase approach
// ============================================================================

bool font_database_scan_internal(FontDatabase* db) {
    if (!db) return false;
    if (db->scanned) return true;

    log_info("font_database_scan: starting 3-phase scan");
    time_t start = time(NULL);

    // Phase 1: Build font file inventory (no parsing, just placeholders)
    log_info("font_database_scan: Phase 1 — discovering font files");
    for (int i = 0; i < db->scan_directories->length; i++) {
        const char* dir = (const char*)db->scan_directories->data[i];
        int depth = 3; // allow deeper recursion for system dirs
        scan_directory_recursive(db, dir, depth);
    }
    log_info("font_database_scan: Phase 1 complete — %d font files discovered",
             db->all_fonts->length);

    // Phase 2: Parse priority fonts (web-safe fonts for fast startup)
    log_info("font_database_scan: Phase 2 — parsing priority fonts");
    int priority_parsed = 0;
    for (int i = 0; i < db->all_fonts->length && priority_parsed < 20; i++) {
        FontEntry* e = (FontEntry*)db->all_fonts->data[i];
        if (!e || !e->is_placeholder || !e->family_name) continue;

        if (is_priority_font_family(e->family_name)) {
            if (e->format == FONT_FORMAT_TTC) {
                parse_ttc_font_metadata(e->file_path, db, db->arena);
                e->is_placeholder = false;
            } else {
                parse_placeholder_font(e, db->arena);
            }
            priority_parsed++;
        }
    }

#ifdef _WIN32
    // windows: also scan registry for additional fonts
    scan_windows_registry_fonts(db);
#endif

    log_info("font_database_scan: Phase 2 complete — %d priority fonts parsed", priority_parsed);

    // Phase 3: Organize into families
    log_info("font_database_scan: Phase 3 — organizing into families");
    organize_fonts_into_families(db);

    db->scanned = true;
    db->last_scan = time(NULL);
    db->cache_dirty = true;

    log_info("font_database_scan: complete — %d fonts in %d families (%.0f seconds)",
             db->all_fonts->length, (int)hashmap_count(db->families),
             difftime(time(NULL), start));
    return true;
}

// ============================================================================
// Utility functions
// ============================================================================

const char* font_format_to_str(FontFormat format) {
    switch (format) {
        case FONT_FORMAT_TTF:     return "TTF";
        case FONT_FORMAT_OTF:     return "OTF";
        case FONT_FORMAT_TTC:     return "TTC";
        case FONT_FORMAT_WOFF:    return "WOFF";
        case FONT_FORMAT_WOFF2:   return "WOFF2";
        case FONT_FORMAT_UNKNOWN: return "Unknown";
        default:                  return "Unknown";
    }
}
