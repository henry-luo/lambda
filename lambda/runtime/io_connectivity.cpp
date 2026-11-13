// Task-owned host mechanisms; discovery, admission and reconnect policy stay in scripts.
#include "io_file_search.hpp"
#include "io_resource.hpp"
#include "concurrency.h"
#include "lambda-number-runtime.hpp"
#include "re2_wrapper.hpp"
#include "lib/memtrack.h"
#include "lib/log.h"
#include "lib/time_util.h"
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#if __has_include(<rtc/rtc.h>)
#define LAMBDA_HAVE_RTC 1
#define RTC_STATIC
#include <rtc/rtc.h>
#endif
#ifdef __APPLE__
#include <dns_sd.h>
#include <arpa/inet.h>
#endif

extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);
static char connectivity_brand;
static const Item ItemTrue = {.item=b2it(BOOL_TRUE)};
static const Item ItemFalse = {.item=b2it(BOOL_FALSE)};
static const int FRAME_MAX = 262144;
#ifdef LAMBDA_HAVE_RTC
static const int QUEUE_MAX = 2097152;
#endif
struct PeerMessage { PeerMessage* next; int size; char bytes[1]; };
struct Connectivity {
    Item handle;
    int pc, dc;
    int pc_state;
    bool closed, released, rooted, reading, writing, failed, gathered, described;
    unsigned active;
    pthread_mutex_t mutex;
    PeerMessage *first, *last;
    int queued, messages;
    int64_t sent_bytes, received_bytes;
#ifdef __APPLE__
    DNSServiceRef service;
    LambdaSocketWaiter* waiter;
    bool registered;
    DNSServiceErrorType dns_error;
#endif
};
static Connectivity* connection(Item h) {
    return get_type_id(h) == LMD_TYPE_VMAP && h.vmap && h.vmap->host_type == &connectivity_brand
        ? (Connectivity*)h.vmap->host_data : nullptr;
}
static void dispose(Connectivity* s) {
#ifdef LAMBDA_HAVE_RTC
    // deletion joins library callbacks before their native state can be reclaimed.
    if (s->dc >= 0) { rtcDeleteDataChannel(s->dc); s->dc = -1; }
    if (s->pc >= 0) { rtcDeletePeerConnection(s->pc); s->pc = -1; }
#endif
#ifdef __APPLE__
    if (s->waiter) { lambda_socket_waiter_close(s->waiter); s->waiter = nullptr; }
    if (s->service) { DNSServiceRefDeallocate(s->service); s->service = nullptr; }
#endif
    while (s->first) { PeerMessage* next = s->first->next; mem_free(s->first); s->first = next; }
    s->last = nullptr;
}
static void free_connection(Connectivity* s) {
    dispose(s); pthread_mutex_destroy(&s->mutex); mem_free(s);
}
static void invalidate(Connectivity* s) {
    if (s->closed) return;
    s->closed = true;
    if (s->handle.vmap) { s->handle.vmap->host_data = nullptr; s->handle.vmap->data = nullptr; }
    if (s->rooted) heap_unregister_gc_root(&s->handle.item);
    s->rooted = false; s->handle = ItemNull;
    dispose(s);
}
static void release(void* data) {
    Connectivity* s = (Connectivity*)data;
    invalidate(s); s->released = true;
    if (!s->active) { free_connection(s); }
}
static void owner_done(LambdaTask*, Item, void*) {}
static VMapVtable carrier = {io_resource_get,io_resource_set,io_resource_count,io_resource_keys,
    io_resource_at,io_resource_at,io_resource_destroy,io_resource_trace};
