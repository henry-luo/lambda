package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("kostya", "levenshtein") {
		os.Exit(1)
	}
}
