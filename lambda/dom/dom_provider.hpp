#pragma once

// Native provider absence does not alter output records (D7.1.3 / D7.5.3).
template<class Operations, class Result, class... Parameters, class... Arguments>
inline Result dom_provider_call(const Operations* operations,
                                Result (*Operations::*operation)(Parameters...),
                                Arguments... arguments) {
    if (operations && operations->*operation)
        return (operations->*operation)(arguments...);
    return Result();
}
