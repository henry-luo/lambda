package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "regex_dna") {
		os.Exit(1)
	}
}
