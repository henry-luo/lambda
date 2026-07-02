// curl_multi_backend.cpp
// Dedicated curl multi network thread for Radiant HTTP resource transfers.

#include "curl_multi_backend.h"
#include "network_resource_manager.h"
#include "cookie_jar.h"
#include "enhanced_file_cache.h"
#include "../../lib/arraylist.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define CURL_MULTI_POLL_TIMEOUT_MS 100
#define NETWORK_MAX_RESOURCE_SIZE (100 * 1024 * 1024)

typedef struct CurlMultiRequest {
    struct NetworkResource* resource;
    void* request_data;
} CurlMultiRequest;

typedef struct HeaderCallbackCtx {
    CookieJar* jar;
    const char* request_url;
} HeaderCallbackCtx;

typedef struct CurlMultiTransfer {
    struct NetworkResource* resource;
    void* request_data;
    CURL* easy;
    struct curl_slist* custom_headers;
    HeaderCallbackCtx* header_ctx;
    char* data;
    size_t size;
} CurlMultiTransfer;

struct CurlMultiBackend {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    CURLM* multi;
    ArrayList* pending;
    int active_count;
    bool shutdown_flag;
    bool stop_thread;
    bool thread_started;
    CurlMultiCompletionCallback callback;
    void* callback_data;
};

static bool curl_multi_global_initialized = false;
static pthread_mutex_t curl_multi_global_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool init_curl_multi_global(void) {
    pthread_mutex_lock(&curl_multi_global_mutex);
    if (!curl_multi_global_initialized) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            pthread_mutex_unlock(&curl_multi_global_mutex);
            log_error("curl-multi: failed to initialize libcurl");
            return false;
        }
        curl_multi_global_initialized = true;
    }
    pthread_mutex_unlock(&curl_multi_global_mutex);
    return true;
}

static size_t write_response_callback(void* contents,
                                      size_t size,
                                      size_t nmemb,
                                      void* userdata) {
    CurlMultiTransfer* transfer = (CurlMultiTransfer*)userdata;
    if (!transfer) return 0;

    size_t total_size = size * nmemb;
    if (transfer->size + total_size > NETWORK_MAX_RESOURCE_SIZE) {
        log_error("curl-multi: resource exceeds maximum size (%d MB)",
                  NETWORK_MAX_RESOURCE_SIZE / (1024 * 1024));
        return 0;
    }

    char* new_data = (char*)mem_realloc(transfer->data,
                                        transfer->size + total_size + 1,
                                        MEM_CAT_NETWORK);
    if (!new_data) {
        log_error("curl-multi: memory allocation failed during download");
        return 0;
    }

    transfer->data = new_data;
    memcpy(&transfer->data[transfer->size], contents, total_size);
    transfer->size += total_size;
    transfer->data[transfer->size] = '\0';
    return total_size;
}

static int transfer_progress_callback(void* clientp,
                                      curl_off_t dltotal,
                                      curl_off_t dlnow,
                                      curl_off_t ultotal,
                                      curl_off_t ulnow) {
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;

    CurlMultiTransfer* transfer = (CurlMultiTransfer*)clientp;
    if (transfer && transfer->resource &&
        atomic_load(&transfer->resource->cancel_requested)) {
        log_debug("curl-multi: aborting cancelled transfer: %s",
                  transfer->resource->url);
        return 1;
    }
    return 0;
}

static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    HeaderCallbackCtx* ctx = (HeaderCallbackCtx*)userdata;
    if (!ctx || !ctx->jar) return total;

    if (total > 12 && strncasecmp(buffer, "Set-Cookie:", 11) == 0) {
        size_t len = total;
        while (len > 0 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n')) {
            len--;
        }
        char* header_str = (char*)mem_alloc(len + 1, MEM_CAT_NETWORK);
        if (!header_str) return total;
        memcpy(header_str, buffer, len);
        header_str[len] = '\0';
        cookie_jar_store(ctx->jar, ctx->request_url, header_str);
        mem_free(header_str);
    }
    return total;
}

