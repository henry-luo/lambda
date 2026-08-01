package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("larceny", "quicksort") {
		os.Exit(1)
	}
}
