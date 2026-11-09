#pragma once

#include "lib/provider.hpp"

// Native provider absence does not alter output records (D9.1.3 / D9.5.6).
template<class Operations, class Result, class... Parameters, class... Arguments>
inline Result dom_provider_call(const Operations* operations,
                                Result (*Operations::*operation)(Parameters...),
                                Arguments... arguments) {
    return lambda_provider_call(operations, operation, arguments...);
}