static void set_resource_error(NetworkResource* res, const char* message) {
    if (!res) return;
    if (res->error_message) mem_free(res->error_message);
    res->error_message = mem_strdup(message ? message : "Unknown network error", MEM_CAT_NETWORK);
}

static void set_curl_error(NetworkResource* res, CURLcode code) {
    const char* error_str = curl_easy_strerror(code);
    char error_msg[256];

    switch (code) {
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
            snprintf(error_msg, sizeof(error_msg), "SSL certificate error: %s", error_str);
            log_error("curl-multi: SSL/TLS certificate error for %s: %s",
                      res->url, error_str);
            break;
        case CURLE_TOO_MANY_REDIRECTS:
            snprintf(error_msg, sizeof(error_msg), "Redirect loop (max 5 redirects exceeded)");
            log_error("curl-multi: redirect loop detected for %s", res->url);
            break;
        case CURLE_OPERATION_TIMEDOUT:
            snprintf(error_msg, sizeof(error_msg), "Request timed out");
            log_error("curl-multi: request timed out for %s", res->url);
            break;
        case CURLE_ABORTED_BY_CALLBACK:
            snprintf(error_msg, sizeof(error_msg), "Cancelled");
            log_debug("curl-multi: transfer cancelled for %s", res->url);
            break;
        case CURLE_COULDNT_RESOLVE_HOST:
            snprintf(error_msg, sizeof(error_msg), "Could not resolve host");
            log_error("curl-multi: could not resolve host for %s", res->url);
            break;
        case CURLE_COULDNT_CONNECT:
            snprintf(error_msg, sizeof(error_msg), "Connection refused");
            log_error("curl-multi: connection refused for %s", res->url);
            break;
        default:
            snprintf(error_msg, sizeof(error_msg), "%s", error_str);
            log_error("curl-multi: download failed for %s: %s", res->url, error_str);
            break;
    }
    set_resource_error(res, error_msg);
}

static bool curl_resource_failure_is_optional(NetworkResource* res) {
    if (!res) return false;
    return res->type == RESOURCE_CSS || res->type == RESOURCE_IMAGE ||
           res->type == RESOURCE_FONT || res->type == RESOURCE_SVG ||
           res->type == RESOURCE_SCRIPT;
}

static bool persist_response(CurlMultiTransfer* transfer) {
    NetworkResource* res = transfer->resource;
    if (!res) return false;

    if (res->cache) {
        char* cached_path = enhanced_cache_try_store(res->cache,
                                                     res->url,
                                                     transfer->data,
                                                     transfer->size,
                                                     NULL);
        if (cached_path) {
            if (res->local_path) mem_free(res->local_path);
            res->local_path = cached_path;
        }
    }

    if (!res->local_path) {
        char temp_path[512];
        snprintf(temp_path, sizeof(temp_path), "./temp/download_%p.tmp", (void*)res);

        FILE* f = fopen(temp_path, "wb");
        if (!f) {
            log_error("curl-multi: failed to write temporary file: %s", temp_path);
            set_resource_error(res, "Failed to write downloaded resource");
            return false;
        }
        fwrite(transfer->data, 1, transfer->size, f);
        fclose(f);
        res->local_path = mem_strdup(temp_path, MEM_CAT_NETWORK);
        log_debug("curl-multi: saved to temporary file: %s", temp_path);
    }

    return true;
}

static bool finish_transfer(CurlMultiTransfer* transfer, CURLcode result) {
    NetworkResource* res = transfer ? transfer->resource : NULL;
    if (!res) return false;

    bool success = false;
    if (result != CURLE_OK) {
        set_curl_error(res, result);
    } else {
        long http_code = 0;
        curl_easy_getinfo(transfer->easy, CURLINFO_RESPONSE_CODE, &http_code);
        res->http_status_code = (int)http_code;

        if (http_code >= 400) {
            char error_msg[128];
            snprintf(error_msg, sizeof(error_msg), "HTTP %ld", http_code);
            set_resource_error(res, error_msg);
            if (curl_resource_failure_is_optional(res)) {
                // Linked subresources may be intentionally blocked or missing;
                // keep the page-load smoke test focused on document stability.
                log_warn("curl-multi: optional subresource HTTP %ld for %s", http_code, res->url);
            } else {
                log_error("curl-multi: HTTP %ld for %s", http_code, res->url);
            }
        } else {
            log_debug("curl-multi: downloaded %zu bytes from %s (HTTP %ld)",
                      transfer->size, res->url, http_code);
            success = persist_response(transfer);
        }
    }

    return success;
}

