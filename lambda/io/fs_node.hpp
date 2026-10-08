#pragma once
#include "../lambda-data.hpp"
#include "zip_archive.hpp"

// Shared filesystem element contract (S12.4.1v2, S14.3.1v2, D7.4.5v2).
extern thread_local const ZipLimits* input_zip_limits;
void input_zip(Input* input, const void* bytes, size_t length,
    ZipBudget* parent_budget = nullptr, uint32_t depth = 0);
bool fs_node_is(Item item);
Item fs_node_content(Item node);
Item fs_node_input(Item node, String* type, String* flavor);
ZipArchive* fs_node_archive(Item node);
bool fs_zip_encode(Item tree, const ZipWriteOptions* options,
    StrBuf* bytes, ZipError* error);
bool fs_zip_options(Item options, ZipLimits* limits, ZipWriteOptions* write,
    ZipError* error);

struct InputZipLimitsScope {
    const ZipLimits* previous;
    explicit InputZipLimitsScope(const ZipLimits* limits) : previous(input_zip_limits) { input_zip_limits = limits; }
    ~InputZipLimitsScope() { input_zip_limits = previous; }
};
