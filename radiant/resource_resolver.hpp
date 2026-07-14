#ifndef RADIANT_RESOURCE_RESOLVER_HPP
#define RADIANT_RESOURCE_RESOLVER_HPP

#include <stddef.h>

bool radiant_resolve_shared_data_resource_path(const char* href, const char* base_path,
                                               char* out_path, size_t out_size);

#endif