static void transfer_free(CurlMultiTransfer* transfer) {
    if (!transfer) return;
    if (transfer->custom_headers) curl_slist_free_all(transfer->custom_headers);
    if (transfer->header_ctx) mem_free(transfer->header_ctx);
    if (transfer->easy) curl_easy_cleanup(transfer->easy);
    if (transfer->data) mem_free(transfer->data);
    mem_free(transfer);
}

static bool configure_transfer(CurlMultiTransfer* transfer) {
    NetworkResource* res = transfer->resource;
    CURL* easy = curl_easy_init();
    if (!easy) {
        set_resource_error(res, "Failed to create curl handle");
        return false;
    }

    transfer->easy = easy;
    curl_easy_setopt(easy, CURLOPT_URL, res->url);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, transfer);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, transfer_progress_callback);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, transfer);

    int timeout_ms = res->timeout_ms > 0 ? res->timeout_ms : 30000;
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "Radiant/1.0 Lambda-Script");
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy, CURLOPT_MAXCONNECTS, 6L);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, transfer);

    CookieJar* jar = (res->manager) ? res->manager->cookie_jar : NULL;
    if (jar) {
        bool is_secure = (strncasecmp(res->url, "https://", 8) == 0);
        char* cookie_value = cookie_jar_build_request_header(jar, res->url, is_secure);
        if (cookie_value) {
            size_t hdr_len = strlen(cookie_value) + 9;
            char* cookie_hdr = (char*)mem_alloc(hdr_len, MEM_CAT_NETWORK);
            if (cookie_hdr) {
                snprintf(cookie_hdr, hdr_len, "Cookie: %s", cookie_value);
                transfer->custom_headers = curl_slist_append(transfer->custom_headers, cookie_hdr);
                curl_easy_setopt(easy, CURLOPT_HTTPHEADER, transfer->custom_headers);
                mem_free(cookie_hdr);
            }
            mem_free(cookie_value);
        }

        transfer->header_ctx =
            (HeaderCallbackCtx*)mem_calloc(1, sizeof(HeaderCallbackCtx), MEM_CAT_NETWORK);
        if (transfer->header_ctx) {
            transfer->header_ctx->jar = jar;
            transfer->header_ctx->request_url = res->url;
            curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_callback);
            curl_easy_setopt(easy, CURLOPT_HEADERDATA, transfer->header_ctx);
            curl_easy_setopt(easy, CURLOPT_PRIVATE, transfer);
        }
    }

    log_debug("curl-multi: queued transfer %s (timeout=%dms)", res->url, timeout_ms);
    return true;
}

static void request_free(CurlMultiRequest* request) {
    if (request) mem_free(request);
}

static void complete_request(CurlMultiBackend* backend,
                             CurlMultiTransfer* transfer,
                             bool success) {
    void* request_data = transfer->request_data;

    pthread_mutex_lock(&backend->mutex);
    if (backend->active_count > 0) backend->active_count--;
    pthread_cond_broadcast(&backend->cond);
    pthread_mutex_unlock(&backend->mutex);

    if (backend->callback) {
        backend->callback(request_data, success, backend->callback_data);
    }
}

