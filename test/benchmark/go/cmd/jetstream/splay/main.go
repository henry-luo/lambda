package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "splay") {
		os.Exit(1)
	}
}
