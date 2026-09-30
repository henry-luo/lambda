package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "fast_diff") {
		os.Exit(1)
	}
}