static bool add_pending_transfer(CurlMultiBackend* backend, CurlMultiRequest* request) {
    CurlMultiTransfer* transfer =
        (CurlMultiTransfer*)mem_calloc(1, sizeof(CurlMultiTransfer), MEM_CAT_NETWORK);
    if (!transfer) return false;

    transfer->resource = request->resource;
    transfer->request_data = request->request_data;
    if (!configure_transfer(transfer)) {
        transfer_free(transfer);
        return false;
    }

    CURLMcode code = curl_multi_add_handle(backend->multi, transfer->easy);
    if (code != CURLM_OK) {
        log_error("curl-multi: failed to add easy handle: %s", curl_multi_strerror(code));
        set_resource_error(request->resource, curl_multi_strerror(code));
        transfer_free(transfer);
        return false;
    }

    pthread_mutex_lock(&backend->mutex);
    backend->active_count++;
    pthread_cond_broadcast(&backend->cond);
    pthread_mutex_unlock(&backend->mutex);
    return true;
}

static void drain_pending_requests(CurlMultiBackend* backend) {
    while (true) {
        pthread_mutex_lock(&backend->mutex);
        CurlMultiRequest* request = NULL;
        if (backend->pending && backend->pending->length > 0) {
            request = (CurlMultiRequest*)backend->pending->data[0];
            arraylist_remove(backend->pending, 0);
        }
        pthread_mutex_unlock(&backend->mutex);

        if (!request) break;

        if (!add_pending_transfer(backend, request)) {
            if (backend->callback) {
                backend->callback(request->request_data, false, backend->callback_data);
            }
        }
        request_free(request);
    }
}

static void reap_completed_transfers(CurlMultiBackend* backend) {
    int messages_left = 0;
    CURLMsg* msg = NULL;
    while ((msg = curl_multi_info_read(backend->multi, &messages_left)) != NULL) {
        if (msg->msg != CURLMSG_DONE) continue;

        CurlMultiTransfer* transfer = NULL;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &transfer);
        if (!transfer) continue;

        curl_multi_remove_handle(backend->multi, transfer->easy);
        bool success = finish_transfer(transfer, msg->data.result);
        complete_request(backend, transfer, success);
        transfer_free(transfer);
    }
}

static void* curl_multi_thread_main(void* arg) {
    CurlMultiBackend* backend = (CurlMultiBackend*)arg;
    if (!backend) return NULL;

    backend->multi = curl_multi_init();
    if (!backend->multi) {
        log_error("curl-multi: failed to create multi handle");
        return NULL;
    }

    int running_handles = 0;
    while (true) {
        drain_pending_requests(backend);

        CURLMcode code = curl_multi_perform(backend->multi, &running_handles);
        if (code != CURLM_OK) {
            log_error("curl-multi: perform failed: %s", curl_multi_strerror(code));
        }
        reap_completed_transfers(backend);

        pthread_mutex_lock(&backend->mutex);
        bool should_stop = backend->stop_thread &&
            backend->active_count == 0 &&
            (!backend->pending || backend->pending->length == 0);
        pthread_mutex_unlock(&backend->mutex);
        if (should_stop) break;

        int numfds = 0;
        curl_multi_poll(backend->multi, NULL, 0, CURL_MULTI_POLL_TIMEOUT_MS, &numfds);
    }

    curl_multi_cleanup(backend->multi);
    backend->multi = NULL;
    return NULL;
}

