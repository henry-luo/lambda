package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("kostya", "collatz") {
		os.Exit(1)
	}
}
