package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "navier_stokes") {
		os.Exit(1)
	}
}
