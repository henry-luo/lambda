package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("julia", "matrix_statistics") {
		os.Exit(1)
	}
}
