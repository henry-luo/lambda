#include "target_identity.h"
#include "../lambda-data.hpp"

extern "C" bool target_equal(Target* first, Target* second) {
    if (first == second) return true;
    if (!first || !second) return false;
    if (first->url_hash != second->url_hash) return false;

    // The hash is only a lookup accelerator; S2.4.2v4 makes the normalized
    // target value the identity, so a collision must fall through to content.
    if (first->type != second->type || first->scheme != second->scheme) {
        return false;
    }
    if (first->type == TARGET_TYPE_URL) {
        return url_equals(first->url, second->url);
    }
    return path_equal(first->path, second->path);
}
