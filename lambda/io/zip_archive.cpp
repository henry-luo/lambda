#include "zip_archive.hpp"
#include "../../lib/endian.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/str.h"
#include "../../lib/utf.h"

#include <limits.h>
#include <stdarg.h>
#include <zlib.h>

static constexpr uint32_t ZIP_NO_ENTRY = UINT32_MAX;
static constexpr uint32_t ZIP_LOCAL = 0x04034b50;
static constexpr uint32_t ZIP_CENTRAL = 0x02014b50;
static constexpr uint32_t ZIP_END = 0x06054b50;
static constexpr uint32_t ZIP64_END = 0x06064b50;
static constexpr uint32_t ZIP64_LOCATOR = 0x07064b50;
static constexpr uint32_t ZIP_DESCRIPTOR = 0x08074b50;

static bool zip_fail(ZipError* error, const char* format, ...) {
    if (error) {
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
        log_debug("ZIP_FAILURE: %s", error->message);
    }
    return false;
}

ZipLimits zip_default_limits() {
    return {256ULL << 20, 64ULL << 20, 256ULL << 20, 1ULL << 30,
        1000000, 4096, 128, 8};
}

static bool zip_range(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

static uint32_t zip_crc(const uint8_t* bytes, size_t length) {
    uLong crc = crc32(0L, Z_NULL, 0);
    while (length) {
        uInt chunk = length > UINT_MAX ? UINT_MAX : (uInt)length;
        crc = crc32(crc, bytes, chunk);
        bytes += chunk;
        length -= chunk;
    }
    return (uint32_t)crc;
}

bool zip_source_expected(const char* name, const void* bytes, size_t length) {
    const uint8_t* data = (const uint8_t*)bytes;
    if (length >= 4 && data && (read_le32(data) == ZIP_LOCAL ||
            read_le32(data) == ZIP_END || read_le32(data) == ZIP64_END)) return true;
    const char* extension = name ? strrchr(name, '.') : nullptr;
    static const char* const extensions[] = {
        ".zip", ".docx", ".docm", ".xlsx", ".xlsm", ".pptx", ".pptm",
        ".epub", ".jar", ".war", ".odt", ".ods", ".odp", nullptr
    };
    for (size_t i = 0; extension && extensions[i]; i++) {
        if (str_icmp_cstr(extension, extensions[i]) == 0) return true;
    }
    return false;
}

static uint64_t zip_name_hash(const void* value, uint64_t a, uint64_t b) {
    const ZipEntry* entry = *(ZipEntry* const*)value;
    return hashmap_sip(entry->path, strlen(entry->path), a, b);
}

static int zip_name_compare(const void* a, const void* b, void*) {
    return strcmp((*(ZipEntry* const*)a)->path, (*(ZipEntry* const*)b)->path);
}

ZipEntry* zip_archive_entry(const ZipArchive* archive, uint32_t index) {
    return archive && index < (uint32_t)archive->entries->length
        ? (ZipEntry*)archive->entries->data[index] : nullptr;
}

ZipArchive* zip_archive_retain(ZipArchive* archive) {
    return archive && ref_count_retain(&archive->refs) ? archive : nullptr;
}

void zip_archive_release(ZipArchive* archive) {
    if (!archive || ref_count_release(&archive->refs) != REF_COUNT_LAST) return;
    if (archive->entries) {
        for (int i = 0; i < archive->entries->length; i++) {
            ZipEntry* entry = (ZipEntry*)archive->entries->data[i];
            byte_storage_release(entry->payload);
            mem_free(entry->error);
            mem_free(entry->path);
            mem_free(entry);
        }
        arraylist_free(archive->entries);
    }
    if (archive->names) hashmap_free(archive->names);
    byte_storage_release(archive->snapshot);
    if (archive->budget && ref_count_release(&archive->budget->refs) == REF_COUNT_LAST) {
        mem_free(archive->budget);
    }
    mem_free(archive);
}

static ZipEntry* zip_index_add(ZipArchive* archive, const char* name,
        uint32_t parent, bool directory, ZipError* error) {
    size_t bytes = strlen(name) + 1 + sizeof(ZipEntry) + sizeof(ZipEntry*) * 3;
    const ZipLimits& limits = archive->budget->limits;
    if ((uint32_t)archive->entries->length > limits.entries ||
            !zip_range(archive->index_bytes, bytes, limits.index_bytes)) {
        zip_fail(error, "ZIP index/entry limit exceeded");
        return nullptr;
    }
    ZipEntry* entry = (ZipEntry*)mem_calloc(1, sizeof(ZipEntry), MEM_CAT_INPUT_OTHER);
    if (!entry) { zip_fail(error, "ZIP index allocation failed"); return nullptr; }
    entry->path = mem_strdup(name, MEM_CAT_INPUT_OTHER);
    entry->directory = directory;
    entry->index = (uint32_t)archive->entries->length;
    entry->parent = parent;
    entry->first_child = entry->last_child = entry->next_sibling = ZIP_NO_ENTRY;
    if (!entry->path || !arraylist_append(archive->entries, entry)) {
        mem_free(entry->path);
        mem_free(entry);
        zip_fail(error, "ZIP index allocation failed");
        return nullptr;
    }
    hashmap_set(archive->names, &entry);
    if (hashmap_oom(archive->names)) {
        zip_fail(error, "ZIP name index allocation failed");
        return nullptr;
    }
    archive->index_bytes += bytes;
    if (parent != ZIP_NO_ENTRY) {
        ZipEntry* owner = zip_archive_entry(archive, parent);
        uint32_t index = (uint32_t)archive->entries->length - 1;
        if (owner->last_child != ZIP_NO_ENTRY) {
            zip_archive_entry(archive, owner->last_child)->next_sibling = index;
        } else owner->first_child = index;
        owner->last_child = index;
        owner->child_count++;
    }
    return entry;
}

static ZipEntry* zip_find_name(ZipArchive* archive, const char* name) {
    ZipEntry key = {};
    key.path = (char*)name;
    ZipEntry* key_pointer = &key;
    ZipEntry* const* found = (ZipEntry* const*)hashmap_get(archive->names, &key_pointer);
    return found ? *found : nullptr;
}

static bool zip_normalize_name(StrBuf* name, const ZipLimits& limits,
        ZipError* error) {
    if (!name->length || name->length > limits.name_bytes || name->str[0] == '/' ||
            (name->length >= 2 && name->str[1] == ':') ||
            memchr(name->str, '\\', name->length) || memchr(name->str, 0, name->length)) {
        return zip_fail(error, "ZIP entry name is absolute, invalid, or exceeds its limit");
    }
    size_t source = 0, destination = 0;
    uint32_t depth = 0;
    while (source < name->length) {
        size_t start = source;
        while (source < name->length && name->str[source] != '/') source++;
        size_t length = source - start;
        if (source < name->length) source++;
        if (length == 2 && name->str[start] == '.' && name->str[start + 1] == '.') {
            return zip_fail(error, "ZIP entry name contains '..' traversal");
        }
        if (!length || (length == 1 && name->str[start] == '.')) continue;
        if (++depth > limits.path_depth) return zip_fail(error, "ZIP path depth limit exceeded");
        if (destination) name->str[destination++] = '/';
        memmove(name->str + destination, name->str + start, length);
        destination += length;
    }
    name->length = destination;
    name->str[destination] = 0;
    return destination != 0 || zip_fail(error, "ZIP entry name normalizes to the archive root");
}

static bool zip_decode_name(const uint8_t* raw, size_t length, uint16_t flags,
        const uint8_t* extra, size_t extra_length, StrBuf* name, ZipError* error) {
    const uint8_t* unicode = nullptr;
    size_t unicode_length = 0;
    for (size_t offset = 0; offset < extra_length;) {
        if (!zip_range(offset, 4, extra_length)) return zip_fail(error, "ZIP extra field is truncated");
        uint16_t id = read_le16(extra + offset), size = read_le16(extra + offset + 2);
        offset += 4;
        if (!zip_range(offset, size, extra_length)) return zip_fail(error, "ZIP extra field is truncated");
        if (id == 0x7075 && size >= 5 && extra[offset] == 1 &&
                read_le32(extra + offset + 1) == zip_crc(raw, length)) {
            if (unicode) return zip_fail(error, "ZIP Unicode name field is duplicated");
            unicode = extra + offset + 5;
            unicode_length = size - 5;
        }
        offset += size;
    }
    if (flags & 0x0800) {
        if (!utf8_valid((const char*)raw, length)) return zip_fail(error, "ZIP entry name is invalid UTF-8");
        if (unicode && (unicode_length != length || memcmp(unicode, raw, length))) {
            return zip_fail(error, "ZIP Unicode entry names disagree");
        }
        strbuf_append_str_n(name, (const char*)raw, length);
    } else if (unicode) {
        if (!utf8_valid((const char*)unicode, unicode_length)) return zip_fail(error, "ZIP Unicode name is invalid UTF-8");
        strbuf_append_str_n(name, (const char*)unicode, unicode_length);
    } else {
        // APPNOTE's legacy CP437 names are independent of locale and iconv availability.
        static const uint16_t cp437_high[] = {
            0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
            0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
            0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
            0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
            0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
            0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
            0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
            0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
            0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
            0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
            0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
            0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
            0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
            0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
            0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
            0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
        };
        for (size_t i = 0; i < length; i++) {
            uint32_t codepoint = raw[i] < 128 ? raw[i] : cp437_high[raw[i] - 128];
            if (!strbuf_append_utf8(name, codepoint)) return zip_fail(error, "ZIP name allocation failed");
        }
    }
    return true;
}

static bool zip_extended_fields(const uint8_t* extra, size_t length,
        uint64_t* size, uint64_t* compressed, uint64_t* local, uint32_t* disk,
        bool* used_zip64, ZipError* error) {
    bool found = false;
    bool needed = *size == UINT32_MAX || *compressed == UINT32_MAX ||
        (local && *local == UINT32_MAX) || (disk && *disk == UINT16_MAX);
    for (size_t offset = 0; offset < length;) {
        if (!zip_range(offset, 4, length)) return zip_fail(error, "ZIP extra field is truncated");
        uint16_t id = read_le16(extra + offset), bytes = read_le16(extra + offset + 2);
        offset += 4;
        if (!zip_range(offset, bytes, length)) return zip_fail(error, "ZIP extra field is truncated");
        if (id == 1) {
            if (found) return zip_fail(error, "ZIP64 extra field is duplicated");
            found = true;
            *used_zip64 = true;
            size_t position = 0;
            uint64_t* fields[] = {size, compressed, local};
            for (uint64_t* field : fields) {
                if (!field || *field != UINT32_MAX) continue;
                if (!zip_range(position, 8, bytes)) return zip_fail(error, "ZIP64 extra field is truncated");
                *field = read_le64(extra + offset + position);
                position += 8;
            }
            if (disk && *disk == UINT16_MAX) {
                if (!zip_range(position, 4, bytes)) return zip_fail(error, "ZIP64 disk field is truncated");
                *disk = read_le32(extra + offset + position);
            }
        }
        offset += bytes;
    }
    return !needed || found || zip_fail(error, "ZIP64 extended information is missing");
}

static bool zip_end_directory_at(ZipArchive* archive, size_t end,
        uint64_t* count, uint64_t* directory_size, ZipError* error) {
    const uint8_t* data = archive->snapshot->data;
    if (read_le16(data + end + 4) || read_le16(data + end + 6) ||
            read_le16(data + end + 8) != read_le16(data + end + 10)) {
        return zip_fail(error, "ZIP multi-disk archives are unsupported");
    }
    *count = read_le16(data + end + 10);
    *directory_size = read_le32(data + end + 12);
    archive->central_offset = read_le32(data + end + 16);
    uint64_t directory_end = end;
    if (end >= 20 && read_le32(data + end - 20) == ZIP64_LOCATOR) {
        archive->zip64 = true;
        const uint8_t* locator = data + end - 20;
        uint64_t record = read_le64(locator + 8);
        if (read_le32(locator + 4) || read_le32(locator + 16) != 1 ||
                !zip_range(record, 56, end - 20) || read_le32(data + record) != ZIP64_END) {
            return zip_fail(error, "ZIP64 locator/end record is invalid or multi-disk");
        }
        uint64_t record_size = read_le64(data + record + 4);
        if (record_size < 44 || !zip_range(record + 12, record_size, end - 20) ||
                record + 12 + record_size != end - 20 ||
                read_le32(data + record + 16) || read_le32(data + record + 20) ||
                read_le64(data + record + 24) != read_le64(data + record + 32)) {
            return zip_fail(error, "ZIP64 end record is inconsistent or multi-disk");
        }
        uint64_t full_count = read_le64(data + record + 32);
        uint64_t full_size = read_le64(data + record + 40);
        uint64_t full_offset = read_le64(data + record + 48);
        if ((*count != UINT16_MAX && *count != full_count) ||
                (*directory_size != UINT32_MAX && *directory_size != full_size) ||
                (archive->central_offset != UINT32_MAX && archive->central_offset != full_offset)) {
            return zip_fail(error, "ZIP32/ZIP64 end records disagree");
        }
        *count = full_count;
        *directory_size = full_size;
        archive->central_offset = full_offset;
        directory_end = record;
    } else if (*count == UINT16_MAX || *directory_size == UINT32_MAX ||
            archive->central_offset == UINT32_MAX) {
        return zip_fail(error, "ZIP64 end record/locator is missing");
    }
    if (!zip_range(archive->central_offset, *directory_size, directory_end) ||
            archive->central_offset + *directory_size != directory_end ||
            *count > archive->budget->limits.entries ||
            *directory_size > archive->budget->limits.index_bytes ||
            *count > *directory_size / 46 || (!*count && archive->central_offset)) {
        return zip_fail(error, "ZIP central directory is invalid or exceeds its limits");
    }
    return true;
}

static bool zip_end_directory(ZipArchive* archive, uint64_t* count,
        uint64_t* directory_size, ZipError* error) {
    const uint8_t* data = archive->snapshot->data;
    size_t length = archive->snapshot->capacity;
    if (length < 22) return zip_fail(error, "ZIP end record is missing or truncated");
    size_t end = length - 22;
    size_t minimum = length > 65557 ? length - 65557 : 0;
    ZipError candidate = {};
    for (;;) {
        if (read_le32(data + end) == ZIP_END &&
                (size_t)read_le16(data + end + 20) == length - end - 22) {
            // A comment may itself contain an end signature and matching length.
            // Check its directory relationship before accepting that candidate.
            archive->zip64 = false;
            if (zip_end_directory_at(archive, end, count, directory_size, &candidate)) return true;
        }
        if (end == minimum) break;
        end--;
    }
    return zip_fail(error, "%s", candidate.message[0] ? candidate.message : "ZIP end record is missing or corrupt");
}

static bool zip_descriptor_matches(const uint8_t* data, uint64_t offset,
        uint64_t limit, const ZipEntry* entry, bool wide, uint64_t* end) {
    uint64_t size = wide ? 20 : 12;
    if (!zip_range(offset, size, limit) || read_le32(data + offset) != entry->crc) return false;
    uint64_t compressed = wide ? read_le64(data + offset + 4) : read_le32(data + offset + 4);
    uint64_t expanded = wide ? read_le64(data + offset + 12) : read_le32(data + offset + 8);
    if (compressed != entry->compressed_size || expanded != entry->size) return false;
    *end = offset + size;
    return true;
}

static bool zip_validate_local(ZipArchive* archive, ZipEntry* entry,
        const uint8_t* name, size_t name_length, ZipError* error) {
    const uint8_t* data = archive->snapshot->data;
    uint64_t offset = entry->local_offset, limit = archive->central_offset;
    if (!zip_range(offset, 30, limit) || read_le32(data + offset) != ZIP_LOCAL) {
        return zip_fail(error, "ZIP local header is missing: %s", entry->path);
    }
    const uint8_t* local = data + offset;
    uint16_t local_name = read_le16(local + 26), extra = read_le16(local + 28);
    if (!zip_range(offset + 30, (uint64_t)local_name + extra, limit) ||
            local_name != name_length || read_le16(local + 4) > 45 || memcmp(local + 30, name, name_length) ||
            read_le16(local + 6) != entry->flags || read_le16(local + 8) != entry->method) {
        return zip_fail(error, "ZIP central/local headers disagree: %s", entry->path);
    }
    uint64_t size = read_le32(local + 22), compressed = read_le32(local + 18);
    bool wide_descriptor = size == UINT32_MAX || compressed == UINT32_MAX;
    if (!zip_extended_fields(local + 30 + local_name, extra, &size, &compressed,
            nullptr, nullptr, &archive->zip64, error)) return false;
    uint32_t crc = read_le32(local + 14);
    if (!(entry->flags & 8) && (size != entry->size || compressed != entry->compressed_size || crc != entry->crc)) {
        return zip_fail(error, "ZIP local sizes/CRC disagree: %s", entry->path);
    }
    if ((entry->flags & 8) && ((size && size != entry->size) ||
            (compressed && compressed != entry->compressed_size) || (crc && crc != entry->crc))) {
        return zip_fail(error, "ZIP descriptor/local metadata disagree: %s", entry->path);
    }
    entry->data_offset = offset + 30 + local_name + extra;
    if (!zip_range(entry->data_offset, entry->compressed_size, limit)) {
        return zip_fail(error, "ZIP member range is out of bounds: %s", entry->path);
    }
    entry->record_end = entry->data_offset + entry->compressed_size;
    if (entry->flags & 8) {
        uint64_t descriptor = entry->record_end;
        bool wide = wide_descriptor || entry->size >= UINT32_MAX || entry->compressed_size >= UINT32_MAX;
        if (zip_range(descriptor, 4, limit) && read_le32(data + descriptor) == ZIP_DESCRIPTOR &&
                zip_descriptor_matches(data, descriptor + 4, limit, entry, wide, &entry->record_end)) return true;
        if (!zip_descriptor_matches(data, descriptor, limit, entry, wide, &entry->record_end)) {
            return zip_fail(error, "ZIP data descriptor is invalid: %s", entry->path);
        }
    }
    return true;
}

static ZipEntry* zip_index_path(ZipArchive* archive, StrBuf* name,
        bool directory, ZipError* error) {
    uint32_t parent = 0;
    for (size_t position = 0; position <= name->length; position++) {
        if (position != name->length && name->str[position] != '/') continue;
        bool is_parent = position != name->length;
        char saved = name->str[position];
        name->str[position] = 0;
        ZipEntry* entry = zip_find_name(archive, name->str);
        if (!entry) entry = zip_index_add(archive, name->str, parent, is_parent || directory, error);
        else if (!entry->directory || (!is_parent && (!directory || entry->explicit_directory))) {
            zip_fail(error, "ZIP duplicate name or file/directory conflict: %s", name->str);
            entry = nullptr;
        }
        name->str[position] = saved;
        if (!entry) return nullptr;
        if (!is_parent) return entry;
        parent = entry->index;
    }
    return nullptr;
}

static int zip_local_order(const void* a, const void* b) {
    uint64_t left = (*(ZipEntry* const*)a)->local_offset;
    uint64_t right = (*(ZipEntry* const*)b)->local_offset;
    return left < right ? -1 : left > right;
}

static bool zip_build_index(ZipArchive* archive, ZipError* error) {
    uint64_t count = 0, directory_size = 0;
    if (!zip_end_directory(archive, &count, &directory_size, error)) return false;
    if (!zip_index_add(archive, "", ZIP_NO_ENTRY, true, error)) return false;
    const uint8_t* data = archive->snapshot->data;
    uint64_t offset = archive->central_offset;
    uint64_t end = offset + directory_size, declared = 0;
    StrBuf* name = strbuf_new();
    ArrayList* records = arraylist_new(16);
    bool ok = name && records;
    if (!ok) zip_fail(error, "ZIP index allocation failed");
    for (uint64_t index = 0; ok && index < count; index++) {
        if (!zip_range(offset, 46, end) || read_le32(data + offset) != ZIP_CENTRAL) {
            ok = zip_fail(error, "ZIP central directory entry is truncated"); break;
        }
        const uint8_t* central = data + offset;
        uint16_t name_length = read_le16(central + 28), extra_length = read_le16(central + 30);
        uint64_t record_size = 46ULL + name_length + extra_length + read_le16(central + 32);
        uint16_t flags = read_le16(central + 8), method = read_le16(central + 10);
        if (!zip_range(offset, record_size, end)) { ok = zip_fail(error, "ZIP central entry is truncated"); break; }
        if (flags & 0x2041) { ok = zip_fail(error, "ZIP encryption is unsupported"); break; }
        if ((flags & ~0x080e) || (method != 0 && method != 8) || read_le16(central + 6) > 45) {
            ok = zip_fail(error, "ZIP compression method/feature is unsupported"); break;
        }
        uint64_t size = read_le32(central + 24), compressed = read_le32(central + 20);
        uint64_t local = read_le32(central + 42);
        uint32_t disk = read_le16(central + 34);
        const uint8_t* raw_name = central + 46;
        const uint8_t* extra = raw_name + name_length;
        strbuf_reset(name);
        ok = zip_extended_fields(extra, extra_length, &size, &compressed, &local,
            &disk, &archive->zip64, error) &&
            zip_decode_name(raw_name, name_length, flags, extra, extra_length, name, error);
        if (!ok) break;
        uint32_t attributes = read_le32(central + 38);
        uint32_t mode = central[5] == 3 ? attributes >> 16 : 0;
        uint32_t file_type = mode & 0170000;
        bool directory = (name->length && name->str[name->length - 1] == '/') ||
            (attributes & 0x10) || file_type == 0040000;
        if (disk || (file_type && file_type != 0100000 && file_type != 0040000)) {
            ok = zip_fail(error, "ZIP multi-disk or special/link entries are unsupported"); break;
        }
        if ((directory && size) || (method == 0 && size != compressed) ||
                size > archive->budget->limits.member_bytes ||
                !zip_range(declared, size, archive->budget->limits.expanded_bytes)) {
            ok = zip_fail(error, "ZIP member sizes are invalid or exceed expansion limits"); break;
        }
        declared += size;
        if (!zip_normalize_name(name, archive->budget->limits, error)) { ok = false; break; }
        ZipEntry* entry = zip_index_path(archive, name, directory, error);
        if (!entry) { ok = false; break; }
        entry->explicit_directory = directory;
        entry->compressed_size = compressed;
        entry->size = size;
        entry->local_offset = local;
        entry->crc = read_le32(central + 16);
        entry->mode = mode;
        entry->dos_time = read_le32(central + 12);
        entry->method = method;
        entry->flags = flags;
        ok = zip_validate_local(archive, entry, raw_name, name_length, error) && arraylist_append(records, entry);
        offset += record_size;
    }
    if (ok && offset != end) ok = zip_fail(error, "ZIP central directory length/count disagree");
    if (ok && records->length) {
        qsort(records->data, records->length, sizeof(void*), zip_local_order);
        uint64_t previous = 0;
        for (int index = 0; ok && index < records->length; index++) {
            ZipEntry* entry = (ZipEntry*)records->data[index];
            if ((index == 0 && entry->local_offset != 0) || entry->local_offset < previous) {
                ok = zip_fail(error, "ZIP local records overlap or contain a self-extracting prefix");
            }
            previous = entry->record_end;
        }
    }
    if (ok && !zip_range(archive->budget->declared_bytes, declared,
            archive->budget->limits.expanded_bytes)) {
        ok = zip_fail(error, "ZIP nested declared expansion limit exceeded");
    }
    if (ok) archive->budget->declared_bytes += declared;
    if (name) strbuf_free(name);
    if (records) arraylist_free(records);
    return ok;
}

ZipArchive* zip_archive_open(const void* bytes, size_t length,
        const ZipLimits* requested_limits, ZipBudget* parent_budget,
        uint32_t depth, ZipError* error) {
    if (error) error->message[0] = 0;
    ZipLimits limits = parent_budget ? parent_budget->limits
        : requested_limits ? *requested_limits : zip_default_limits();
    if ((!bytes && length) || length > limits.archive_bytes || depth > limits.nesting_depth) {
        zip_fail(error, "ZIP archive size/nesting limit exceeded"); return nullptr;
    }
    ZipArchive* archive = (ZipArchive*)mem_calloc(1, sizeof(ZipArchive), MEM_CAT_INPUT_OTHER);
    if (!archive) { zip_fail(error, "ZIP archive allocation failed"); return nullptr; }
    ref_count_init(&archive->refs);
    archive->depth = depth;
    if (parent_budget && ref_count_retain(&parent_budget->refs)) archive->budget = parent_budget;
    else if (!parent_budget) {
        archive->budget = (ZipBudget*)mem_calloc(1, sizeof(ZipBudget), MEM_CAT_INPUT_OTHER);
        if (archive->budget) {
            ref_count_init(&archive->budget->refs);
            archive->budget->limits = limits;
        }
    }
    archive->snapshot = byte_storage_alloc(length, MEM_CAT_INPUT_OTHER);
    archive->entries = arraylist_new(16);
    archive->names = hashmap_new(sizeof(ZipEntry*), 16, 0, 0, zip_name_hash, zip_name_compare, nullptr, nullptr);
    if (!archive->budget || !archive->snapshot || !archive->entries || !archive->names) {
        zip_fail(error, "ZIP archive allocation failed");
        zip_archive_release(archive);
        return nullptr;
    }
    if (length) memcpy(archive->snapshot->data, bytes, length);
    archive->snapshot->flags |= BYTE_STORAGE_FLAG_READ_ONLY;
    if (!zip_build_index(archive, error)) {
        zip_archive_release(archive);
        return nullptr;
    }
    log_debug("ZIP_INDEX_READY: entries=%d zip64=%d bytes=%zu", archive->entries->length - 1, archive->zip64, length);
    return archive;
}

static bool zip_inflate_bytes(const uint8_t* input, size_t input_length,
        uint8_t* output, size_t output_capacity, size_t expected, ZipError* error) {
    z_stream stream = {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return zip_fail(error, "ZIP DEFLATE initialization failed");
    size_t consumed = 0, produced = 0;
    int result = Z_OK;
    while (result == Z_OK) {
        uInt incoming = input_length - consumed > UINT_MAX ? UINT_MAX : (uInt)(input_length - consumed);
        uInt outgoing = output_capacity - produced > UINT_MAX ? UINT_MAX : (uInt)(output_capacity - produced);
        stream.next_in = (Bytef*)input + consumed;
        stream.avail_in = incoming;
        stream.next_out = output + produced;
        stream.avail_out = outgoing;
        result = inflate(&stream, Z_NO_FLUSH);
        size_t read = incoming - stream.avail_in, written = outgoing - stream.avail_out;
        consumed += read;
        produced += written;
        if (produced > expected || (!read && !written && result == Z_OK)) { result = Z_DATA_ERROR; break; }
    }
    inflateEnd(&stream);
    return (result == Z_STREAM_END && consumed == input_length && produced == expected) ||
        zip_fail(error, "ZIP DEFLATE payload is truncated, corrupt, or has an invalid size");
}

bool zip_entry_bytes(ZipArchive* archive, uint32_t index, ByteSpan* bytes,
        ZipError* error) {
    ZipEntry* entry = zip_archive_entry(archive, index);
    if (!entry || entry->directory) return zip_fail(error, "ZIP content source is not a file");
    if (!entry->decoded) {
        entry->decoded = true;
        ZipError failure = {};
        bool ok = entry->size < SIZE_MAX && zip_range(archive->budget->expanded_bytes,
            entry->size, archive->budget->limits.expanded_bytes);
        if (!ok) zip_fail(&failure, "ZIP aggregate expansion/allocation limit exceeded");
        if (ok) {
            archive->budget->expanded_bytes += entry->size;
            entry->payload = byte_storage_alloc((size_t)entry->size + 1, MEM_CAT_INPUT_OTHER);
            ok = entry->payload != nullptr;
            if (!ok) zip_fail(&failure, "ZIP member allocation failed");
        }
        if (ok) {
            const uint8_t* source = archive->snapshot->data + entry->data_offset;
            if (entry->method == 0) memcpy(entry->payload->data, source, (size_t)entry->size);
            else {
                archive->decompressions++;
                ok = zip_inflate_bytes(source, (size_t)entry->compressed_size,
                    entry->payload->data, entry->payload->capacity, (size_t)entry->size, &failure);
            }
            if (ok && zip_crc(entry->payload->data, (size_t)entry->size) != entry->crc) {
                ok = zip_fail(&failure, "ZIP member CRC mismatch");
            }
        }
        if (ok) {
            entry->payload->data[entry->size] = 0;
            entry->payload->flags |= BYTE_STORAGE_FLAG_READ_ONLY;
        } else {
            entry->error = (ZipError*)mem_alloc(sizeof(ZipError), MEM_CAT_INPUT_OTHER);
            if (entry->error) *entry->error = failure;
            byte_storage_release(entry->payload);
            entry->payload = nullptr;
        }
    }
    if (!entry->payload) return zip_fail(error, "%s: %s", entry->path,
        entry->error ? entry->error->message : "ZIP member decoding failed");
    return byte_span_init(bytes, entry->payload, 0, (size_t)entry->size);
}

ZipWriteOptions zip_default_write_options() {
    return {8, Z_DEFAULT_COMPRESSION, false, false, zip_default_limits()};
}

bool zip_append_bytes(StrBuf* out, const void* bytes, size_t size,
        uint64_t limit, ZipError* error) {
    if (!out || !zip_range(out->length, size, limit) ||
            size > SIZE_MAX - out->length - 1 ||
            !strbuf_ensure_cap(out, out->length + size + 1)) {
        return zip_fail(error, "ZIP encoded size/allocation limit exceeded");
    }
    if (size) memcpy(out->str + out->length, bytes, size);
    out->length += size;
    out->str[out->length] = 0;
    return true;
}

static bool zip_deflate_bytes(const ZipOutputEntry* entry, int level,
        StrBuf* out, uint64_t limit, ZipError* error) {
    z_stream stream = {};
    if (deflateInit2(&stream, level, Z_DEFLATED, -MAX_WBITS, 8,
            Z_DEFAULT_STRATEGY) != Z_OK) return zip_fail(error, "ZIP DEFLATE initialization failed");
    uint8_t buffer[32768];
    size_t consumed = 0;
    int result = Z_OK;
    bool ok = true;
    while (ok && result != Z_STREAM_END) {
        uInt chunk = entry->length - consumed > UINT_MAX ? UINT_MAX : (uInt)(entry->length - consumed);
        stream.next_in = (Bytef*)entry->bytes + consumed;
        stream.avail_in = chunk;
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        result = deflate(&stream, consumed + chunk == entry->length ? Z_FINISH : Z_NO_FLUSH);
        consumed += chunk - stream.avail_in;
        ok = (result == Z_OK || result == Z_STREAM_END) &&
            zip_append_bytes(out, buffer, sizeof(buffer) - stream.avail_out, limit, error);
    }
    deflateEnd(&stream);
    return ok || zip_fail(error, "ZIP DEFLATE encoding failed");
}

static int zip_output_order(const void* a, const void* b) {
    return strcmp((*(ZipOutputEntry* const*)a)->path, (*(ZipOutputEntry* const*)b)->path);
}

bool zip_write_entries(const ArrayList* entries, const ZipWriteOptions* requested,
        StrBuf* out, ZipError* error) {
    ZipWriteOptions options = requested ? *requested : zip_default_write_options();
    if (!entries || !out || (options.method != 0 && options.method != 8) ||
            options.compression_level < -1 || options.compression_level > 9 ||
            (uint64_t)entries->length > options.limits.entries) {
        return zip_fail(error, "ZIP output options/entry count are invalid");
    }
    // Reuse the reader's normalized-name/index rules before encoding anything.
    uint8_t empty[22] = {};
    write_le32(empty, ZIP_END);
    ZipArchive* index = zip_archive_open(empty, sizeof(empty), &options.limits, nullptr, 0, error);
    StrBuf* name = strbuf_new();
    StrBuf* central = strbuf_new();
    ArrayList* ordered = arraylist_new(entries->length);
    bool ok = index && name && central && ordered;
    if (!ok) zip_fail(error, "ZIP writer allocation failed");
    uint64_t total = 0;
    for (int i = 0; ok && i < entries->length; i++) {
        ZipOutputEntry* source = (ZipOutputEntry*)entries->data[i];
        if (!source || !source->path || (!source->bytes && source->length) ||
                (source->directory && source->length) ||
                ((source->mode & 0170000) && (source->mode & 0170000) != 0100000 && (source->mode & 0170000) != 0040000) ||
                source->length > options.limits.member_bytes ||
                !zip_range(total, source->length, options.limits.expanded_bytes)) {
            ok = zip_fail(error, "ZIP output member/expansion limit is invalid"); break;
        }
        total += source->length;
        strbuf_reset(name);
        strbuf_append_str(name, source->path);
        if (!str_utf8_valid(name->str, name->length) ||
                !zip_normalize_name(name, options.limits, error)) { ok = false; break; }
        ZipEntry* member = zip_index_path(index, name, source->directory, error);
        if (!member) { ok = false; break; }
        member->explicit_directory = source->directory;
        ok = arraylist_append(ordered, source);
    }
    if (ok && options.deterministic && ordered->length > 1) {
        qsort(ordered->data, ordered->length, sizeof(void*), zip_output_order);
    }
    strbuf_reset(out);
    bool wide_archive = options.force_zip64 || entries->length >= UINT16_MAX;
    for (int i = 0; ok && i < ordered->length; i++) {
        ZipOutputEntry* source = (ZipOutputEntry*)ordered->data[i];
        strbuf_reset(name);
        strbuf_append_str(name, source->path);
        ok = zip_normalize_name(name, options.limits, error);
        if (!ok) break;
        if (source->directory) strbuf_append_char(name, '/');
        if (name->length > UINT16_MAX) { ok = zip_fail(error, "ZIP output name exceeds record limit"); break; }
        uint16_t method = source->directory ? 0 : options.method;
        uint32_t time = options.deterministic || !source->dos_time ? 0x00210000 : source->dos_time;
        uint32_t crc = zip_crc(source->bytes, source->length);
        uint64_t offset = out->length;
        uLong bound = method == 8 && source->length < UINT32_MAX
            ? compressBound((uLong)source->length) : 0;
        // Reserve ZIP64 before writing when even a sub-4GiB DEFLATE input
        // can exceed ZIP32; uLong may wrap on 32-bit platforms.
        bool wide = options.force_zip64 || source->length >= UINT32_MAX ||
            (method == 8 && (bound >= UINT32_MAX || bound < source->length));
        // Reserve local ZIP64 size fields before compression; patch only owned bytes.
        uint8_t local[50] = {};
        write_le32(local, ZIP_LOCAL);
        write_le16(local + 4, wide ? 45 : 20);
        write_le16(local + 6, 0x800);
        write_le16(local + 8, method);
        write_le32(local + 10, time);
        write_le32(local + 14, crc);
        write_le32(local + 18, wide ? UINT32_MAX : 0);
        write_le32(local + 22, wide ? UINT32_MAX : (uint32_t)source->length);
        write_le16(local + 26, (uint16_t)name->length);
        write_le16(local + 28, wide ? 20 : 0);
        ok = zip_append_bytes(out, local, 30, options.limits.archive_bytes, error) &&
            zip_append_bytes(out, name->str, name->length, options.limits.archive_bytes, error);
        if (wide) {
            write_le16(local, 1); write_le16(local + 2, 16);
            write_le64(local + 4, source->length); write_le64(local + 12, 0);
            ok = ok && zip_append_bytes(out, local, 20, options.limits.archive_bytes, error);
        }
        uint64_t start = out->length;
        ok = ok && (method == 0
            ? zip_append_bytes(out, source->bytes, source->length, options.limits.archive_bytes, error)
            : zip_deflate_bytes(source, options.compression_level, out, options.limits.archive_bytes, error));
        if (!ok) break;
        uint64_t compressed = out->length - start;
        if (!wide && compressed >= UINT32_MAX) { ok = zip_fail(error, "ZIP compressed member requires ZIP64"); break; }
        if (wide) write_le64((uint8_t*)out->str + offset + 30 + name->length + 12, compressed);
        else write_le32((uint8_t*)out->str + offset + 18, (uint32_t)compressed);
        bool wide_offset = options.force_zip64 || offset >= UINT32_MAX;
        uint16_t extra = (wide || wide_offset) ? 4 + (wide ? 16 : 0) + (wide_offset ? 8 : 0) : 0;
        uint8_t record[78] = {};
        write_le32(record, ZIP_CENTRAL);
        write_le16(record + 4, 0x032d);
        write_le16(record + 6, (wide || wide_offset) ? 45 : 20);
        write_le16(record + 8, 0x800);
        write_le16(record + 10, method);
        write_le32(record + 12, time); write_le32(record + 16, crc);
        write_le32(record + 20, wide ? UINT32_MAX : (uint32_t)compressed);
        write_le32(record + 24, wide ? UINT32_MAX : (uint32_t)source->length);
        write_le16(record + 28, (uint16_t)name->length); write_le16(record + 30, extra);
        uint32_t mode = source->mode ? source->mode : (source->directory ? 0040755 : 0100644);
        write_le32(record + 38, (mode << 16) | (source->directory ? 0x10 : 0));
        write_le32(record + 42, wide_offset ? UINT32_MAX : (uint32_t)offset);
        ok = zip_append_bytes(central, record, 46, options.limits.index_bytes, error) &&
            zip_append_bytes(central, name->str, name->length, options.limits.index_bytes, error);
        if (extra) {
            memset(record, 0, sizeof(record));
            write_le16(record, 1); write_le16(record + 2, extra - 4);
            size_t at = 4;
            if (wide) { write_le64(record + at, source->length); write_le64(record + at + 8, compressed); at += 16; }
            if (wide_offset) write_le64(record + at, offset);
            ok = ok && zip_append_bytes(central, record, extra, options.limits.index_bytes, error);
        }
        wide_archive = wide_archive || wide || wide_offset;
    }
    uint64_t central_offset = out->length;
    ok = ok && zip_append_bytes(out, central ? central->str : nullptr, central ? central->length : 0, options.limits.archive_bytes, error);
    wide_archive = wide_archive || central_offset >= UINT32_MAX || (central && central->length >= UINT32_MAX);
    if (ok && wide_archive) {
        uint8_t record[76] = {};
        write_le32(record, ZIP64_END); write_le64(record + 4, 44);
        write_le16(record + 12, 45); write_le16(record + 14, 45);
        write_le64(record + 24, entries->length); write_le64(record + 32, entries->length);
        write_le64(record + 40, central->length); write_le64(record + 48, central_offset);
        write_le32(record + 56, ZIP64_LOCATOR); write_le64(record + 64, out->length);
        write_le32(record + 72, 1);
        ok = zip_append_bytes(out, record, sizeof(record), options.limits.archive_bytes, error);
    }
    if (ok) {
        uint8_t end[22] = {};
        write_le32(end, ZIP_END);
        write_le16(end + 8, wide_archive ? UINT16_MAX : (uint16_t)entries->length);
        write_le16(end + 10, wide_archive ? UINT16_MAX : (uint16_t)entries->length);
        write_le32(end + 12, wide_archive ? UINT32_MAX : (uint32_t)central->length);
        write_le32(end + 16, wide_archive ? UINT32_MAX : (uint32_t)central_offset);
        ok = zip_append_bytes(out, end, sizeof(end), options.limits.archive_bytes, error);
    }
    zip_archive_release(index);
    if (ordered) arraylist_free(ordered);
    if (central) strbuf_free(central);
    if (name) strbuf_free(name);
    if (!ok) strbuf_reset(out);
    return ok;
}
