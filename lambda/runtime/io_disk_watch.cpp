// OS notifications are bounded hints; anchored capture and reconciliation stay in script.
#include "io_file_search.hpp"
#include "io_resource.hpp"
#include "concurrency.h"
#include "lambda-number-runtime.hpp"
#include "re2_wrapper.hpp"
#include "lib/uv_loop.h"
#include "lib/utf.h"
#include <string.h>

extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);

namespace {
char watch_brand;
constexpr int WATCH_PATHS = 256;
constexpr int WATCH_DIRECTORIES = 10000;
struct DirectoryWatch;
struct WatchNode {
    uv_fs_event_t event;
    DirectoryWatch* owner;
    char* relative;
};
struct DirectoryWatch {
    Item handle;
    FileAnchor* anchor;
    ArrayList *nodes, *paths;
    uv_timer_t* timer;
    LambdaTask* waiter;
    unsigned references, active;
    int failure;
    bool closed, released, rooted, rescan, refresh;
};
DirectoryWatch* of(Item item) {
    return get_type_id(item) == LMD_TYPE_VMAP && item.vmap && item.vmap->host_type == &watch_brand
        ? (DirectoryWatch*)item.vmap->host_data : nullptr;
}
void clear_paths(DirectoryWatch* s) {
    for (int i = 0; i < s->paths->length; ++i) mem_free(s->paths->data[i]);
    s->paths->length = 0;
}
void free_watch(DirectoryWatch* s) {
    clear_paths(s);
    arraylist_free(s->paths); arraylist_free(s->nodes);
    file_anchor_close(s->anchor); mem_free(s);
}
void maybe_free(DirectoryWatch* s) {
    // uv_close callbacks can outlive the task that owned the native monitor.
    if (s->released && !s->references && !s->active) free_watch(s);
}
void node_closed(uv_handle_t* handle) {
    WatchNode* node = (WatchNode*)handle->data;
    DirectoryWatch* owner = node->owner;
    mem_free(node->relative); mem_free(node); owner->references--; maybe_free(owner);
}
void close_nodes(DirectoryWatch* s) {
    for (int i = 0; i < s->nodes->length; ++i) {
        WatchNode* node = (WatchNode*)s->nodes->data[i];
        uv_fs_event_stop(&node->event);
        uv_close((uv_handle_t*)&node->event,node_closed);
    }
    s->nodes->length = 0;
}
void invalidate(DirectoryWatch* s) {
    if (s->closed) return;
    s->closed = true;
    if (s->handle.vmap) { s->handle.vmap->host_data = nullptr; s->handle.vmap->data = nullptr; }
    if (s->rooted) heap_unregister_gc_root(&s->handle.item);
    s->rooted = false; s->handle = ItemNull;
    close_nodes(s);
    if (s->timer) {
        uv_timer_stop(s->timer);
        uv_close((uv_handle_t*)s->timer, [](uv_handle_t* h) {
            DirectoryWatch* owner = (DirectoryWatch*)h->data;
            mem_free(h); owner->references--; maybe_free(owner);
        });
        s->timer = nullptr;
    }
    if (s->waiter && lambda_task_state(s->waiter) == LAMBDA_TASK_PARKED) lambda_task_resume(s->waiter);
}
void owner_done(LambdaTask*, Item, void*) {}
void release(void* data) {
    DirectoryWatch* s = (DirectoryWatch*)data;
    invalidate(s); s->released = true; maybe_free(s);
}
VMapVtable carrier = {io_resource_get,io_resource_set,io_resource_count,io_resource_keys,
    io_resource_at,io_resource_at,io_resource_destroy,io_resource_trace};
bool relative_path(const char* path) {
    if (!*path || strlen(path) > 4096 || !utf8_valid(path, strlen(path))) return false;
    const char* part = path;
    for (const char* p = path;; ++p) {
        if (*p == '\\') return false;
        if (*p == '/' || !*p) {
            size_t n = (size_t)(p-part);
            if (!n || (n == 1 && part[0] == '.') || (n == 2 && !memcmp(part,"..",2))) return false;
            if (!*p) return true;
            part = p+1;
        }
    }
}
void changed(uv_fs_event_t* event, const char* filename, int events, int status) {
    WatchNode* node = (WatchNode*)event->data;
    DirectoryWatch* s = node->owner;
    if (s->closed) return;
#if !defined(__APPLE__) && !defined(_WIN32)
    // Linux watches each directory; rebuild coverage before requesting a recovery scan.
    if (events & UV_RENAME) { s->refresh = true; s->rescan = true; }
#else
    (void)events;
#endif
    if (status < 0) s->failure = status;
    if (status < 0 || !filename) s->rescan = true;
    else if (!s->rescan) {
        StrBuf* path = strbuf_new();
        if (!path) {
            s->rescan = true;
            clear_paths(s);
            if (s->waiter && lambda_task_state(s->waiter) == LAMBDA_TASK_PARKED) lambda_task_resume(s->waiter);
            return;
        }
        if (*node->relative) { strbuf_append_str(path,node->relative); strbuf_append_char(path,'/'); }
        strbuf_append_str(path,filename);
#if defined(_WIN32)
        // libuv reports Windows separators; script hints use portable relative paths.
        for (char* p = path->str; *p; ++p) if (*p == '\\') *p = '/';
#endif
        if (!relative_path(path->str)) s->rescan = true;
        bool duplicate = false;
        for (int i = 0; i < s->paths->length; ++i)
            if (!strcmp((char*)s->paths->data[i],path->str)) { duplicate = true; break; }
        if (!duplicate && !s->rescan) {
            if (s->paths->length >= WATCH_PATHS) s->rescan = true;
            else {
                char* copy = mem_strdup(path->str,MEM_CAT_EVAL);
                if (!copy || !arraylist_append(s->paths,copy)) { mem_free(copy); s->rescan = true; }
            }
        }
        strbuf_free(path);
    }
    if (s->rescan) clear_paths(s);
    if (s->waiter && lambda_task_state(s->waiter) == LAMBDA_TASK_PARKED) lambda_task_resume(s->waiter);
}
bool add_node(DirectoryWatch* s, const char* relative) {
    if (s->nodes->length >= WATCH_DIRECTORIES) return false;
    FileAnchor* directory = *relative ? file_anchor_directory(s->anchor,relative,false,0) : s->anchor;
    char* path = directory ? file_anchor_path(directory) : nullptr;
    if (directory != s->anchor) file_anchor_close(directory);
    if (!path) return false;
    WatchNode* node = (WatchNode*)mem_calloc(1,sizeof(WatchNode),MEM_CAT_EVAL);
    if (!node) { mem_free(path); return false; }
    node->owner = s; node->relative = mem_strdup(relative,MEM_CAT_EVAL);
    if (!node->relative || uv_fs_event_init(lambda_uv_loop(),&node->event) != 0) {
        mem_free(node->relative); mem_free(node); mem_free(path); return false;
    }
    node->event.data = node;
    s->references++;
    bool appended = arraylist_append(s->nodes,node);
    unsigned flags = 0;
#if defined(__APPLE__) || defined(_WIN32)
    flags = UV_FS_EVENT_RECURSIVE;
#endif
    int status = appended ? uv_fs_event_start(&node->event,changed,path,flags) : UV_ENOMEM;
    mem_free(path);
    if (!appended) {
        uv_close((uv_handle_t*)&node->event,node_closed);
    }
    return status == 0 && file_anchor_validate(s->anchor);
}
#if !defined(__APPLE__) && !defined(_WIN32)
struct Walk { DirectoryWatch* owner; const char* prefix; int depth; };
bool add_tree(DirectoryWatch*, const char*, int);
bool visit_directory(void* data, const char* name, const FileRevision* revision) {
    if (!revision->stat.is_dir) return true;
    Walk* walk = (Walk*)data;
    StrBuf* path = strbuf_new();
    if (!path) return false;
    if (*walk->prefix) { strbuf_append_str(path,walk->prefix); strbuf_append_char(path,'/'); }
    strbuf_append_str(path,name);
    bool ok = add_tree(walk->owner,path->str,walk->depth+1);
    strbuf_free(path); return ok;
}
bool add_tree(DirectoryWatch* s, const char* relative, int depth) {
    if (depth > 128 || !add_node(s,relative)) return false;
    FileAnchor* dir = *relative ? file_anchor_directory(s->anchor,relative,false,0) : s->anchor;
    Walk walk = {s,relative,depth};
    bool ok = dir && file_anchor_visit_strict(dir,WATCH_DIRECTORIES,visit_directory,&walk);
    if (dir != s->anchor) file_anchor_close(dir);
    return ok;
}
#else
bool add_tree(DirectoryWatch* s, const char* relative, int) { return add_node(s,relative); }
#endif
}

