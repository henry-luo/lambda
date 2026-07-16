#ifndef LAMBDA_FONT_WEIGHT_H
#define LAMBDA_FONT_WEIGHT_H

#include <stdbool.h>

static inline int font_css_weight_search_phase(int requested, int candidate) {
    if (requested >= 400 && requested <= 500) {
        if (candidate >= requested && candidate <= 500) return 0;
        if (candidate < requested) return 1;
        return 2;
    }
    if (requested < 400) return candidate <= requested ? 0 : 1;
    return candidate >= requested ? 0 : 1;
}

static inline bool font_css_weight_phase_is_ascending(int requested, int phase) {
    if (requested >= 400 && requested <= 500) return phase != 1;
    if (requested < 400) return phase == 1;
    return phase == 0;
}

static inline bool font_css_weight_is_better(int requested, int candidate,
                                             int incumbent) {
    int candidate_phase = font_css_weight_search_phase(requested, candidate);
    int incumbent_phase = font_css_weight_search_phase(requested, incumbent);
    if (candidate_phase != incumbent_phase) return candidate_phase < incumbent_phase;

    bool ascending = font_css_weight_phase_is_ascending(requested, candidate_phase);
    return ascending ? candidate < incumbent : candidate > incumbent;
}

#endif