static Connectivity* allocate() {
    Connectivity* s = (Connectivity*)mem_calloc(1,sizeof(Connectivity),MEM_CAT_NETWORK);
    if (s) { s->pc = s->dc = -1; s->handle = ItemNull; pthread_mutex_init(&s->mutex,nullptr); }
    return s;
}
static Item publish(Connectivity* s) {
    LambdaTask* task = lambda_task_running();
    if (!task || !lambda_task_on_complete(task,owner_done,s,release)) {
        free_connection(s);
        return io_fs_error(ERR_INVALID_STATE,"io.connectivity: a running procedure is required");
    }
    VMap* h = io_resource_handle_new(&carrier,&connectivity_brand,s);
    if (!h) { s->closed = true; dispose(s); return ItemError; }
    s->handle = (Item){.vmap=h}; heap_register_gc_root(&s->handle.item); s->rooted = true;
    return s->handle;
}
struct ActiveConnection {
    Connectivity* s;
    explicit ActiveConnection(Connectivity* value): s(value) { if (s) s->active++; }
    ~ActiveConnection() { if (s && !--s->active && s->released) { free_connection(s); } }
};
static bool text(Item value, size_t limit) {
    return is_text_type_id(get_type_id(value)) && value.get_len() > 0 && value.get_len() <= limit &&
        !memchr(value.get_chars(),0,value.get_len());
}
static Item failure(const char* reason) { return io_fs_error(ERR_NETWORK_ERROR,"io.connectivity: %s",reason); }
static bool timeout(Item value, int64_t* ms) {
    if (get_type_id(value) != LMD_TYPE_INT) return false;
    *ms = it2i(value); return *ms >= 1 && *ms <= 60000;
}
static Item pause_until(uint64_t deadline, const char* operation) {
    uint64_t now = time_now_ms();
    if (now >= deadline) return io_fs_error(ERR_NETWORK_TIMEOUT,"io.connectivity.%s: deadline exceeded",operation);
    return pn_sleep((Item){.item=i2it((int64_t)((deadline-now) < 10 ? deadline-now : 10))});
}
extern "C" Item pn_io_rtc_capabilities() {
    static const char* names[] = {"rtc","bonjour"};
    static const TypeId types[] = {LMD_TYPE_BOOL,LMD_TYPE_BOOL};
    RootFrame roots(1); Rooted<Map*> result(roots,map_alloc_for_type(runtime_result_shape(names,types,2),0));
    if (!result.get()) return ItemError;
    Item values[] = {
#ifdef LAMBDA_HAVE_RTC
        ItemTrue,
#else
        ItemFalse,
#endif
#ifdef __APPLE__
        ItemTrue
#else
        ItemFalse
#endif
    };
    map_fill_items(result.get(),values,2); return (Item){.map=result.get()};
}
#ifdef LAMBDA_HAVE_RTC
static bool trace_rtc;
static void rtc_log(rtcLogLevel, const char* message) {
    if (trace_rtc) log_info("io.rtc: %s",message);
    else log_debug("io.rtc: %s",message);
}
static pthread_once_t logger_once = PTHREAD_ONCE_INIT;
static void initialize_logger() {
    trace_rtc=getenv("LAMBDA_RTC_TRACE") != nullptr;
    rtcInitLogger(trace_rtc ? RTC_LOG_DEBUG : RTC_LOG_ERROR,rtc_log);
}
static void changed(int, rtcState state, void* ptr) {
    Connectivity* s=(Connectivity*)ptr;
    pthread_mutex_lock(&s->mutex); s->pc_state=(int)state; pthread_mutex_unlock(&s->mutex);
}
static int connection_state(Connectivity* s) {
    pthread_mutex_lock(&s->mutex); int state=s->pc_state; pthread_mutex_unlock(&s->mutex); return state;
}
static Item channel_pause(Connectivity* s, uint64_t deadline, const char* operation) {
    if (time_now_ms() >= deadline) return io_fs_error(ERR_NETWORK_TIMEOUT,
        "io.connectivity.%s: deadline exceeded (state=%d open=%d buffered=%d)",
        operation,connection_state(s),rtcIsOpen(s->dc),rtcGetBufferedAmount(s->dc));
    return pause_until(deadline,operation);
}
static void gathering(int, rtcGatheringState state, void* ptr) {
    Connectivity* s = (Connectivity*)ptr;
    pthread_mutex_lock(&s->mutex); s->gathered = state == RTC_GATHERING_COMPLETE; pthread_mutex_unlock(&s->mutex);
}
static void message(int, const char* bytes, int size, void* ptr) {
    Connectivity* s = (Connectivity*)ptr;
    pthread_mutex_lock(&s->mutex);
    if (size < 0 || size > FRAME_MAX || s->queued+size > QUEUE_MAX || s->messages >= 64) s->failed = true;
    if (!s->failed) {
        PeerMessage* m = (PeerMessage*)mem_alloc(sizeof(PeerMessage)+(size_t)size,MEM_CAT_NETWORK);
        if (!m) s->failed = true;
        else { m->next = nullptr; m->size = size; memcpy(m->bytes,bytes,(size_t)size);
            if (s->last) s->last->next = m; else s->first = m;
            s->last = m; s->queued += size; s->messages++; s->received_bytes += size; }
    }
    pthread_mutex_unlock(&s->mutex);
}
#endif
extern "C" Item pn_io_rtc_create(Item options) {
    Item error = ItemNull;
    if (!io_fs_allow_unbrokered("io.rtc.create",&error)) return error;
#ifndef LAMBDA_HAVE_RTC
    (void)options; return failure("WebRTC dependency unavailable; run utils/prepare_sync_connectivity.py");
#else
    IoFsOptionMap opts; ArrayList* servers = nullptr; char* bind = nullptr;
    if (!io_fs_options_open(options,"io.rtc.create",SYSPROC_IO_RTC_CREATE,&opts,&error) ||
        !io_fs_option_text_list(&opts,"stun",&servers,&error) || !io_fs_option_text(&opts,"bind",&bind,&error)) {
        io_fs_text_list_free(servers); mem_free(bind); return error;
    }
    bool valid = !servers || servers->length <= 4;
    if (servers) for (int i=0;i<servers->length;i++) {
        const char* url = (const char*)servers->data[i];
        valid = valid && strlen(url) <= 512 && !strncmp(url,"stun:",5) && !strpbrk(url,"\r\n@ ");
    }
    if (!valid) { io_fs_text_list_free(servers); mem_free(bind); return failure("expected up to four STUN URLs"); }
    pthread_once(&logger_once,initialize_logger);
    Connectivity* s = allocate();
    if (!s) { io_fs_text_list_free(servers); mem_free(bind); return ItemError; }
    rtcConfiguration config = {};
    config.iceServers = servers ? (const char**)servers->data : nullptr;
    config.iceServersCount = servers ? servers->length : 0; config.bindAddress = bind;
    config.disableAutoNegotiation = true; config.maxMessageSize = FRAME_MAX;
    s->pc = rtcCreatePeerConnection(&config);
    io_fs_text_list_free(servers); mem_free(bind);
    if (s->pc >= 0) {
        rtcSetUserPointer(s->pc,s); rtcSetGatheringStateChangeCallback(s->pc,gathering);
        rtcSetStateChangeCallback(s->pc,changed);
        rtcDataChannelInit init = {}; init.negotiated = true; init.manualStream = true; init.stream = 0;
        init.protocol = "lambda-peer-lan-p256-1";
        s->dc = rtcCreateDataChannelEx(s->pc,"lambda-sync",&init);
        if (s->dc >= 0) { rtcSetUserPointer(s->dc,s); rtcSetMessageCallback(s->dc,message); }
    }
    if (s->pc < 0 || s->dc < 0) { free_connection(s); return failure("cannot create channel"); }
    return publish(s);
#endif
}
extern "C" Item pn_io_rtc_description(Item handle, Item role, Item wait) {
    RootFrame roots(2); Rooted<Item> held(roots,handle), held_role(roots,role);
    Connectivity* s = connection(handle); int64_t ms;
    if (!s || s->dc < 0 || !text(role,6) || !timeout(wait,&ms)) return failure("invalid description arguments");
#ifndef LAMBDA_HAVE_RTC
    return failure("WebRTC unavailable");
#else
    ActiveConnection active(s);
    char kind[7] = {}; memcpy(kind,role.get_chars(),role.get_len());
    if (strcmp(kind,"offer") && strcmp(kind,"answer")) return failure("expected offer or answer");
    if (s->described || rtcSetLocalDescription(s->pc,kind) < 0) return failure("local description rejected");
    s->described = true;
    uint64_t deadline = time_now_ms()+ms;
    for (;;) {
        if (s->closed) return failure("channel closed");
        pthread_mutex_lock(&s->mutex); bool ready = s->gathered; pthread_mutex_unlock(&s->mutex);
        if (ready) break;
        Item error = pause_until(deadline,"description"); if (get_type_id(error) == LMD_TYPE_ERROR) return error;
    }
    char buffer[60001]; int count = rtcGetLocalDescription(s->pc,buffer,sizeof(buffer));
    return count > 0 && count <= (int)sizeof(buffer) ? io_fs_make_string(buffer,strlen(buffer)) : failure("SDP budget");
#endif
}
extern "C" Item pn_io_rtc_remote(Item handle, Item sdp, Item role) {
    Connectivity* s = connection(handle);
    if (!s || s->dc < 0 || !text(sdp,60000) || !text(role,6)) return failure("invalid remote description");
#ifdef LAMBDA_HAVE_RTC
    char kind[7] = {}; memcpy(kind,role.get_chars(),role.get_len());
    if (strcmp(kind,"offer") && strcmp(kind,"answer")) return failure("expected offer or answer");
    char* copied = mem_dup_n(sdp.get_chars(),sdp.get_len(),MEM_CAT_NETWORK);
    int code = copied ? rtcSetRemoteDescription(s->pc,copied,kind) : -1; mem_free(copied);
    return code < 0 ? failure("remote description rejected") : ItemTrue;
#else
    return failure("WebRTC unavailable");
#endif
}
extern "C" Item pn_io_rtc_receive(Item handle, Item wait) {
    RootFrame roots(3); Rooted<Item> held(roots,handle), bytes(roots,ItemNull); Rooted<Map*> result(roots,nullptr);
    Connectivity* s = connection(handle); int64_t ms;
    if (!s || s->dc < 0 || !timeout(wait,&ms) || s->reading) return failure("invalid/concurrent receive");
    ActiveConnection active(s); s->reading = true;
    struct Reset { bool* flag; ~Reset() { *flag=false; } } reset{&s->reading};
    uint64_t deadline = time_now_ms()+ms;
    for (;;) {
        if (s->closed) return failure("channel closed");
        pthread_mutex_lock(&s->mutex);
        bool failed = s->failed; PeerMessage* m = s->first;
        if (m) { s->first = m->next; if (!s->first) s->last = nullptr; s->queued -= m->size; s->messages--; }
        pthread_mutex_unlock(&s->mutex);
        if (failed) { mem_free(m); return failure("inbound channel budget exceeded"); }
        if (m) {
            Binary* received=heap_binary_from_bytes(m->bytes,m->size); mem_free(m);
            if (!received) return ItemError;
            bytes.set((Item){.item=x2it(received)});
            static const char* names[] = {"type","data"}; static const TypeId types[] = {LMD_TYPE_STRING,LMD_TYPE_BINARY};
            result.set(map_alloc_for_type(runtime_result_shape(names,types,2),0));
            if (!result.get()) return ItemError;
            Item values[] = {io_fs_make_string("binary",6),bytes.get()}; map_fill_items(result.get(),values,2);
            return (Item){.map=result.get()};
        }
#ifdef LAMBDA_HAVE_RTC
        if (rtcIsClosed(s->dc)) return failure("remote channel closed");
#endif
#ifdef LAMBDA_HAVE_RTC
        Item error = channel_pause(s,deadline,"receive");
#else
        Item error = pause_until(deadline,"receive");
#endif
        if (get_type_id(error) == LMD_TYPE_ERROR) return error;
    }
}
extern "C" Item pn_io_rtc_send(Item handle, Item bytes) {
    RootFrame roots(2); Rooted<Item> held(roots,handle), payload(roots,bytes);
    Connectivity* s = connection(handle);
    if (!s || s->dc < 0 || get_type_id(bytes) != LMD_TYPE_BINARY || bytes.get_len() > FRAME_MAX || s->writing)
        return failure("invalid/concurrent send");
#ifdef LAMBDA_HAVE_RTC
    ActiveConnection active(s); s->writing = true;
    struct Reset { bool* flag; ~Reset() { *flag=false; } } reset{&s->writing};
    uint64_t deadline = time_now_ms()+15000;
    while (!s->closed && (!rtcIsOpen(s->dc) || rtcGetBufferedAmount(s->dc) > QUEUE_MAX-FRAME_MAX)) {
        if (rtcIsClosed(s->dc)) return failure("remote channel closed");
        Item error = channel_pause(s,deadline,"send"); if (get_type_id(error) == LMD_TYPE_ERROR) return error;
    }
    if (s->closed || rtcSendMessage(s->dc,(const char*)binary_data(payload.get().get_safe_binary()),(int)payload.get().get_len()) < 0)
        return failure("send failed");
    s->sent_bytes += (int64_t)payload.get().get_len();
    return ItemNull;
#else
    return failure("WebRTC unavailable");
#endif
}
extern "C" Item pn_io_rtc_close(Item handle) {
    Connectivity* s = connection(handle); if (s) invalidate(s); return ItemNull;
}
extern "C" Item pn_io_rtc_status(Item handle) {
    Connectivity* s = connection(handle);
    if (!s || s->dc < 0) return failure("expected an open WebRTC resource");
#ifdef LAMBDA_HAVE_RTC
    pthread_mutex_lock(&s->mutex); bool failed=s->failed || s->pc_state==RTC_FAILED; int queued=s->queued; int64_t received=s->received_bytes; pthread_mutex_unlock(&s->mutex);
    static const char* names[]={"open","closed","failed","queued","buffered","sent_bytes","received_bytes"};
    static const TypeId types[]={LMD_TYPE_BOOL,LMD_TYPE_BOOL,LMD_TYPE_BOOL,LMD_TYPE_INT,LMD_TYPE_INT,LMD_TYPE_INT,LMD_TYPE_INT};
    RootFrame roots(2); Rooted<Item> held(roots,handle);
    Rooted<Map*> result(roots,map_alloc_for_type(runtime_result_shape(names,types,7),0));
    if (!result.get()) return ItemError;
    Item values[]={{.item=b2it(rtcIsOpen(s->dc))},{.item=b2it(rtcIsClosed(s->dc))},
        {.item=b2it(failed)},{.item=i2it(queued)},{.item=i2it(rtcGetBufferedAmount(s->dc))},
        {.item=i2it(s->sent_bytes)},{.item=i2it(received)}};
    map_fill_items(result.get(),values,7); return (Item){.map=result.get()};
#else
    return failure("WebRTC unavailable");
#endif
}

