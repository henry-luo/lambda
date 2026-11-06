#include "document_host.hpp"
#include "../../lib/provider.hpp"

static const LambdaDocumentHostOps* document_host_operations = nullptr;

bool lambda_document_host_register(const LambdaDocumentHostOps* operations) {
    return lambda_provider_register(&document_host_operations, operations,
        &LambdaDocumentHostOps::cleanup_ready, &LambdaDocumentHostOps::cleanup);
}

bool lambda_document_cleanup_ready(void* document) {
    if (!document) return true;
    const LambdaDocumentHostOps* operations = lambda_provider_load(&document_host_operations);
    return operations && operations->cleanup_ready(document);
}

bool lambda_document_cleanup(void* document) {
    if (!document) return false;
    const LambdaDocumentHostOps* operations = lambda_provider_load(&document_host_operations);
    return operations && operations->cleanup(document);
}
