#pragma once

#include <stdbool.h>
#include <string.h>

static inline void path_str_normalize_separators(char* path) {
    if (!path) return;
    for (char* p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
}

static inline bool path_str_is_win32_separator(char ch) {
    return ch == '\\' || ch == '/';
}

static inline bool path_str_is_drive_letter(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static inline bool path_str_win32_is_absolute(const char* path) {
    if (!path || path[0] == '\0') return false;
    if (path_str_is_win32_separator(path[0]) && path_str_is_win32_separator(path[1])) {
        return true;
    }
    if (path_str_is_win32_separator(path[0])) return true;
    return path_str_is_drive_letter(path[0]) && path[1] == ':' &&
           path_str_is_win32_separator(path[2]);
}

static inline bool path_str_posix_is_absolute(const char* path) {
    return path && path[0] == '/';
}

static inline int path_str_copy(char* result, int result_size, const char* src) {
    if (!result || result_size <= 0) return 0;
    if (!src) {
        result[0] = '\0';
        return 0;
    }
    int len = (int)strlen(src);
    if (len >= result_size) len = result_size - 1;
    memcpy(result, src, (size_t)len);
    result[len] = '\0';
    return len;
}

static inline int path_str_join_posix_into(char* result,
                                           int result_size,
                                           const char* base,
                                           const char* segment) {
    int pos = path_str_copy(result, result_size, base);
    if (!result || result_size <= 0 || !segment || !*segment) return pos;
    if (pos > 0 && pos < result_size - 1 && result[pos - 1] != '/') {
        result[pos++] = '/';
    }
    int seg_len = (int)strlen(segment);
    if (pos + seg_len >= result_size) seg_len = result_size - 1 - pos;
    if (seg_len > 0) {
        memcpy(result + pos, segment, (size_t)seg_len);
        pos += seg_len;
    }
    result[pos] = '\0';
    return pos;
}

static inline int path_str_normalize_lexical_posix(const char* path,
                                                   char* result,
                                                   int result_size,
                                                   bool empty_relative_dot) {
    if (!result || result_size <= 0) return 0;
    result[0] = '\0';
    if (!path) {
        if (empty_relative_dot && result_size > 1) {
            result[0] = '.';
            result[1] = '\0';
            return 1;
        }
        return 0;
    }

    const char* segments[256];
    int segment_lens[256];
    int segment_count = 0;
    bool is_absolute = (path[0] == '/');

    char temp[4096];
    int plen = (int)strlen(path);
    if (plen >= (int)sizeof(temp)) plen = (int)sizeof(temp) - 1;
    memcpy(temp, path, (size_t)plen);
    temp[plen] = '\0';

    const char* p = temp;
    while (*p) {
        while (*p == '/') p++;
        const char* start = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - start);
        if (len == 0 || (len == 1 && start[0] == '.')) continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (segment_count > 0) {
                bool prev_parent = segment_lens[segment_count - 1] == 2 &&
                                   segments[segment_count - 1][0] == '.' &&
                                   segments[segment_count - 1][1] == '.';
                if (!prev_parent) {
                    segment_count--;
                    continue;
                }
            }
            if (!is_absolute && segment_count < 256) {
                segments[segment_count] = start;
                segment_lens[segment_count] = len;
                segment_count++;
            }
            continue;
        }
        if (segment_count < 256) {
            segments[segment_count] = start;
            segment_lens[segment_count] = len;
            segment_count++;
        }
    }

    int pos = 0;
    if (is_absolute && pos < result_size - 1) result[pos++] = '/';
    for (int i = 0; i < segment_count; i++) {
        if (i > 0 && pos < result_size - 1) result[pos++] = '/';
        for (int j = 0; j < segment_lens[i] && pos < result_size - 1; j++) {
            result[pos++] = segments[i][j];
        }
    }
    if (pos == 0 && empty_relative_dot && !is_absolute && result_size > 1) {
        result[pos++] = '.';
    }
    result[pos] = '\0';
    return pos;
}

static inline bool path_str_posix_basename_span(const char* path,
                                                int* start_out,
                                                int* len_out) {
    if (start_out) *start_out = 0;
    if (len_out) *len_out = 0;
    if (!path || !*path) return false;

    int path_len = (int)strlen(path);
    int end = path_len - 1;
    while (end >= 0 && path[end] == '/') end--;
    if (end < 0) {
        if (len_out) *len_out = 1;
        return true;
    }

    int start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    if (start_out) *start_out = start;
    if (len_out) *len_out = end - start + 1;
    return true;
}

