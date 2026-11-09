#pragma once

#include <stdbool.h>
#include "lambda/io/security_io.h"

typedef struct EvalContext EvalContext;
typedef struct Path Path;
typedef struct Url Url;

#ifdef __cplusplus
extern "C" {
#endif

// D6.4: the evaluation context owns its client binding. The host transport must
// outlive the context; binding once never lends broker management to scripts.
bool eval_context_bind_security_io(EvalContext* owner, const SecurityIOClient* client);
const SecurityIOClient* eval_context_security_io(EvalContext* owner);
// Trusted lexical metadata, not a grant. Capture before native confinement;
// install once after binding, before source execution. Context owns a copy.
bool eval_context_bind_security_directory(EvalContext* owner, const char* directory);
// Caller owns the URL. Explicit host metadata never requires ambient getcwd.
Url* eval_context_current_directory(EvalContext* owner);
// Runtime C adapters query the active owner; the core path layer never sees TLS.
const SecurityIOClient* lambda_current_security_io(void);
bool lambda_security_path_cache_get(Path* path, const SecurityAuthorityStamp* stamp, uint64_t* value);
bool lambda_security_path_cache_store(Path* path, const SecurityAuthorityStamp* stamp, uint64_t value);

#ifdef __cplusplus
}
#endif
