// @@split constructs a sticky RegExp before allocating its output array.
// The forced-GC gate proves the native frame retains that matcher across each
// allocation, rather than leaving its RE2 payload reachable only by a C++ local.
console.log("alpha::beta::gamma".split(/::/g).join("|"));