static inline bool path_str_posix_dirname_span(const char* path,
                                               int* len_out) {
    if (len_out) *len_out = 0;
    if (!path || !*path) return false;

    int len = (int)strlen(path);
    bool has_root = path[0] == '/';
    int root_end = 0;
    if (has_root) {
        root_end = 1;
        if (len > 1 && path[1] == '/' && (len == 2 || path[2] != '/')) {
            root_end = 2;
        }
    }

    int end = len - 1;
    while (end > root_end - 1 && path[end] == '/') end--;
    if (end < root_end) {
        if (len_out) *len_out = root_end > 0 ? root_end : 1;
        return true;
    }

    int slash = end;
    while (slash >= root_end && path[slash] != '/') slash--;
    if (slash < root_end) {
        if (has_root) {
            if (len_out) *len_out = root_end;
            return true;
        }
        return false;
    }

    while (slash > root_end - 1 && path[slash] == '/') slash--;
    if (slash < root_end) {
        if (len_out) *len_out = root_end;
        return true;
    }

    if (len_out) *len_out = slash + 1;
    return true;
}

static inline bool path_str_posix_extname_span(const char* path,
                                               int* start_out,
                                               int* len_out) {
    if (start_out) *start_out = 0;
    if (len_out) *len_out = 0;

    int base_start = 0;
    int base_len = 0;
    if (!path_str_posix_basename_span(path, &base_start, &base_len)) return false;
    if (base_len <= 0 || (base_len == 1 && path[base_start] == '/')) return false;

    const char* base = path + base_start;
    bool all_dots = true;
    for (int i = 0; i < base_len; i++) {
        if (base[i] != '.') {
            all_dots = false;
            break;
        }
    }
    if (all_dots) {
        if (base_len <= 2) return false;
        if (start_out) *start_out = base_start + base_len - 1;
        if (len_out) *len_out = 1;
        return true;
    }

    int last_dot = -1;
    for (int i = base_len - 1; i >= 0; i--) {
        if (base[i] == '.') {
            last_dot = i;
            break;
        }
    }
    if (last_dot <= 0) return false;

    if (start_out) *start_out = base_start + last_dot;
    if (len_out) *len_out = base_len - last_dot;
    return true;
}

static inline int path_str_relative_posix(const char* from_abs,
                                          const char* to_abs,
                                          char* result,
                                          int result_size) {
    if (!result || result_size <= 0) return 0;
    result[0] = '\0';
    if (!from_abs || !to_abs) return 0;

    int common = 0;
    int last_sep = 0;
    int fl = (int)strlen(from_abs);
    int tl = (int)strlen(to_abs);
    int minl = fl < tl ? fl : tl;

    for (int i = 0; i < minl; i++) {
        if (from_abs[i] != to_abs[i]) break;
        if (from_abs[i] == '/') last_sep = i;
        common = i + 1;
    }
    if (common == minl) {
        if (fl == tl) return 0;
        if (common < fl && from_abs[common] == '/') {
            last_sep = common;
        } else if (common < tl && to_abs[common] == '/') {
            last_sep = common;
        } else {
            last_sep = common;
        }
    }

    int up_count = 0;
    for (int i = last_sep + 1; i < fl; i++) {
        if (from_abs[i] == '/') up_count++;
    }
    if (last_sep + 1 < fl) up_count++;

    int pos = 0;
    for (int i = 0; i < up_count && pos < result_size - 1; i++) {
        if (i > 0 && pos < result_size - 1) result[pos++] = '/';
        if (pos < result_size - 1) result[pos++] = '.';
        if (pos < result_size - 1) result[pos++] = '.';
    }

    const char* to_rest = to_abs + last_sep;
    if (to_rest[0] == '/') to_rest++;
    int rest_len = (int)strlen(to_rest);
    if (rest_len > 0) {
        if (pos > 0 && pos < result_size - 1) result[pos++] = '/';
        if (pos + rest_len >= result_size) rest_len = result_size - 1 - pos;
        if (rest_len > 0) {
            memcpy(result + pos, to_rest, (size_t)rest_len);
            pos += rest_len;
        }
    }
    result[pos] = '\0';
    return pos;
}

static inline int path_str_win32_root_end(const char* path) {
    if (!path) return 0;
    int path_len = (int)strlen(path);
    if (path_len >= 2 && path_str_is_drive_letter(path[0]) && path[1] == ':') {
        if (path_len > 2 && path_str_is_win32_separator(path[2])) return 3;
        return 2;
    }
    if (path_len >= 1 && path_str_is_win32_separator(path[0])) {
        if (path_len >= 2 && path_str_is_win32_separator(path[1])) return 2;
        return 1;
    }
    return 0;
}

static inline bool path_str_win32_dirname_span(const char* path,
                                               int* len_out) {
    if (len_out) *len_out = 0;
    if (!path || !*path) return false;

    int path_len = (int)strlen(path);
    int root_end = path_str_win32_root_end(path);
    int end = path_len - 1;
    while (end >= root_end && path_str_is_win32_separator(path[end])) end--;
    if (end < root_end) {
        if (root_end > 0) {
            if (len_out) *len_out = root_end;
            return true;
        }
        return false;
    }

    int slash = end;
    while (slash >= root_end && !path_str_is_win32_separator(path[slash])) slash--;
    if (slash < root_end) {
        if (root_end > 0) {
            if (len_out) *len_out = root_end;
            return true;
        }
        return false;
    }

    while (slash > root_end - 1 && path_str_is_win32_separator(path[slash])) slash--;
    int dir_len = slash + 1;
    if (dir_len < root_end) dir_len = root_end;
    if (len_out) *len_out = dir_len;
    return true;
}

