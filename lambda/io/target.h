#pragma once

#include "../lambda.h"
#include "../core/target_identity.h"
#include "../../lib/strview.h"

// Target is the IO-owned normalization of a Lambda Path or URL. Runtime and
// parser consumers may inspect it, but URL/path ownership remains in lambda-io.
typedef struct Url Url;

typedef enum {
    TARGET_SCHEME_FILE = 0,
    TARGET_SCHEME_HTTP,
    TARGET_SCHEME_HTTPS,
    TARGET_SCHEME_SYS,
    TARGET_SCHEME_FTP,
    TARGET_SCHEME_DATA,
    TARGET_SCHEME_UNKNOWN
} TargetScheme;

typedef enum {
    TARGET_TYPE_URL = 0,
    TARGET_TYPE_PATH
} TargetType;

typedef struct Target {
    TargetScheme scheme;
    TargetType type;
    uint64_t url_hash;
    const char* original;
    const char* name_prefix;  // optional lexical hint, excluded from identity
    char name_separator;
    union {
        Url* url;
        Path* path;
    };
} Target;

#ifdef __cplusplus
extern "C" {
#endif

Target* item_to_target(uint64_t item, Url* cwd);
Target* namespace_target(Pool* owner, const char* uri, const char* prefix,
                         char separator);
void* target_to_local_path(Target* target, Url* cwd);
const char* target_to_url_string(Target* target, void* out_buf);
bool target_is_local(Target* target);
bool target_is_remote(Target* target);
bool target_is_dir(Target* target);
bool target_exists(Target* target);
void target_free(Target* target);

const char* namespace_name_spelling(Pool* owner, Target* ns, StrView local, char separator);

void target_cleanup(void* target); // pool cleanup callback for owned namespace targets

#ifdef __cplusplus
}
#endif