#ifdef __APPLE__
struct Discovered { char name[64], type[64], domain[256], host[256]; uint32_t interface; int port; bool resolved; };
struct Browse { Discovered entries[64]; int count; DNSServiceErrorType error; ArrayList* names; const char* prefix; };
static void browsed(DNSServiceRef, DNSServiceFlags flags, uint32_t interface, DNSServiceErrorType error,
    const char* name, const char* type, const char* domain, void* ptr) {
    Browse* b=(Browse*)ptr; if (error) { b->error=error; return; }
    if (b->names) {
        bool expected=false;
        for (int i=0;i<b->names->length;i++) if (!strcmp((char*)b->names->data[i],name)) { expected=true; break; }
        if (!expected) return;
    }
    if (b->prefix && strncmp(name,b->prefix,strlen(b->prefix))) return;
    if (!(flags&kDNSServiceFlagsAdd) || b->count>=64 || strlen(name)>63 || strlen(type)>63 || strlen(domain)>255) return;
    for (int i=0;i<b->count;i++) if (!strcmp(b->entries[i].name,name) && b->entries[i].interface==interface) return;
    Discovered* d=&b->entries[b->count++];
    str_copy(d->name,sizeof(d->name),name,strlen(name)); str_copy(d->type,sizeof(d->type),type,strlen(type));
    str_copy(d->domain,sizeof(d->domain),domain,strlen(domain)); d->interface=interface;
}
static void resolved(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType error, const char*,
    const char* host, uint16_t port, uint16_t, const unsigned char*, void* ptr) {
    Discovered* d=(Discovered*)ptr; if (error || strlen(host)>255) return;
    str_copy(d->host,sizeof(d->host),host,strlen(host)); d->port=ntohs(port); d->resolved=true;
}
static void registered(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType error, const char*,
    const char*, const char*, void* ptr) {
    Connectivity* s=(Connectivity*)ptr; s->dns_error=error; s->registered=!error;
}
static Item process_dns(DNSServiceRef ref, LambdaSocketWaiter* waiter, uint64_t deadline) {
    Item error=ItemNull;
    uint64_t now=time_now_ms();
    // sample once: crossing the boundary must not turn subtraction into an unbounded wait.
    if (now>=deadline) return ItemFalse;
    int ready=lambda_socket_waiter_wait(waiter,false,(int64_t)(deadline-now),&error);
    if (ready<0) return error;
    if (!ready) return ItemFalse;
    return DNSServiceProcessResult(ref)==kDNSServiceErr_NoError ? ItemTrue : failure("Bonjour processing failed");
}
#endif
extern "C" Item pn_io_discovery_advertise(Item name, Item port) {
    Item error=ItemNull;
    if (!io_fs_allow_unbrokered("io.discovery.advertise",&error)) return error;
    if (!text(name,63) || get_type_id(port)!=LMD_TYPE_INT || it2i(port)<1 || it2i(port)>65535)
        return failure("invalid Bonjour name/port");
#ifdef __APPLE__
    Connectivity* s=allocate(); if (!s) return ItemError;
    char label[64]={}; memcpy(label,name.get_chars(),name.get_len());
    DNSServiceErrorType code=DNSServiceRegister(&s->service,kDNSServiceFlagsNoAutoRename,0,label,
        "_lambda-sync._tcp","local.",nullptr,htons((uint16_t)it2i(port)),0,nullptr,registered,s);
    if (code) { free_connection(s); return failure("Bonjour registration failed"); }
    // NoAutoRename requires a callback; confirm the private name before reporting readiness.
    s->waiter=lambda_socket_waiter_new((uint64_t)DNSServiceRefSockFD(s->service));
    error=s->waiter ? process_dns(s->service,s->waiter,time_now_ms()+3000) : failure("cannot wait for Bonjour registration");
    if (get_type_id(error)==LMD_TYPE_ERROR || !s->registered || s->dns_error) {
        free_connection(s);
        return get_type_id(error)==LMD_TYPE_ERROR ? error : failure("Bonjour registration not confirmed");
    }
    return publish(s);
#else
    return failure("Bonjour is unavailable on this host");
#endif
}
extern "C" Item pn_io_discovery_browse2(Item wait, Item options) {
    RootFrame arguments(2); Rooted<Item> held_wait(arguments,wait), held_options(arguments,options);
    Item error=ItemNull; int64_t ms;
    if (!io_fs_allow_unbrokered("io.discovery.browse",&error)) return error;
    if (!timeout(held_wait.get(),&ms)) return failure("invalid browse deadline");
    struct Filter {
        ArrayList* names=nullptr;
        char* prefix=nullptr;
        ~Filter() { io_fs_text_list_free(names); mem_free(prefix); }
    } filter;
    IoFsOptionMap opts;
    if (!io_fs_options_open(held_options.get(),"io.discovery.browse",SYSPROC_IO_DISCOVERY_BROWSE,&opts,&error) ||
        !io_fs_option_text_list(&opts,"names",&filter.names,&error) ||
        !io_fs_option_text(&opts,"prefix",&filter.prefix,&error)) return error;
    if ((filter.names && (filter.names->length>64 || filter.prefix)) ||
        (filter.prefix && (!*filter.prefix || strlen(filter.prefix)>63))) return failure("invalid browse filter");
    if (filter.names) for (int i=0;i<filter.names->length;i++) {
        size_t n=strlen((char*)filter.names->data[i]);
        if (!n || n>63) return failure("invalid browse name");
    }
#ifdef __APPLE__
    Browse found={}; found.names=filter.names; found.prefix=filter.prefix;
    Connectivity* s=allocate(); if (!s) return ItemError;
    struct Cleanup { Connectivity* s; ~Cleanup() { free_connection(s); } } cleanup{s};
    uint64_t deadline=time_now_ms()+ms;
    if (DNSServiceBrowse(&s->service,0,0,"_lambda-sync._tcp","local.",browsed,&found)) return failure("Bonjour browse failed");
    s->waiter=lambda_socket_waiter_new((uint64_t)DNSServiceRefSockFD(s->service));
    if (!s->waiter) return failure("cannot wait for Bonjour");
    uint64_t browse_deadline=time_now_ms()+ms/2;
    while (time_now_ms()<browse_deadline && found.count<64) {
        error=process_dns(s->service,s->waiter,browse_deadline);
        if (get_type_id(error)==LMD_TYPE_ERROR) return error;
        if (error.item==ItemFalse.item || found.error) break;
        // reserve half the finite budget for service resolution.
        if (found.count && time_now_ms()+ms/2>=deadline) break;
    }
    dispose(s);
    RootFrame roots(4); Rooted<Array*> rows(roots,array_plain()); Rooted<Map*> row(roots,nullptr);
    Rooted<Item> value(roots,ItemNull), host(roots,ItemNull);
    for (int i=0;i<found.count && time_now_ms()<deadline;i++) {
        Discovered* d=&found.entries[i];
        if (DNSServiceResolve(&s->service,0,d->interface,d->name,d->type,d->domain,resolved,d)) continue;
        s->waiter=lambda_socket_waiter_new((uint64_t)DNSServiceRefSockFD(s->service));
        if (!s->waiter) return failure("cannot resolve Bonjour");
        // A stale matching candidate cannot consume every other peer's resolution budget.
        uint64_t now=time_now_ms();
        uint64_t share=now<deadline ? (deadline-now)/(uint64_t)(found.count-i) : 0;
        error=process_dns(s->service,s->waiter,now+(share ? share : 1)); dispose(s);
        if (get_type_id(error)==LMD_TYPE_ERROR) return error;
        if (!d->resolved) continue;
        static const char* names[]={"name","host","port"}; static const TypeId types[]={LMD_TYPE_STRING,LMD_TYPE_STRING,LMD_TYPE_INT};
        row.set(map_alloc_for_type(runtime_result_shape(names,types,3),0)); if (!row.get() || !rows.get()) return ItemError;
        value.set(io_fs_make_string(d->name,strlen(d->name)));
        host.set(io_fs_make_string(d->host,strlen(d->host)));
        Item values[]={value.get(),host.get(),(Item){.item=i2it(d->port)}};
        map_fill_items(row.get(),values,3); array_push_verbatim(rows.get(),(Item){.map=row.get()});
    }
    return (Item){.array=rows.get()};
#else
    return failure("Bonjour is unavailable on this host");
#endif
}
extern "C" Item pn_io_discovery_browse(Item wait) { return pn_io_discovery_browse2(wait,ItemNull); }
