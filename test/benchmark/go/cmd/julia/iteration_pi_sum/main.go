package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("julia", "iteration_pi_sum") {
		os.Exit(1)
	}
}
