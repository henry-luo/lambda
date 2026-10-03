package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("julia", "parse_integers") {
		os.Exit(1)
	}
}
