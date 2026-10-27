#pragma once

struct UiContext;
struct RadiantDevtools;

RadiantDevtools* radiant_devtools_open(UiContext* ui, int fd, const char* source_path);
bool radiant_devtools_tick(RadiantDevtools* inspector);
void radiant_devtools_destroy(RadiantDevtools* inspector);
int view_doc_with_devtools(const char* path, int fd, bool headless);
