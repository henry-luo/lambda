// lib/hashmap_helpers.c - see hashmap_helpers.h
//
// Implementations for the generic offset-based callbacks. The macro variants
// stay header-only so the compiler can inline them per call site.

#include "hashmap_helpers.h"

#include <string.h>

uint64_t hashmap_hash_cstrptr_at(const void* item, size_t off, uint64_t s0, uint64_t s1) {
    const char* k = *(const char* const*)((const char*)item + off);
    return hashmap_sip(k, strlen(k), s0, s1);
}

int hashmap_cmp_cstrptr_at(const void* a, const void* b, size_t off) {
    const char* ka = *(const char* const*)((const char*)a + off);
    const char* kb = *(const char* const*)((const char*)b + off);
    return strcmp(ka, kb);
}

uint64_t hashmap_hash_cstr_at(const void* item, size_t off, uint64_t s0, uint64_t s1) {
    const char* k = (const char*)item + off;
    return hashmap_sip(k, strlen(k), s0, s1);
}

int hashmap_cmp_cstr_at(const void* a, const void* b, size_t off) {
    return strcmp((const char*)a + off, (const char*)b + off);
}

uint64_t hashmap_hash_ptr_at(const void* item, size_t off, uint64_t s0, uint64_t s1) {
    uintptr_t p = *(const uintptr_t*)((const char*)item + off);
    return hashmap_sip(&p, sizeof(p), s0, s1);
}

int hashmap_cmp_ptr_at(const void* a, const void* b, size_t off) {
    uintptr_t pa = *(const uintptr_t*)((const char*)a + off);
    uintptr_t pb = *(const uintptr_t*)((const char*)b + off);
    if (pa == pb) return 0;
    return pa < pb ? -1 : 1;
}

uint64_t hashmap_hash_int_at(const void* item, size_t off, uint64_t s0, uint64_t s1) {
    int v = *(const int*)((const char*)item + off);
    return hashmap_sip(&v, sizeof(v), s0, s1);
}

int hashmap_cmp_int_at(const void* a, const void* b, size_t off) {
    int va = *(const int*)((const char*)a + off);
    int vb = *(const int*)((const char*)b + off);
    return (va > vb) - (va < vb);
}
