// curl_multi_backend.h
// Dedicated curl multi backend for Radiant HTTP resource transfers.

#ifndef CURL_MULTI_BACKEND_H
#define CURL_MULTI_BACKEND_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct NetworkResource;

typedef struct CurlMultiBackend CurlMultiBackend;
typedef void (*CurlMultiCompletionCallback)(void* request_data, bool success, void* user_data);

CurlMultiBackend* curl_multi_backend_create(CurlMultiCompletionCallback callback,
                                            void* user_data);
void curl_multi_backend_destroy(CurlMultiBackend* backend);

bool curl_multi_backend_submit(CurlMultiBackend* backend,
                               struct NetworkResource* resource,
                               void* request_data);
bool curl_multi_backend_cancel(CurlMultiBackend* backend, void* request_data);
void curl_multi_backend_wait_all(CurlMultiBackend* backend);
void curl_multi_backend_shutdown(CurlMultiBackend* backend);

#ifdef __cplusplus
}
#endif

#endif // CURL_MULTI_BACKEND_H