extern "C" {

CurlMultiBackend* curl_multi_backend_create(CurlMultiCompletionCallback callback,
                                            void* user_data) {
    if (!init_curl_multi_global()) return NULL;

    CurlMultiBackend* backend =
        (CurlMultiBackend*)mem_calloc(1, sizeof(CurlMultiBackend), MEM_CAT_NETWORK);
    if (!backend) return NULL;

    backend->callback = callback;
    backend->callback_data = user_data;
    backend->pending = arraylist_new(16);
    if (!backend->pending ||
        pthread_mutex_init(&backend->mutex, NULL) != 0 ||
        pthread_cond_init(&backend->cond, NULL) != 0) {
        if (backend->pending) arraylist_free(backend->pending);
        mem_free(backend);
        return NULL;
    }

    if (pthread_create(&backend->thread, NULL, curl_multi_thread_main, backend) != 0) {
        pthread_cond_destroy(&backend->cond);
        pthread_mutex_destroy(&backend->mutex);
        arraylist_free(backend->pending);
        mem_free(backend);
        return NULL;
    }
    backend->thread_started = true;

    log_debug("curl-multi: backend created");
    return backend;
}

void curl_multi_backend_destroy(CurlMultiBackend* backend) {
    if (!backend) return;
    curl_multi_backend_shutdown(backend);

    if (backend->thread_started) {
        pthread_join(backend->thread, NULL);
    }

    if (backend->pending) {
        for (int i = 0; i < backend->pending->length; i++) {
            request_free((CurlMultiRequest*)backend->pending->data[i]);
        }
        arraylist_free(backend->pending);
    }
    pthread_cond_destroy(&backend->cond);
    pthread_mutex_destroy(&backend->mutex);
    mem_free(backend);
    log_debug("curl-multi: backend destroyed");
}

bool curl_multi_backend_submit(CurlMultiBackend* backend,
                               struct NetworkResource* resource,
                               void* request_data) {
    if (!backend || !resource) return false;

    CurlMultiRequest* request =
        (CurlMultiRequest*)mem_calloc(1, sizeof(CurlMultiRequest), MEM_CAT_NETWORK);
    if (!request) return false;
    request->resource = resource;
    request->request_data = request_data;

    pthread_mutex_lock(&backend->mutex);
    if (backend->shutdown_flag) {
        pthread_mutex_unlock(&backend->mutex);
        request_free(request);
        return false;
    }

    bool ok = arraylist_append(backend->pending, request) != 0;
    pthread_cond_broadcast(&backend->cond);
    pthread_mutex_unlock(&backend->mutex);

    if (backend->multi) {
        curl_multi_wakeup(backend->multi);
    }
    if (!ok) request_free(request);
    return ok;
}

bool curl_multi_backend_cancel(CurlMultiBackend* backend, void* request_data) {
    if (!backend || !request_data) return false;

    pthread_mutex_lock(&backend->mutex);
    bool cancelled = false;
    if (backend->pending) {
        for (int i = 0; i < backend->pending->length; i++) {
            CurlMultiRequest* request = (CurlMultiRequest*)backend->pending->data[i];
            if (request && request->request_data == request_data) {
                arraylist_remove(backend->pending, i);
                request_free(request);
                cancelled = true;
                break;
            }
        }
    }
    pthread_cond_broadcast(&backend->cond);
    pthread_mutex_unlock(&backend->mutex);
    if (cancelled && backend->multi) curl_multi_wakeup(backend->multi);
    return cancelled;
}

void curl_multi_backend_wait_all(CurlMultiBackend* backend) {
    if (!backend) return;

    pthread_mutex_lock(&backend->mutex);
    while (backend->active_count > 0 || (backend->pending && backend->pending->length > 0)) {
        pthread_cond_wait(&backend->cond, &backend->mutex);
    }
    pthread_mutex_unlock(&backend->mutex);
}

void curl_multi_backend_shutdown(CurlMultiBackend* backend) {
    if (!backend) return;

    pthread_mutex_lock(&backend->mutex);
    backend->shutdown_flag = true;
    backend->stop_thread = true;
    ArrayList* cancelled = NULL;
    if (backend->pending && backend->pending->length > 0) {
        cancelled = arraylist_new(backend->pending->length);
        if (cancelled) {
            for (int i = 0; i < backend->pending->length; i++) {
                arraylist_append(cancelled, backend->pending->data[i]);
            }
            arraylist_clear(backend->pending);
        }
    }
    pthread_cond_broadcast(&backend->cond);
    pthread_mutex_unlock(&backend->mutex);

    if (cancelled) {
        for (int i = 0; i < cancelled->length; i++) {
            CurlMultiRequest* request = (CurlMultiRequest*)cancelled->data[i];
            if (backend->callback) {
                backend->callback(request->request_data, false, backend->callback_data);
            }
            request_free(request);
        }
        arraylist_free(cancelled);
    }

    if (backend->multi) curl_multi_wakeup(backend->multi);
}

} // extern "C"