static inline bool path_str_win32_basename_span(const char* path,
                                                int* start_out,
                                                int* len_out) {
    if (start_out) *start_out = 0;
    if (len_out) *len_out = 0;
    if (!path || !*path) return false;

    int path_len = (int)strlen(path);
    int root_end = path_str_win32_root_end(path);
    int end = path_len - 1;
    while (end >= root_end && path_str_is_win32_separator(path[end])) end--;
    if (end < root_end) return false;

    int start = end;
    while (start > root_end && !path_str_is_win32_separator(path[start - 1])) start--;
    if (start < root_end) start = root_end;

    if (start_out) *start_out = start;
    if (len_out) *len_out = end - start + 1;
    return true;
}

static inline bool path_str_win32_extname_span(const char* path,
                                               int* start_out,
                                               int* len_out) {
    if (start_out) *start_out = 0;
    if (len_out) *len_out = 0;

    int base_start = 0;
    int base_len = 0;
    if (!path_str_win32_basename_span(path, &base_start, &base_len)) return false;
    const char* base = path + base_start;

    int last_dot = -1;
    for (int i = base_len - 1; i >= 0; i--) {
        if (base[i] == '.') {
            last_dot = i;
            break;
        }
    }
    if (last_dot <= 0) return false;

    bool all_dots = true;
    for (int i = 0; i < last_dot; i++) {
        if (base[i] != '.') {
            all_dots = false;
            break;
        }
    }
    if (all_dots) return false;

    if (start_out) *start_out = base_start + last_dot;
    if (len_out) *len_out = base_len - last_dot;
    return true;
}

static inline int path_str_normalize_lexical_win32(const char* path,
                                                   char* result,
                                                   int result_size,
                                                   bool empty_relative_dot) {
    if (!result || result_size <= 0) return 0;
    result[0] = '\0';
    if (!path || path[0] == '\0') {
        if (empty_relative_dot && result_size > 1) {
            result[0] = '.';
            result[1] = '\0';
            return 1;
        }
        return 0;
    }

    int plen = (int)strlen(path);
    const char* segments[256];
    int seg_count = 0;
    bool is_absolute = false;
    int prefix_len = 0;
    char prefix[16] = {0};

    if (plen >= 2 && path_str_is_drive_letter(path[0]) && path[1] == ':') {
        prefix[0] = path[0];
        prefix[1] = ':';
        prefix_len = 2;
        if (plen >= 3 && path_str_is_win32_separator(path[2])) {
            prefix[2] = '\\';
            prefix_len = 3;
            is_absolute = true;
        }
    } else if (plen >= 2 && path_str_is_win32_separator(path[0]) &&
               path_str_is_win32_separator(path[1])) {
        prefix[0] = '\\';
        prefix[1] = '\\';
        prefix_len = 2;
        is_absolute = true;
    } else if (path_str_is_win32_separator(path[0])) {
        prefix[0] = '\\';
        prefix_len = 1;
        is_absolute = true;
    }

    char temp[4096];
    int remaining_start = prefix_len;
    int rlen = plen - remaining_start;
    if (rlen >= (int)sizeof(temp)) rlen = (int)sizeof(temp) - 1;
    memcpy(temp, path + remaining_start, (size_t)rlen);
    temp[rlen] = '\0';

    char* p = temp;
    while (*p) {
        while (*p && path_str_is_win32_separator(*p)) p++;
        if (!*p) break;
        char* seg_start = p;
        while (*p && !path_str_is_win32_separator(*p)) p++;
        if (*p) {
            *p = '\0';
            p++;
        }

        if (strcmp(seg_start, ".") == 0) continue;
        if (strcmp(seg_start, "..") == 0) {
            if (seg_count > 0 && strcmp(segments[seg_count - 1], "..") != 0) {
                seg_count--;
            } else if (!is_absolute && seg_count < 256) {
                segments[seg_count++] = "..";
            }
        } else if (seg_count < 256) {
            segments[seg_count++] = seg_start;
        }
    }

    int pos = 0;
    for (int i = 0; i < prefix_len && pos < result_size - 1; i++) {
        result[pos++] = prefix[i];
    }
    for (int i = 0; i < seg_count && pos < result_size - 1; i++) {
        if (i > 0 && pos < result_size - 1) result[pos++] = '\\';
        int slen = (int)strlen(segments[i]);
        if (pos + slen >= result_size) slen = result_size - 1 - pos;
        if (slen > 0) {
            memcpy(result + pos, segments[i], (size_t)slen);
            pos += slen;
        }
    }
    if (pos == 0 && empty_relative_dot && result_size > 1) {
        result[pos++] = '.';
    }
    result[pos] = '\0';
    return pos;
}
