// Pipe with system function call injection tests
// Covers: data | sysfunc(args) where piped data is injected as first argument

// join with separator via pipe (the original failing case)
'=1='; ["a", "b", "c"] | join(" ")

// join with different separator
'=2='; ["hello", "world"] | join(", ")

// join with empty separator
'=3='; ["x", "y", "z"] | join("")

// join on computed list via pipe
'=4='; (for (i in 1 to 3) string(i)) | join("-")

// split via pipe
'=5='; "a,b,c" | split(",")

// split with limit via pipe
'=6='; "a-b-c-d" | split("-", 2)

// replace via pipe
'=7='; "hello world" | replace("world", "lambda")

// contains via pipe
'=8a='; "hello world" | contains("world")
'=8b='; "hello world" | contains("xyz")

// starts_with / ends_with via pipe
'=9a='; "hello" | starts_with("hel")
'=9b='; "hello" | ends_with("llo")

// slice via pipe
'=10a='; [10, 20, 30, 40, 50] | slice(1, 3)
'=10b='; "hello" | slice(0, 3)

// index_of via pipe
'=11='; "hello world" | index_of("world")

// chained pipes: split then join
'=12='; "a,b,c" | split(",") | join(" ")
