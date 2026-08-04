#pragma once

// shared native hosted-call ABI for Core Lambda, LambdaJS, and other hosted
// runtimes.  The caller performs language-specific target selection, rooting,
// and adaptation; this header only expands a checked Item argument count into
// the matching fixed C++ function-pointer prototype.

#include "../lambda.h"

template <int... indices>
struct LambdaHostedItemIndexSequence {};

template <int count, int... indices>
struct LambdaHostedMakeIndexSequence
    : LambdaHostedMakeIndexSequence<count - 1, count - 1, indices...> {};

template <int... indices>
struct LambdaHostedMakeIndexSequence<0, indices...> {
    typedef LambdaHostedItemIndexSequence<indices...> Type;
};

template <typename IndexSequence>
struct LambdaHostedItemInvoker;

template <typename IndexSequence>
struct LambdaHostedItemPrefixInvoker;

template <int>
using LambdaHostedItem = Item;

template <int... indices>
struct LambdaHostedItemInvoker<LambdaHostedItemIndexSequence<indices...>> {
    static Item invoke(void* function_pointer, const Item* args) {
        typedef Item (*HostedItemFunction)(LambdaHostedItem<indices>...);
        return ((HostedItemFunction)function_pointer)(args[indices]...);
    }
};

template <int... indices>
struct LambdaHostedItemPrefixInvoker<LambdaHostedItemIndexSequence<indices...>> {
    static Item invoke(void* function_pointer, Item prefix, const Item* args) {
        typedef Item (*HostedItemFunction)(Item, LambdaHostedItem<indices>...);
        return ((HostedItemFunction)function_pointer)(prefix, args[indices]...);
    }
};

template <int count>
static Item lambda_hosted_item_invoke_arity(void* function_pointer,
        const Item* args) {
    typedef typename LambdaHostedMakeIndexSequence<count>::Type Indices;
    return LambdaHostedItemInvoker<Indices>::invoke(function_pointer, args);
}

template <int count>
static Item lambda_hosted_item_invoke_prefix_arity(void* function_pointer,
        Item prefix, const Item* args) {
    typedef typename LambdaHostedMakeIndexSequence<count>::Type Indices;
    return LambdaHostedItemPrefixInvoker<Indices>::invoke(function_pointer,
        prefix, args);
}

static Item lambda_hosted_item_invoke_by_count(void* function_pointer,
        const Item* args, int count, bool has_prefix, Item prefix) {
    if (!function_pointer || count < 0 || count > LAMBDA_MAX_FUNCTION_ARGS ||
            (has_prefix && count >= LAMBDA_MAX_FUNCTION_ARGS)) {
        return ItemError;
    }

    // keep this as the sole shared native Item-ABI count switch.  A runtime
    // adapter may prepend one explicit Item (for example, a closure state
    // handle); that prefix consumes one of the same 16 native operands.
    switch (count) {
    case 0: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<0>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<0>(function_pointer, args);
    case 1: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<1>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<1>(function_pointer, args);
    case 2: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<2>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<2>(function_pointer, args);
    case 3: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<3>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<3>(function_pointer, args);
    case 4: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<4>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<4>(function_pointer, args);
    case 5: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<5>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<5>(function_pointer, args);
    case 6: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<6>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<6>(function_pointer, args);
    case 7: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<7>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<7>(function_pointer, args);
    case 8: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<8>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<8>(function_pointer, args);
    case 9: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<9>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<9>(function_pointer, args);
    case 10: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<10>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<10>(function_pointer, args);
    case 11: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<11>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<11>(function_pointer, args);
    case 12: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<12>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<12>(function_pointer, args);
    case 13: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<13>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<13>(function_pointer, args);
    case 14: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<14>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<14>(function_pointer, args);
    case 15: return has_prefix
        ? lambda_hosted_item_invoke_prefix_arity<15>(function_pointer, prefix, args)
        : lambda_hosted_item_invoke_arity<15>(function_pointer, args);
    case 16: return lambda_hosted_item_invoke_arity<16>(function_pointer, args);
    default: return ItemError;
    }
}
