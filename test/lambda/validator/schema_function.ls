// Schema for function types - demonstrates fn_type syntax
type SimpleFunction = fn (int, int) int
type ComplexFunction = fn (string, any) string
type Callback = fn (any) void

type FunctionTypes = {
    simple_function: {
        name: string,
        params: string*,
        return_type: string,
        body: string
    },
    complex_function: {
        name: string,
        params: string*,
        return_type: string,
        async: bool?,
        body: string
    },
    callback: {
        name: string,
        params: string*,
        return_type: string,
        body: string
    }
}
