#pragma once

// evaluation consumers add runtime state and roots to the shared data model.
#include "lambda/core/data.hpp"
#include "lambda.hpp"
#include "runtime/eval_context.hpp"
#include "runtime/lambda-root-frame.hpp"

// Resolve the Input that owns an active UI result arena.
Input* ui_result_input(Arena* arena, bool report_missing = true);
