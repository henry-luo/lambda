#include "../lambda-data.hpp"
#include "runtime-state.h"

// Keep the active evaluator in the runtime layer so runner orchestration and
// focused runtime fixtures share one provider without linking each other.
__thread EvalContext* context = nullptr;
