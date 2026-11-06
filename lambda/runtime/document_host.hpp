#pragma once

// a host retains ownership until both retirement checks succeed (D5.5.2).
// the registered table has process lifetime and borrows each document only
// during its call. missing providers refuse retirement of a live document.
struct LambdaDocumentHostOps {
    bool (*cleanup_ready)(void* document);
    bool (*cleanup)(void* document);
};

bool lambda_document_host_register(const LambdaDocumentHostOps* operations);
bool lambda_document_cleanup_ready(void* document);
bool lambda_document_cleanup(void* document);
