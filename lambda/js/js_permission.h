#pragma once

#include "../lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void js_permission_init_from_argv(int argc, const char** argv);
void js_permission_reset(void);
int js_permission_enabled(void);
int js_permission_has_net(void);

Item js_process_permission_has(Item scope_item, Item resource_item);
Item js_process_permission_drop(Item scope_item, Item resource_item);

int js_permission_has_fs_read(const char* path);
int js_permission_has_fs_write(const char* path);
int js_permission_has_full_fs_read(void);
int js_permission_has_full_fs_write(void);

Item js_permission_make_fs_error(const char* permission, const char* resource, const char* message);
Item js_permission_make_net_error(const char* syscall, const char* resource);
Item js_permission_throw_fs_error(const char* permission, const char* resource, const char* message);
Item js_permission_check_fs_read(const char* path);
Item js_permission_check_fs_write(const char* path);

#ifdef __cplusplus
}
#endif