extern "C" Item pn_io_disk_watch(Item root) {
    Item error = ItemNull;
    if (!io_fs_allow_unbrokered("io.disk.watch",&error)) return error;
    LambdaTask* task = lambda_task_running();
    if (!task || (!lambda_uv_loop() && lambda_uv_init() != 0))
        return io_fs_error(ERR_INVALID_STATE,"io.disk.watch: running procedure required");
    StrBuf* path = lambda_get_local_path_from_item(root);
    FileAnchorOptions options = {0,io_resource_file_guard,task,nullptr,nullptr};
    FileAnchor* anchor = path ? file_anchor_open(path->str,&options) : nullptr;
    strbuf_free(path);
    if (!anchor) return io_fs_error(ERR_IO_ERROR,"io.disk.watch: cannot acquire root");
    DirectoryWatch* s = (DirectoryWatch*)mem_calloc(1,sizeof(DirectoryWatch),MEM_CAT_EVAL);
    if (!s) { file_anchor_close(anchor); return ItemError; }
    s->handle = ItemNull; s->anchor = anchor;
    s->nodes = arraylist_new(8); s->paths = arraylist_new(16);
    if (!s->nodes || !s->paths) {
        if (s->paths) clear_paths(s);
        arraylist_free(s->paths); arraylist_free(s->nodes);
        file_anchor_close(anchor); mem_free(s); return ItemError;
    }
    if (!lambda_task_on_complete(task,owner_done,s,release)) { free_watch(s); return ItemError; }
    if (!add_tree(s,"",0)) { invalidate(s); return io_fs_error(ERR_IO_ERROR,"io.disk.watch: cannot monitor subtree"); }
    s->timer = (uv_timer_t*)mem_calloc(1,sizeof(uv_timer_t),MEM_CAT_EVAL);
    if (!s->timer || uv_timer_init(lambda_uv_loop(),s->timer) != 0) {
        mem_free(s->timer); s->timer = nullptr; invalidate(s); return ItemError;
    }
    s->timer->data = s; s->references++;
    VMap* handle = io_resource_handle_new(&carrier,&watch_brand,s);
    if (!handle) { invalidate(s); return ItemError; }
    s->handle = (Item){.vmap=handle}; heap_register_gc_root(&s->handle.item); s->rooted = true;
    return s->handle;
}

