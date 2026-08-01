package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("larceny", "gcbench") {
		os.Exit(1)
	}
}
