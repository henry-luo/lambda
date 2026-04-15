// npm_tarball.cpp — Extract .tgz (gzipped tar) archives for npm packages

#include "npm_tarball.h"
#include "../lib/file.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <zlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Minimal tar reader (POSIX/UStar format)
// ---------------------------------------------------------------------------

// tar header: 512 bytes
struct TarHeader {
    char name[100];       // 0
    char mode[8];         // 100
    char uid[8];          // 108
    char gid[8];          // 116
    char size[12];        // 124
    char mtime[12];       // 136
    char checksum[8];     // 148
    char typeflag;        // 156
    char linkname[100];   // 157
    char magic[6];        // 257
    char version[2];      // 263
    char uname[32];       // 265
    char gname[32];       // 297
    char devmajor[8];     // 329
    char devminor[8];     // 337
    char prefix[155];     // 345
    char pad[12];         // 500
};

static_assert(sizeof(TarHeader) == 512, "tar header must be 512 bytes");

// parse octal field
static size_t tar_parse_octal(const char* field, int len) {
    size_t val = 0;
    for (int i = 0; i < len && field[i]; i++) {
        if (field[i] >= '0' && field[i] <= '7') {
            val = (val << 3) | (field[i] - '0');
        } else if (field[i] == ' ' || field[i] == '\0') {
            continue;
        }
    }
    return val;
}

// check if a tar block is all zeros (end-of-archive marker)
static bool is_zero_block(const char* block) {
    for (int i = 0; i < 512; i++) {
        if (block[i] != 0) return false;
    }
    return true;
}

// construct full path from tar header, stripping npm's "package/" prefix
static void tar_entry_path(const TarHeader* hdr, char* out, int out_size) {
    // build full name from prefix + name
    if (hdr->prefix[0]) {
        snprintf(out, out_size, "%.*s/%.*s",
                 (int)sizeof(hdr->prefix), hdr->prefix,
                 (int)sizeof(hdr->name), hdr->name);
    } else {
        snprintf(out, out_size, "%.*s", (int)sizeof(hdr->name), hdr->name);
    }

    // strip leading "package/" prefix (npm convention)
    const char* stripped = out;
    if (strncmp(stripped, "package/", 8) == 0) {
        stripped += 8;
    } else if (strncmp(stripped, "package", 7) == 0 && stripped[7] == '\0') {
        stripped += 7;
    }

    if (stripped != out) {
        memmove(out, stripped, strlen(stripped) + 1);
    }
}

// ---------------------------------------------------------------------------
// gzip decompression + tar extraction
// ---------------------------------------------------------------------------

int npm_extract_tarball(const char* tgz_path, const char* dest_dir) {
    if (!tgz_path || !dest_dir) return -1;

    // read the .tgz file
    size_t tgz_size = 0;
    char* tgz_data = read_binary_file(tgz_path, &tgz_size);
    if (!tgz_data || tgz_size == 0) {
        log_error("npm tarball: cannot read %s", tgz_path);
        return -1;
    }

    // gzip decompress using zlib
    z_stream strm = {};
    strm.next_in = (Bytef*)tgz_data;
    strm.avail_in = (uInt)tgz_size;

    // 15 + 16 = gzip decoding
    if (inflateInit2(&strm, 15 + 16) != Z_OK) {
        log_error("npm tarball: zlib init failed");
        mem_free(tgz_data);
        return -1;
    }

    // decompress into growing buffer
    size_t tar_cap = tgz_size * 4;  // initial estimate
    size_t tar_len = 0;
    char* tar_data = (char*)mem_alloc(tar_cap, MEM_CAT_JS_RUNTIME);

    int zret;
    do {
        if (tar_len + 65536 > tar_cap) {
            tar_cap *= 2;
            tar_data = (char*)mem_realloc(tar_data, tar_cap, MEM_CAT_JS_RUNTIME);
        }
        strm.next_out = (Bytef*)(tar_data + tar_len);
        strm.avail_out = (uInt)(tar_cap - tar_len);

        zret = inflate(&strm, Z_NO_FLUSH);
        tar_len = tar_cap - strm.avail_out;

        if (zret == Z_MEM_ERROR || zret == Z_DATA_ERROR) {
            log_error("npm tarball: zlib inflate error %d", zret);
            inflateEnd(&strm);
            mem_free(tar_data);
            mem_free(tgz_data);
            return -1;
        }
    } while (zret != Z_STREAM_END);

    tar_len = strm.total_out;
    inflateEnd(&strm);
    mem_free(tgz_data);

    log_info("npm tarball: decompressed %zu → %zu bytes", tgz_size, tar_len);

    // ensure destination directory exists
    file_ensure_dir(dest_dir);

    // process tar entries
    size_t offset = 0;
    int file_count = 0;
    int dir_count = 0;

    while (offset + 512 <= tar_len) {
        const char* block = tar_data + offset;

        // check for end-of-archive (two zero blocks)
        if (is_zero_block(block)) break;

        const TarHeader* hdr = (const TarHeader*)block;
        offset += 512;

        // get entry name
        char entry_path[1024];
        tar_entry_path(hdr, entry_path, sizeof(entry_path));

        // skip empty paths
        if (entry_path[0] == '\0') continue;

        // get file size
        size_t file_size = tar_parse_octal(hdr->size, sizeof(hdr->size));

        // build destination path
        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dest_dir, entry_path);

        // check typeflag
        switch (hdr->typeflag) {
            case '5':  // directory
            case 'D': {
                file_ensure_dir(full_path);
                dir_count++;
                break;
            }
            case '\0':  // regular file (old-style)
            case '0': { // regular file
                // ensure parent directory exists
                char* dir = file_path_dirname(full_path);
                if (dir) {
                    file_ensure_dir(dir);
                    free(dir);
                }

                // extract file data
                if (offset + file_size > tar_len) {
                    log_error("npm tarball: truncated entry '%s'", entry_path);
                    break;
                }

                int wret = write_binary_file(full_path, tar_data + offset, file_size);
                if (wret < 0) {
                    log_error("npm tarball: failed to write '%s'", full_path);
                } else {
                    file_count++;
                }
                break;
            }
            case '2': { // symlink
                char link_target[256];
                snprintf(link_target, sizeof(link_target), "%.*s",
                         (int)sizeof(hdr->linkname), hdr->linkname);
                // ensure parent dir
                char* dir = file_path_dirname(full_path);
                if (dir) {
                    file_ensure_dir(dir);
                    free(dir);
                }
                file_symlink(link_target, full_path);
                break;
            }
            default:
                // skip other types (hard links, block devices, etc.)
                break;
        }

        // advance past file data (rounded up to 512-byte boundary)
        if (file_size > 0) {
            size_t blocks = (file_size + 511) / 512;
            offset += blocks * 512;
        }
    }

    mem_free(tar_data);
    log_info("npm tarball: extracted %d files, %d dirs to %s", file_count, dir_count, dest_dir);
    return 0;
}
