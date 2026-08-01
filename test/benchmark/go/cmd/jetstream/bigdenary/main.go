package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "bigdenary") {
		os.Exit(1)
	}
}
