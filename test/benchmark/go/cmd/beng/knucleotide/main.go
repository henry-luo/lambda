package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "knucleotide") {
		os.Exit(1)
	}
}
