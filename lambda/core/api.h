#pragma once

// import shared visibility annotations without changing a consumer's value ABI.
#ifdef LAMBDA_API_ONLY
#include "lambda/lambda.h"
#else
#define LAMBDA_API_ONLY
#include "lambda/lambda.h"
#undef LAMBDA_API_ONLY
#endif