extern "C" Item pn_io_disk_changes(Item handle, Item timeout) {
    DirectoryWatch* s = of(handle);
    if (!s || s->closed || s->waiter || get_type_id(timeout) != LMD_TYPE_INT || it2i(timeout) < 0 || it2i(timeout) > 60000)
        return io_fs_error(ERR_INVALID_OPERATION,"io.disk.changes: open monitor and timeout 0..60000 required");
    LambdaTask* task = lambda_task_running();
    if (!task) return io_fs_error(ERR_INVALID_STATE,"io.disk.changes: running procedure required");
    s->active++;
    Item resumed = ItemNull;
    if (!s->rescan && !s->paths->length && it2i(timeout) > 0) {
        s->waiter = task;
        int status = uv_timer_start(s->timer,[](uv_timer_t* timer) {
            DirectoryWatch* s = (DirectoryWatch*)timer->data;
            if (s->waiter && lambda_task_state(s->waiter) == LAMBDA_TASK_PARKED) lambda_task_resume(s->waiter);
        },(uint64_t)it2i(timeout),0);
        if (status < 0) {
            s->waiter = nullptr; s->active--;
            return io_fs_error(ERR_IO_ERROR,"io.disk.changes: cannot start wait timer (%s)",uv_strerror(status));
        }
        lambda_task_park(task); resumed = lambda_task_suspend(task);
        s->waiter = nullptr;
        if (s->timer) uv_timer_stop(s->timer);
    }
    if (get_type_id(resumed) == LMD_TYPE_ERROR || s->closed || lambda_task_cancel_requested(task)) {
        s->active--; maybe_free(s);
        return get_type_id(resumed) == LMD_TYPE_ERROR ? resumed : io_fs_error(ERR_CANCELLED,"io.disk.changes: monitor closed or task cancelled");
    }
    if (s->failure) {
        int status = s->failure;
        s->active--; invalidate(s);
        return io_fs_error(ERR_IO_ERROR,"io.disk.changes: native monitor failed (%s)",uv_strerror(status));
    }
    if (s->refresh) {
        close_nodes(s); s->refresh = false;
        if (!add_tree(s,"",0)) { s->active--; invalidate(s); return io_fs_error(ERR_IO_ERROR,"io.disk.changes: subtree coverage lost"); }
    }
    if (!file_anchor_validate(s->anchor)) s->rescan = true;
    RootFrame roots(3);
    Rooted<Array*> paths(roots,array_plain()); Rooted<Item> path(roots,ItemNull);
    if (!paths.get()) { s->active--; return ItemError; }
    for (int i = 0; i < s->paths->length; ++i) {
        path.set(io_fs_make_string((char*)s->paths->data[i],strlen((char*)s->paths->data[i])));
        array_push_verbatim(paths.get(),path.get());
    }
    static const char* names[] = {"paths","rescan"};
    static const TypeId types[] = {LMD_TYPE_ARRAY,LMD_TYPE_BOOL};
    Rooted<Map*> result(roots,map_alloc_for_type(runtime_result_shape(names,types,2),0));
    if (!result.get()) { s->active--; return ItemError; }
    Item values[] = {(Item){.array=paths.get()},(Item){.item=b2it(s->rescan)}};
    map_fill_items(result.get(),values,2);
    clear_paths(s); s->rescan = false; s->active--;
    return (Item){.map=result.get()};
}

extern "C" Item pn_io_disk_unwatch(Item handle) {
    DirectoryWatch* s = of(handle);
    if (s) invalidate(s);
    return ItemNull;
}
