#include "target_identity.h"
#include "../lambda-data.hpp"

extern "C" bool target_equal(Target* first, Target* second) {
    if (first == second) return true;
    if (!first || !second) return false;
    return first->url_hash == second->url_hash;
}
