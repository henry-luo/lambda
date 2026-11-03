// S7.5.5/S7.9.3: conversion cannot erase an unacknowledged error.
pn main() {
    u64(error("lost"))
    42
}
