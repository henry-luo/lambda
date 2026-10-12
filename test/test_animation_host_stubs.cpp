#include "../radiant/view.hpp"

// isolated scheduler/player targets are opaque tokens; these tests have no DOM or skipped subtrees.
bool view_content_visibility_hides(DomElement*, ContentVisibilityCheck, bool) {
    return false;
}
